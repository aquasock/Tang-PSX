#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Exercise descriptor batches fetched from DDR into the GPU word stream."""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import passive, run_simulation

from gpu_accel import GPUCommandDMA, MAIN_RAM_BASE


BATCHES = (37, 19)


def producer(dut, state):
    base = MAIN_RAM_BASE + 0x2000
    for batch, words in enumerate(BATCHES):
        while not (yield dut.ready):
            yield
        yield dut.base.eq(base + batch * 0x100)
        yield dut.words.eq(words)
        yield dut.start.eq(1)
        yield
        yield dut.start.eq(0)
        while (yield dut.ready):
            yield
        while not (yield dut.ready):
            yield
    state["producer_done"] = True


def consumer(dut, received, state):
    rng = random.Random(13)
    expected = sum(BATCHES)
    while len(received) != expected:
        yield dut.source.ready.eq(rng.random() < 0.7)
        if (yield dut.source.valid) and (yield dut.source.ready):
            received.append((yield dut.source.data))
        yield
    yield dut.source.ready.eq(0)
    state["consumer_done"] = True


@passive
def memory(dut):
    rng = random.Random(29)
    pending = None
    delay = 0
    while True:
        yield dut.port.cmd.ready.eq(pending is None and rng.random() < 0.8)
        if pending is not None and delay == 0:
            line = 0
            byte_address = MAIN_RAM_BASE + pending * 32
            batch = (byte_address - (MAIN_RAM_BASE + 0x2000)) // 0x100
            first_word = ((byte_address - (MAIN_RAM_BASE + 0x2000 +
                batch * 0x100)) // 4)
            for lane in range(8):
                value = (batch << 24) | (first_word + lane)
                line |= value << (lane * 32)
            yield dut.port.rdata.data.eq(line)
            yield dut.port.rdata.valid.eq(1)
        else:
            yield dut.port.rdata.valid.eq(0)
        yield
        if pending is not None and delay:
            delay -= 1
        if (yield dut.port.rdata.valid) and (yield dut.port.rdata.ready):
            pending = None
        if (yield dut.port.cmd.valid) and (yield dut.port.cmd.ready):
            assert not (yield dut.port.cmd.we)
            pending = yield dut.port.cmd.addr
            delay = rng.randrange(1, 5)


def watchdog(state):
    for _ in range(5000):
        yield
        if state.get("producer_done") and state.get("consumer_done"):
            return
    raise AssertionError(f"GPU command DMA timed out: {state}")


def main():
    dut = GPUCommandDMA(address_width=25)
    received = []
    state = {}
    run_simulation(dut, [
        producer(dut, state),
        consumer(dut, received, state),
        memory(dut),
        watchdog(state),
    ])
    expected = [
        (batch << 24) | word
        for batch, words in enumerate(BATCHES)
        for word in range(words)
    ]
    assert received == expected
    print(f"PASS: {len(received)} descriptor words fetched in two DDR batches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
