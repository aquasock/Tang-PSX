#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Simulate the Gate 1 framebuffer's 256-bit RGB565 scanout path."""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import ClockDomainsRenamer, Module, passive, run_simulation
from migen.fhdl.specials import Memory

# Migen cannot lower write-only ports in the framebuffer's buffered FIFOs.
_get_port = Memory.get_port
def _sim_get_port(self, *args, read_capable=True, **kwargs):
    return _get_port(self, *args, read_capable=True, **kwargs)
Memory.get_port = _sim_get_port

from litedram.common import LiteDRAMNativePort
from litex.soc.cores.video import VideoFrameBuffer, VideoTimingGenerator


HRES = 16
VRES = 4
TIMINGS = {
    "pix_clk": 1e6,
    "h_active": HRES,
    "h_blanking": 6,
    "h_sync_offset": 2,
    "h_sync_width": 2,
    "v_active": VRES,
    "v_blanking": 4,
    "v_sync_offset": 2,
    "v_sync_width": 1,
}


def pixel(index):
    return (((index * 3) & 31) << 11) | (((index * 5) & 63) << 5) | ((index * 7) & 31)


PIXELS = [pixel(index) for index in range(HRES * VRES)]
WORDS = [sum(PIXELS[16*word + lane] << (16*lane) for lane in range(16))
         for word in range(len(PIXELS)//16)]
EXPECTED = [(((value >> 11) & 31) << 3,
             ((value >>  5) & 63) << 2,
             ( value        & 31) << 3) for value in PIXELS]


class DUT(Module):
    def __init__(self):
        self.port = LiteDRAMNativePort("read", address_width=10, data_width=256)
        self.submodules.vtg = ClockDomainsRenamer("hdmi")(
            VideoTimingGenerator(default_video_timings=TIMINGS))
        self.submodules.framebuffer = VideoFrameBuffer(
            self.port,
            hres=HRES,
            vres=VRES,
            fifo_depth=64,
            clock_domain="hdmi",
            format="rgb565",
        )
        self.comb += self.vtg.source.connect(self.framebuffer.vtg_sink)


@passive
def memory(dut):
    port = dut.port
    pending = []
    while True:
        yield port.cmd.ready.eq(1)
        yield port.rdata.valid.eq(bool(pending))
        if pending:
            yield port.rdata.data.eq(pending[0])
        yield
        if (yield port.rdata.valid) and (yield port.rdata.ready):
            pending.pop(0)
        if (yield port.cmd.valid) and (yield port.cmd.ready):
            pending.append(WORDS[(yield port.cmd.addr) % len(WORDS)])


def capture(dut, captured, state):
    fb = dut.framebuffer
    yield fb.dma._enable.storage.eq(1)
    yield fb.source.ready.eq(1)
    for _ in range(10000):
        if (yield fb.source.valid) and (yield fb.source.ready) and (yield fb.source.de):
            captured.append(((yield fb.source.r), (yield fb.source.g), (yield fb.source.b)))
            if len(captured) == len(EXPECTED):
                state["underflow"] = yield fb.underflow
                return
        yield
    raise AssertionError("timed out waiting for a complete framebuffer image")


def main():
    dut = DUT()
    captured, state = [], {}
    run_simulation(dut,
        {"sys": [memory(dut)], "hdmi": [capture(dut, captured, state)]},
        clocks={"sys": 10, "hdmi": 30})
    assert captured == EXPECTED, "RGB565 pixels were reordered or converted incorrectly"
    assert state["underflow"] == 0, "framebuffer starved with an always-ready memory"
    print(f"PASS: {len(captured)} RGB565 pixels from four 256-bit DDR words")
    return 0


if __name__ == "__main__":
    sys.exit(main())
