# SPDX-License-Identifier: GPL-3.0-only

"""Wishbone command front-end for the Gate 1 PSX fabric rasterizer."""

from pathlib import Path

from migen import (Array, Cat, Case, ClockSignal, FSM, If, Instance, Mux,
    ResetInserter, ResetSignal, Signal)

from litex.gen import LiteXModule
from litex.gen import NextState, NextValue
from litex.soc.interconnect import stream, wishbone
from litedram.common import LiteDRAMNativePort

from ddr3_port_arbiter import DDR3RWArbiter


ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x47505531                 # "GPU1"
VRAM_BASE = 0x7FE0_0000
MAIN_RAM_BASE = 0x4000_0000
VRAM_NATIVE_BASE = (VRAM_BASE - MAIN_RAM_BASE) // 32


class GPUCommandDMA(LiteXModule):
    """Fetch a cached descriptor batch from DDR as a stream of 32-bit words."""

    def __init__(self, address_width=25):
        self.start = Signal()
        self.base = Signal(32)
        self.words = Signal(32)
        self.ready = Signal()
        self.error = Signal()
        self.source = stream.Endpoint([("data", 32)])
        self.port = port = LiteDRAMNativePort(
            "both", address_width=address_width, data_width=256)

        address = Signal(address_width)
        remaining = Signal(32)
        line = Signal(256)
        line_words = Signal(4)
        lane = Signal(3)

        self.comb += [
            port.cmd.we.eq(0),
            port.cmd.addr.eq(address),
            port.cmd.first.eq(1),
            port.cmd.last.eq(1),
            port.wdata.valid.eq(0),
            port.wdata.data.eq(0),
            port.wdata.we.eq(0),
            port.wdata.first.eq(1),
            port.wdata.last.eq(1),
            port.flush.eq(0),
            self.source.data.eq(Array(
                line[32*n:32*(n + 1)] for n in range(8))[lane]),
        ]

        self.fsm = fsm = FSM(reset_state="IDLE")
        fsm.act("IDLE",
            self.ready.eq(1),
            If(self.start,
                If((self.base[:5] != 0) | (self.base < MAIN_RAM_BASE),
                    NextValue(self.error, 1),
                ).Elif(self.words != 0,
                    NextValue(address,
                        (self.base - MAIN_RAM_BASE) >> 5),
                    NextValue(remaining, self.words),
                    NextState("COMMAND"),
                ),
            ),
        )
        fsm.act("COMMAND",
            port.cmd.valid.eq(1),
            If(port.cmd.ready,
                NextState("READ"),
            ),
        )
        fsm.act("READ",
            port.rdata.ready.eq(1),
            If(port.rdata.valid,
                NextValue(line, port.rdata.data),
                NextValue(lane, 0),
                NextValue(line_words, Mux(remaining > 8, 8, remaining[:4])),
                NextState("PUSH"),
            ),
        )
        fsm.act("PUSH",
            self.source.valid.eq(1),
            If(self.source.ready,
                If(lane == line_words - 1,
                    If(remaining <= 8,
                        NextValue(remaining, 0),
                        NextState("IDLE"),
                    ).Else(
                        NextValue(remaining, remaining - 8),
                        NextValue(address, address + 1),
                        NextState("COMMAND"),
                    ),
                ).Else(
                    NextValue(lane, lane + 1),
                ),
            ),
        )


