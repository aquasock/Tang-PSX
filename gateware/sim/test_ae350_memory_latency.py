#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Time an AE350 cache-line fill through the Gate 1 DDR3 path, stage by stage.

The path is built from the modules Gate 1 uses: LiteX's bursting AHB2Wishbone,
WishboneRegisterSlice and BurstWishbone2Native (Gate1RAMBridge), DDR3RWArbiter
and DDR3PortArbiter with idle GPU and video clients, and the 75-to-100 MHz
LiteDRAMNativePortCDC with the read-data Buffer and write-data PipeValid of
gowin_ddr3.GowinDDR3. NativeAdapter re-states gateware/ddr3_vendor/
gowin_ddr3_native.sv in Migen, and the encrypted Gowin controller is modelled
as an always-ready port that returns read data a fixed number of DDR clocks
after the command. Clocks keep the 75:100 ratio (periods 40 and 30).

A 32-byte line fill is one 64-bit WRAP4 AHB read burst, the assumed AE350
line-fill form. For each controller latency the script prints when each stage
first saw the request or the data, in 75 MHz system cycles from the NONSEQ
address phase, and the cycles to the first and the last data beat.

    python3 gateware/sim/test_ae350_memory_latency.py [LATENCY ...]
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import ClockDomainsRenamer, If, Module, Signal, passive, run_simulation
from migen.fhdl.specials import Memory

from litex.soc.interconnect import ahb, stream, wishbone

from litedram.common import LiteDRAMNativePort
from litedram.frontend.adapter import LiteDRAMNativePortCDC

from ae350_ram_bridge import BurstWishbone2Native, WishboneRegisterSlice
from ddr3_port_arbiter import DDR3PortArbiter, DDR3RWArbiter

# Migen cannot lower write-only ports in the crossing FIFOs.
_get_port = Memory.get_port
def _sim_get_port(self, *args, read_capable=True, **kwargs):
    return _get_port(self, *args, read_capable=True, **kwargs)
Memory.get_port = _sim_get_port

SYS_PERIOD, DDR_PERIOD = 40, 30
ORIGIN = 0x4000_0000
ADDRESS_WIDTH = 27
LINE = ORIGIN + 0x1230


class NativeAdapter(Module):
    """gowin_ddr3_native.sv for reads: one pending command, credit-limited reads."""

    def __init__(self, port, latency, depth=8):
        self.issued = issued = Signal()
        self.returned = returned = Signal()
        pend_valid = Signal()
        pend_we = Signal()
        in_flight = Signal(max=depth + 1)
        count = Signal(max=depth + 1)
        issue_read = Signal()
        rdata_fire = Signal()
        pipe = [Signal() for _ in range(max(latency, 1))]
        self.comb += [
            issue_read.eq(pend_valid & ~pend_we & (in_flight != depth)),
            port.cmd.ready.eq(~pend_valid | issue_read),
            port.wdata.ready.eq(0),
            port.rdata.valid.eq(count != 0),
            rdata_fire.eq(port.rdata.valid & port.rdata.ready),
            issued.eq(issue_read),
            returned.eq(pipe[-1] if latency else issue_read),
        ]
        self.sync += [
            If(port.cmd.valid & port.cmd.ready,
                pend_valid.eq(1),
                pend_we.eq(port.cmd.we),
            ).Elif(issue_read,
                pend_valid.eq(0),
            ),
            in_flight.eq(in_flight + issue_read - rdata_fire),
            count.eq(count + returned - rdata_fire),
        ]
        if latency:
            self.sync += pipe[0].eq(issue_read)
            self.sync += [pipe[i].eq(pipe[i - 1]) for i in range(1, latency)]


