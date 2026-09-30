#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Simulate the PmodVGA output against an independent model of every pin.

LiteX's video timing generator drives a small frame whose pixels are a
function of position. For each of the sixteen modes (four placements, two pin
numberings, stream or test pattern) every pin of both sockets must match, two pixel clocks later,
the model's pin for that placement: colour bits from the stream's top four
bits or the pattern, zero outside the active area, and active-low syncs.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex")]

from migen import Module, Signal, run_simulation

from litex.soc.cores.video import VideoTimingGenerator

from vga_output import J1, J2, VGAOutput


HRES = 32
VRES = 16
TIMINGS = {
    "pix_clk": 1e6,
    "h_active": HRES,
    "h_blanking": 8,
    "h_sync_offset": 2,
    "h_sync_width": 3,
    "v_active": VRES,
    "v_blanking": 4,
    "v_sync_offset": 1,
    "v_sync_width": 2,
}
FRAME = (HRES + 8) * (VRES + 4)
LATENCY = 2
BARS = [(1, 1, 1), (1, 1, 0), (0, 1, 1), (0, 1, 0),
        (1, 0, 1), (1, 0, 0), (0, 0, 1), (0, 0, 0)]


def stream_rgb(n):
    """Stream colours as the DUT derives them from its cycle counter."""
    return ((n * 37) & 0xff, (n * 5 + 71) & 0xff, (n * 113 + 3) & 0xff)


def pattern_rgb(x, y):
    """Four-bit colours of the test pattern, written from its description."""
    if y < VRES * 3 // 4:
        return tuple(15 * on for on in BARS[x // (HRES // 8)])
    band = (y - VRES * 3 // 4) // (VRES // 16)
    level = x // (HRES // 16)
    return tuple(level if band in (channel, 3) else 0 for channel in range(3))


def expected_pins(mode, sample):
    de, hsync, vsync, x, y, rgb = sample
    if mode & 4:
        colours = pattern_rgb(x, y) if de else (0, 0, 0)
    else:
        colours = tuple(c >> 4 if de else 0 for c in rgb)
    value = {"hs": 1 - hsync, "vs": 1 - vsync, None: 0}
    for channel, name in enumerate("rgb"):
        for bit in range(4):
            value[f"{name}{bit}"] = (colours[channel] >> bit) & 1
    # Placement from the description in vga_output.py, not socket_signals().
    first, second = (J2, J1) if mode & 1 else (J1, J2)
    pins = []
    for header in (first, second):
        by_pin = header[4:] + header[:4] if mode & 2 else header
        io = [None] * 8
        for position, name in enumerate(by_pin):
            row, column = divmod(position, 4)   # pins 1-4, then 7-10
            io[position if mode & 8 else 2 * column + row] = name
        pins.append([value[name] for name in io])
    return pins


class DUT(Module):
    def __init__(self):
        self.submodules.vtg = VideoTimingGenerator(default_video_timings=TIMINGS)
        self.pmod_a = Signal(8)
        self.pmod_b = Signal(8)
        timing = self.vtg.source
        # Stream colours are a function of a free-running counter the test
        # reads, so the test never writes a signal.
        self.n = Signal(16)
        self.sync += self.n.eq(self.n + 1)
        self.r, self.g, self.b = Signal(8), Signal(8), Signal(8)
        self.comb += [
            self.r.eq(self.n * 37),
            self.g.eq(self.n * 5 + 71),
            self.b.eq(self.n * 113 + 3),
        ]

        class Sink:
            pass
        sink = Sink()
        sink.de, sink.hsync, sink.vsync = timing.de, timing.hsync, timing.vsync
        sink.r, sink.g, sink.b = self.r, self.g, self.b
        self.comb += timing.ready.eq(1)
        self.submodules.vga = VGAOutput(sink, self.pmod_a, self.pmod_b,
            h_active=HRES, v_active=VRES)


def bits(value):
    return [(value >> i) & 1 for i in range(8)]


def check(dut, failures):
    timing = dut.vtg.source
    samples = []
    compared = 0
    patterned = set()
    # Pixel position, counted from data-enable as the framebuffer does (the
    # generator's hcount/vcount lead data-enable and step mid-line).
    x = y = 0
    de_last = 0
    for mode in range(16):
        yield dut.vga.mode.eq(mode)
        samples.clear()
        # One frame to settle the mode and the pattern's row counter.
        for cycle in range(2 * FRAME + LATENCY):
            de = yield timing.de
            hsync = yield timing.hsync
            vsync = yield timing.vsync
            if vsync:
                y = 0
            elif de_last and not de:
                y += 1
            x = x if de else 0
            rgb = stream_rgb((yield dut.n))
            yield
            samples.append((de, hsync, vsync, x, y, rgb))
            de_last = de
            x += de
            if cycle < FRAME + LATENCY:
                continue
            # Reads after a clock edge see registers loaded by it, so the
            # pins now show the sample LATENCY - 1 cycles before this one.
            sample = samples[-LATENCY]
            observed = [bits((yield dut.pmod_a)), bits((yield dut.pmod_b))]
            wanted = expected_pins(mode, sample)
            compared += 1
            if mode & 4 and sample[0]:
                patterned.add(pattern_rgb(sample[3], sample[4]))
            if observed != wanted and len(failures) < 5:
                failures.append(f"mode {mode} cycle {cycle}: "
                                f"pins {observed} expected {wanted} "
                                f"from {sample[:5]}")
    failures.append(("compared", compared, len(patterned)))


def main():
    failures = []
    dut = DUT()
    # The timing generator's enable crosses from sys; run everything in sys.
    run_simulation(dut, check(dut, failures))
    summary = failures.pop()
    if failures:
        print("\n".join(failures))
        print("FAIL")
        return 1
    _, compared, colours = summary
    # The 8 bars plus 14 new levels of each ramp: level 0 is black and level
    # 15 is the full red, green, blue or white bar.
    if colours != 8 + 4 * 14:
        print(f"FAIL: pattern showed {colours} distinct colours")
        return 1
    print(f"PASS: {compared} pixel clocks across 16 modes, "
          f"{colours} distinct pattern colours")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
