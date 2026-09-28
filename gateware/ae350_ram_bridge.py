# SPDX-License-Identifier: GPL-3.0-only

"""AE350 RAM-port bridge with a registered, compare-free DDR3 path.

Gate1RAMBridge follows LiteX's AE350RAMBridge (litex/soc/cores/cpu/gowin_ae350/core.py,
BSD-2-Clause) with two changes on the DDR3 side. WishboneRegisterSlice registers the path so
no combinational logic runs into the AE350 macro's slow AHB inputs, and BurstWishbone2Native
replaces LiteDRAM's burst frontend, whose full-address merge and read-cache compares limited
the 75 MHz system clock.
"""

from migen import Case, If, Mux, Signal

from litex.gen import FSM, LiteXModule, NextState, NextValue
from litex.soc.interconnect import ahb, wishbone

from litex.soc.interconnect.wishbone import CTI_BURST_INCREMENTING


class WishboneRegisterSlice(LiteXModule):
    """Classic Wishbone slice with registered request and response.

    Each master beat becomes one slave access issued the cycle after the master presents it; the
    slave's ack/err and read data return to the master one cycle after the slave responds. CYC is
    held between beats while the master holds it, so bursting frontends still see one cycle.
    """

    def __init__(self, master, slave):
        assert len(master.dat_w) == len(slave.dat_w)
        assert len(master.adr) == len(slave.adr)

        dat_r = Signal.like(master.dat_r)
        err   = Signal()
        cyc   = Signal()

        self.sync += cyc.eq(master.cyc)
        self.comb += master.dat_r.eq(dat_r)

        self.fsm = fsm = FSM(reset_state="IDLE")
        fsm.act("IDLE",
            slave.cyc.eq(cyc),
            If(master.cyc & master.stb,
                NextValue(slave.adr,   master.adr),
                NextValue(slave.dat_w, master.dat_w),
                NextValue(slave.sel,   master.sel),
                NextValue(slave.we,    master.we),
                NextValue(slave.cti,   master.cti),
                NextValue(slave.bte,   master.bte),
                NextState("REQUEST"),
            ),
        )
        fsm.act("REQUEST",
            slave.cyc.eq(1),
            slave.stb.eq(1),
            If(slave.ack | slave.err,
                NextValue(dat_r, slave.dat_r),
                NextValue(err,   slave.err),
                NextState("RESPONSE"),
            ),
        )
        fsm.act("RESPONSE",
            slave.cyc.eq(cyc),
            master.ack.eq(~err),
            master.err.eq(err),
            NextState("IDLE"),
        )