class GPUAccelerator(LiteXModule):
    """Queue CPU-prepared primitives and rasterize them through a DDR port.

    Register 0 accepts descriptor words and reads as the accelerator magic.
    Register 1 reports ready/idle/error, while registers 2 and 3 expose
    completed primitive and written-pixel counters.  A write to register 1
    resets the queue and rasterizer.
    """

    def __init__(self, platform, address_width=25, fifo_depth=128):
        self.bus = bus = wishbone.Interface(
            data_width=32, address_width=32, addressing="word")
        self.port = port = LiteDRAMNativePort(
            "both", address_width=address_width, data_width=256)
        raster_port = LiteDRAMNativePort(
            "both", address_width=address_width, data_width=256)
        self.command_dma = command_dma = ResetInserter()(
            GPUCommandDMA(address_width=address_width))
        self.command_arbiter = DDR3RWArbiter(
            command_dma.port, raster_port, port)

        self.fifo = fifo = ResetInserter()(stream.SyncFIFO(
            [("data", 32)], fifo_depth, buffered=True))
        raster_ready = Signal()
        raster_idle = Signal()
        raster_error = Signal()
        reset_pulse = Signal()
        primitives = Signal(32)
        pixels = Signal(32)
        read_data = Signal(32)
        request = Signal()
        data_write = Signal()
        pending_valid = Signal()
        pending_data = Signal(32)
        batch_base = Signal(32)
        batch_words = Signal(32)
        batch_start = Signal()
        batch_write = Signal()
        batch_active = Signal()
        batch_started = Signal()

        self.comb += [
            request.eq(bus.cyc & bus.stb),
            data_write.eq(bus.we & (bus.adr[0:2] == 0)),
            batch_write.eq(bus.we & (bus.adr[0:3] == 5)),
            fifo.sink.valid.eq(pending_valid | command_dma.source.valid),
            fifo.sink.data.eq(Mux(pending_valid, pending_data,
                command_dma.source.data)),
            command_dma.source.ready.eq(fifo.sink.ready & ~pending_valid),
            command_dma.start.eq(batch_start),
            command_dma.base.eq(batch_base),
            command_dma.words.eq(batch_words),
            command_dma.reset.eq(reset_pulse),
            bus.err.eq(0),
            read_data.eq(0),
            Case(bus.adr[0:3], {
                0: read_data.eq(MAGIC),
                1: read_data.eq(Cat(~pending_valid,
                    raster_idle & ~fifo.source.valid & command_dma.ready &
                        ~pending_valid & ~batch_active,
                    raster_error | command_dma.error,
                    command_dma.ready)),
                2: read_data.eq(primitives),
                3: read_data.eq(pixels),
                4: read_data.eq(batch_base),
                5: read_data.eq(batch_words),
            }),
            fifo.reset.eq(reset_pulse),
            fifo.source.ready.eq(raster_ready),
        ]
        self.sync += [
            # Registered with ack: the address is held while stb is asserted,
            # and the SoC bus address never reaches the AE350 read-data
            # register through this mux in one cycle.
            bus.dat_r.eq(read_data),
            bus.ack.eq(0),
            reset_pulse.eq(0),
            batch_start.eq(0),
            If(batch_active & ~command_dma.ready,
                batch_started.eq(1),
            ),
            If(batch_active & batch_started & command_dma.ready,
                batch_active.eq(0),
                batch_started.eq(0),
            ),
            If(pending_valid & fifo.sink.ready,
                pending_valid.eq(0),
            ),
            If(request & ~bus.ack,
                If(data_write,
                    If(~pending_valid,
                        pending_data.eq(bus.dat_w),
                        pending_valid.eq(1),
                        bus.ack.eq(1),
                    ),
                ).Elif(batch_write,
                    If(command_dma.ready,
                        batch_words.eq(bus.dat_w),
                        batch_start.eq(1),
                        batch_active.eq(1),
                        batch_started.eq(0),
                        bus.ack.eq(1),
                    ),
                ).Else(
                    bus.ack.eq(1),
                    If(bus.we & (bus.adr[0:2] == 1),
                        reset_pulse.eq(1),
                        pending_valid.eq(0),
                        batch_active.eq(0),
                        batch_started.eq(0),
                    ).Elif(bus.we & (bus.adr[0:3] == 4),
                        batch_base.eq(bus.dat_w),
                    ),
                ),
            ),
        ]

        platform.add_source(str(ROOT / "gateware" / "gpu_rasterizer.sv"))
        self.specials += Instance("gpu_rasterizer",
            p_ADDR_BITS=address_width,
            p_VRAM_WORD_BASE=VRAM_NATIVE_BASE,
            i_clk=ClockSignal("sys"),
            i_rst=ResetSignal("sys") | reset_pulse,
            i_cmd_valid=fifo.source.valid,
            o_cmd_ready=raster_ready,
            i_cmd_data=fifo.source.data,
            o_idle=raster_idle,
            o_error=raster_error,
            o_primitives=primitives,
            o_pixels=pixels,
            o_mem_cmd_valid=raster_port.cmd.valid,
            i_mem_cmd_ready=raster_port.cmd.ready,
            o_mem_cmd_we=raster_port.cmd.we,
            o_mem_cmd_addr=raster_port.cmd.addr,
            o_mem_wdata_valid=raster_port.wdata.valid,
            i_mem_wdata_ready=raster_port.wdata.ready,
            o_mem_wdata_data=raster_port.wdata.data,
            o_mem_wdata_we=raster_port.wdata.we,
            i_mem_rdata_valid=raster_port.rdata.valid,
            o_mem_rdata_ready=raster_port.rdata.ready,
            i_mem_rdata_data=raster_port.rdata.data,
        )
        self.comb += [
            raster_port.cmd.first.eq(1),
            raster_port.cmd.last.eq(1),
            raster_port.wdata.first.eq(1),
            raster_port.wdata.last.eq(1),
            raster_port.flush.eq(0),
        ]
