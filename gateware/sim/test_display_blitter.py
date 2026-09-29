#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Verify PSX VRAM scaling and BGR555 conversion through the display blitter."""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import passive, run_simulation

from display_blitter import (DisplayBlitter, FRAMEBUFFER_NATIVE_BASE,
    MAIN_RAM_BASE, OUTPUT_HEIGHT, OUTPUT_WIDTH, VRAM_NATIVE_BASE)


ORIGIN_X = 1000
ORIGIN_Y = 501
SOURCE_WIDTH = 320
SOURCE_HEIGHT = 240


def bgr555(x, y):
    return ((x * 3 + y) & 31) | (((x + y * 2) & 31) << 5) | \
        (((x * 7 + y) & 31) << 10)


def rgb565(pixel):
    return ((pixel & 0x001f) << 11) | ((pixel & 0x03e0) << 1) | \
        ((pixel & 0x7c00) >> 10)


def bus_write(dut, register, value):
    yield dut.bus.adr.eq(register)
    yield dut.bus.dat_w.eq(value)
    yield dut.bus.we.eq(1)
    yield dut.bus.cyc.eq(1)
    yield dut.bus.stb.eq(1)
    yield
    while not (yield dut.bus.ack):
        yield
    yield dut.bus.cyc.eq(0)
    yield dut.bus.stb.eq(0)
    yield dut.bus.we.eq(0)
    yield


def producer(dut, state):
    yield from bus_write(dut, 2, ORIGIN_X | (ORIGIN_Y << 16))
    yield from bus_write(dut, 3, SOURCE_WIDTH | (SOURCE_HEIGHT << 16))
    yield from bus_write(dut, 1, 1)
    while not (yield dut.port.cmd.valid):
        yield
    state["started"] = True
    while not ((yield dut.bus.dat_r) & 1):
        yield dut.bus.adr.eq(1)
        yield
    state["done"] = True


@passive
def memory(dut, written):
    rng = random.Random(37)
    read_address = None
    read_delay = 0
    write_address = None
    while True:
        ready = read_address is None and write_address is None and \
            rng.random() < 0.85
        yield dut.port.cmd.ready.eq(ready)
        yield dut.port.wdata.ready.eq(
            write_address is not None and rng.random() < 0.85)
        if read_address is not None and read_delay == 0:
            offset = read_address - VRAM_NATIVE_BASE
            y = (offset // 64) & 511
            x0 = (offset % 64) * 16
            line = sum(bgr555((x0 + lane) & 1023, y) << (16 * lane)
                for lane in range(16))
            yield dut.port.rdata.data.eq(line)
            yield dut.port.rdata.valid.eq(1)
        else:
            yield dut.port.rdata.valid.eq(0)
        yield
        if read_address is not None and read_delay:
            read_delay -= 1
        if (yield dut.port.rdata.valid) and (yield dut.port.rdata.ready):
            read_address = None
        if (yield dut.port.wdata.valid) and (yield dut.port.wdata.ready):
            assert write_address is not None
            assert (yield dut.port.wdata.we) == 0xffffffff
            written[write_address] = yield dut.port.wdata.data
            write_address = None
        if (yield dut.port.cmd.valid) and (yield dut.port.cmd.ready):
            address = yield dut.port.cmd.addr
            if (yield dut.port.cmd.we):
                write_address = address
            else:
                read_address = address
                read_delay = rng.randrange(1, 4)


def watchdog(state):
    for _ in range(1_500_000):
        yield
        if state.get("done"):
            return
    raise AssertionError(f"display blitter timed out: {state}")


def main():
    dut = DisplayBlitter(address_width=25)
    written = {}
    state = {}
    run_simulation(dut, [
        producer(dut, state),
        memory(dut, written),
        watchdog(state),
    ])
    assert len(written) == OUTPUT_HEIGHT * (OUTPUT_WIDTH // 16)
    for y in range(OUTPUT_HEIGHT):
        source_y = (ORIGIN_Y + y * SOURCE_HEIGHT // OUTPUT_HEIGHT) & 511
        for word in range(OUTPUT_WIDTH // 16):
            line = written[FRAMEBUFFER_NATIVE_BASE +
                y * (OUTPUT_WIDTH // 16) + word]
            for lane in range(16):
                x = word * 16 + lane
                source_x = (ORIGIN_X +
                    x * SOURCE_WIDTH // OUTPUT_WIDTH) & 1023
                observed = (line >> (lane * 16)) & 0xffff
                expected = rgb565(bgr555(source_x, source_y))
                assert observed == expected, (
                    f"pixel {x},{y}: {observed:04x} != {expected:04x}")
    print(f"PASS: {OUTPUT_WIDTH}x{OUTPUT_HEIGHT} scaled frame, "
          f"{len(written)} DDR writes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