class BurstWishbone2Native(LiteXModule):
    """64-bit Wishbone to a 256-bit LiteDRAM native port, merging beats within a burst.

    A native word is four 64-bit lanes (one 32-byte AE350 cache line). Consecutive beats of one
    burst stay in the same native word when the burst wraps at four beats (BTE 1) or is linear
    and has not yet passed lane 3, so continuation is decided from the burst state and the
    previous lane, never by comparing addresses. Write beats are merged and written once, at the
    burst's last beat. The first read beat of a burst fetches the native word and later beats
    of the same burst are served from it; the fetched word is dropped at the end of the burst
    or on any write, so no stale data survives across bursts.
    """

    def __init__(self, bus, port, base_address):
        assert len(bus.dat_w) == 64 and len(port.wdata.data) == 256
        assert bus.addressing == "word"

        lane       = Signal(2)
        native     = Signal(len(port.cmd.addr))
        incr       = Signal()
        cont       = Signal()
        in_burst   = Signal()
        prev_lane  = Signal(2)
        prev_bte   = Signal(2)
        prev_we    = Signal()
        wr_pending = Signal()
        wr_addr    = Signal(len(port.cmd.addr))
        wr_data    = Signal(256)
        wr_we      = Signal(32)
        rd_valid   = Signal()
        rd_addr    = Signal(len(port.cmd.addr))
        rd_data    = Signal(256)
        lane_data  = Signal(256)
        lane_keep  = Signal(256)
        lane_we    = Signal(32)
        rd_lane    = Signal(64)
        port_lane  = Signal(64)

        offset = base_address >> 3
        self.comb += [
            lane.eq(bus.adr[0:2]),
            native.eq((bus.adr - offset)[2:]),
            incr.eq(bus.cti == CTI_BURST_INCREMENTING),
            cont.eq(in_burst & (prev_we == bus.we) &
                ((prev_bte == 1) | ((prev_bte == 0) & (prev_lane != 3)))),
            lane_keep.eq(2**256 - 1),
            Case(lane, {i: [
                lane_data[64*i:64*(i + 1)].eq(bus.dat_w),
                lane_keep[64*i:64*(i + 1)].eq(0),
                lane_we[8*i:8*(i + 1)].eq(bus.sel),
                rd_lane.eq(rd_data[64*i:64*(i + 1)]),
                port_lane.eq(port.rdata.data[64*i:64*(i + 1)]),
            ] for i in range(4)}),
            port.flush.eq(~bus.cyc),
            port.cmd.last.eq(1),
        ]

        def track_beat():
            return [
                NextValue(in_burst,  incr),
                NextValue(prev_lane, lane),
                NextValue(prev_bte,  bus.bte),
                NextValue(prev_we,   bus.we),
            ]

        self.fsm = fsm = FSM(reset_state="IDLE")
        fsm.act("IDLE",
            If(~bus.cyc,
                NextValue(in_burst, 0),
                NextValue(rd_valid, 0),
                If(wr_pending, NextState("WRITE-CMD")),
            ).Elif(bus.stb & bus.we,
                NextValue(rd_valid, 0),
                If(wr_pending & ~cont,
                    # A write from another burst: flush first; the beat is retried.
                    NextState("WRITE-CMD"),
                ).Else(
                    bus.ack.eq(1),
                    NextValue(wr_pending, 1),
                    NextValue(wr_addr, native),
                    NextValue(wr_data, Mux(wr_pending, wr_data & lane_keep, 0) | lane_data),
                    NextValue(wr_we, Mux(wr_pending, wr_we, 0) | lane_we),
                    *track_beat(),
                    If(~incr, NextState("WRITE-CMD")),
                ),
            ).Elif(bus.stb,
                If(wr_pending,
                    NextState("WRITE-CMD"),
                ).Elif(rd_valid & cont,
                    bus.ack.eq(1),
                    bus.dat_r.eq(rd_lane),
                    *track_beat(),
                    If(~incr, NextValue(rd_valid, 0)),
                ).Else(
                    NextValue(rd_addr, native),
                    NextState("READ-CMD"),
                ),
            ),
        )
        fsm.act("WRITE-CMD",
            port.cmd.valid.eq(1),
            port.cmd.we.eq(1),
            port.cmd.addr.eq(wr_addr),
            If(port.cmd.ready, NextState("WRITE-DATA")),
        )
        fsm.act("WRITE-DATA",
            port.wdata.valid.eq(1),
            port.wdata.data.eq(wr_data),
            port.wdata.we.eq(wr_we),
            If(port.wdata.ready,
                NextValue(wr_pending, 0),
                NextState("IDLE"),
            ),
        )
        fsm.act("READ-CMD",
            port.cmd.valid.eq(1),
            port.cmd.we.eq(0),
            port.cmd.addr.eq(rd_addr),
            If(port.cmd.ready, NextState("READ-DATA")),
        )
        fsm.act("READ-DATA",
            port.rdata.ready.eq(1),
            If(port.rdata.valid,
                bus.ack.eq(bus.cyc & bus.stb),
                bus.dat_r.eq(port_lane),
                NextValue(rd_data, port.rdata.data),
                NextValue(rd_valid, incr),
                *track_beat(),
                NextState("IDLE"),
            ),
        )


class Gate1RAMBridge(LiteXModule):
    """AE350 RAM AHB port split between DDR3 (native port) and the fabric data bus."""

    def __init__(self, ahb_ram, dbus, port, origin, size):
        fabric_ahb      = ahb.AHBInterface(data_width=ahb_ram.data_width, address_width=ahb_ram.address_width)
        memory_ahb      = ahb.AHBInterface(data_width=ahb_ram.data_width, address_width=ahb_ram.address_width)
        memory_hit      = Signal()
        memory_selected = Signal()
        self.comb += memory_hit.eq((ahb_ram.addr >= origin) & (ahb_ram.addr < origin + size))
        self.sync += If(ahb_ram.readyout & ahb_ram.sel & ahb_ram.trans[1],
            memory_selected.eq(memory_hit),
        )
        for index, slave in enumerate((fabric_ahb, memory_ahb)):
            for name in ("addr", "trans", "size", "burst", "write", "wdata", "prot", "mastlock"):
                self.comb += getattr(slave, name).eq(getattr(ahb_ram, name))
            # An inactive slave must not accept the following address before global HREADY.
            self.comb += slave.sel.eq(ahb_ram.sel & ahb_ram.readyout & (memory_hit == index))
        for name in ("rdata", "readyout", "resp"):
            self.comb += getattr(ahb_ram, name).eq(Mux(memory_selected,
                getattr(memory_ahb, name), getattr(fabric_ahb, name)))

        def memory_bus():
            return wishbone.Interface(
                data_width=ahb_ram.data_width, address_width=ahb_ram.address_width, addressing="word")

        bridge_bus   = memory_bus()
        frontend_bus = memory_bus()
        self.memory_bridge   = ahb.AHB2Wishbone(memory_ahb, bridge_bus, with_bursting=True)
        self.memory_slice    = WishboneRegisterSlice(bridge_bus, frontend_bus)
        self.memory_frontend = BurstWishbone2Native(frontend_bus, port, base_address=origin)
        self.fabric_bridge   = ahb.AHB2Wishbone(fabric_ahb, dbus)