class Path(Module):
    def __init__(self, latency):
        self.ahb = ahb.AHBInterface(data_width=64, address_width=32)
        bridge_bus = wishbone.Interface(data_width=64, address_width=32, addressing="word")
        frontend_bus = wishbone.Interface(data_width=64, address_width=32, addressing="word")
        cpu_port = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256)
        gpu_port = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256)
        video_port = LiteDRAMNativePort("read", ADDRESS_WIDTH, 256)
        cpu_gpu_port = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256)
        shared_port = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256)
        cdc_port = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256, clock_domain="sys")
        ddr_port = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256, clock_domain="ddr")

        self.submodules.bridge = ahb.AHB2Wishbone(self.ahb, bridge_bus, with_bursting=True)
        self.submodules.slice = WishboneRegisterSlice(bridge_bus, frontend_bus)
        self.submodules.frontend = BurstWishbone2Native(frontend_bus, cpu_port, ORIGIN)
        self.submodules.rw_arbiter = DDR3RWArbiter(cpu_port, gpu_port, cpu_gpu_port)
        self.submodules.arbiter = DDR3PortArbiter(cpu_gpu_port, video_port, shared_port)
        self.submodules.cdc = LiteDRAMNativePortCDC(cdc_port, ddr_port)
        self.submodules.rdata_pipe = rdata_pipe = stream.Buffer(
            cdc_port.rdata.description, pipe_valid=True, pipe_ready=True)
        self.submodules.adapter = ClockDomainsRenamer("ddr")(NativeAdapter(ddr_port, latency))
        self.comb += [
            shared_port.cmd.connect(cdc_port.cmd),
            shared_port.wdata.connect(cdc_port.wdata),
            cdc_port.rdata.connect(rdata_pipe.sink),
            rdata_pipe.source.connect(shared_port.rdata),
            gpu_port.cmd.valid.eq(0),
            video_port.cmd.valid.eq(0),
        ]

        # Stage probes, in path order.
        self.sys_probes = [
            ("Wishbone beat (AHB2Wishbone)", bridge_bus.stb),
            ("Wishbone beat (after RegisterSlice)", frontend_bus.stb),
            ("native command (BurstWishbone2Native)", cpu_port.cmd.valid),
            ("native command (after RWArbiter)", cpu_gpu_port.cmd.valid),
            ("native command (after PortArbiter skid)", shared_port.cmd.valid),
            ("read data out of CDC (sys)", cdc_port.rdata.valid),
            ("read data out of Buffer (sys)", shared_port.rdata.valid),
            ("read data at BurstWishbone2Native", cpu_port.rdata.valid),
            ("first ack after RegisterSlice", bridge_bus.ack),
        ]
        self.ddr_probes = [
            ("command out of CDC (ddr)", ddr_port.cmd.valid),
            ("controller command issued (ddr)", self.adapter.issued),
            ("controller read data (ddr)", self.adapter.returned),
            ("adapter read data valid (ddr)", ddr_port.rdata.valid),
        ]


def master(dut, result):
    """AHB master: one WRAP4 64-bit read burst of the line, pipelined per AHB."""
    bus = dut.ahb
    addresses = [LINE + 8 * lane for lane in range(4)]
    phase = 0
    cycle = 0
    pending = None
    beats = []
    yield bus.sel.eq(1)
    yield bus.size.eq(3)
    yield bus.burst.eq(2)          # WRAP4
    yield bus.write.eq(0)
    while len(beats) < 4:
        if phase < 4:
            yield bus.addr.eq(addresses[phase])
            yield bus.trans.eq(2 if phase == 0 else 3)
        else:
            yield bus.trans.eq(0)
        yield
        cycle += 1
        if (yield bus.readyout):
            if pending is not None:
                beats.append(cycle)
                pending = None
            if phase < 4:
                pending = phase
                phase += 1
        if cycle > 2000:
            raise RuntimeError("line fill did not complete")
    result["first"] = beats[0]
    result["last"] = beats[-1]


@passive
def probe(probes, period, result, domain):
    cycle = 0
    while True:
        for name, signal in probes:
            if name not in result and (yield signal):
                result[name] = (cycle * period, domain)
        yield
        cycle += 1


def measure(latency):
    dut = Path(latency)
    result = {}
    stages = {}

    sys_probe = probe(dut.sys_probes, SYS_PERIOD, stages, "sys")
    ddr_probe = probe(dut.ddr_probes, DDR_PERIOD, stages, "ddr")

    def reset_wait():
        # Let the crossing FIFOs leave reset before the burst.
        for _ in range(8):
            yield
        yield from master(dut, result)

    run_simulation(dut, {"sys": [reset_wait(), sys_probe], "ddr": [ddr_probe]},
        clocks={"sys": SYS_PERIOD, "ddr": DDR_PERIOD},
        vcd_name=None)
    start = 8 * SYS_PERIOD
    order = [name for name, _ in dut.sys_probes + dut.ddr_probes]
    timeline = sorted(((stages[name][0] - start) / SYS_PERIOD, name)
        for name in order if name in stages)
    return result, timeline


def main():
    latencies = [int(v) for v in sys.argv[1:]] or [0, 10, 20]
    for latency in latencies:
        result, timeline = measure(latency)
        print(f"controller read latency {latency} DDR clocks:")
        for when, name in timeline:
            print(f"  {when:6.2f}  {name}")
        print(f"  first beat {result['first']} and last beat "
              f"{result['last']} system cycles after the NONSEQ address phase")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
