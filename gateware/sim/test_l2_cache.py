#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Randomized check of gateware/l2_cache.py against a reference memory.

The upstream driver issues one command at a time as BurstWishbone2Native
does: reads, and writes of 256-bit words with random byte enables whose data
follows the command. Addresses cover lines that share cache indexes, and a
bypass range. The enable input toggles at random. The downstream port is an
in-order memory with random command, write and read delays, and an external
writer (standing for the fabric GPU) changes bypass-range words in memory
behind the cache. Every read must
return the reference data, every downstream write must match the upstream
one, and hits, misses and bypass reads must account for every read.

    python3 gateware/sim/test_l2_cache.py
"""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import Module, passive, run_simulation

from litedram.common import LiteDRAMNativePort

from l2_cache import L2Cache

LINES = 16
ADDRESS_WIDTH = 10
BYPASS_BASE, BYPASS_END = 0x300, 0x304
OPERATIONS = 4000


class DUT(Module):
    def __init__(self):
        self.upstream = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256)
        self.downstream = LiteDRAMNativePort("both", ADDRESS_WIDTH, 256)
        self.submodules.l2 = L2Cache(self.upstream, self.downstream, LINES,
            BYPASS_BASE, BYPASS_END)


def addresses(rng):
    # Few tags per index, so lines conflict and hit; plus the bypass range.
    if rng.random() < 0.15:
        return rng.randrange(BYPASS_BASE, BYPASS_END)
    return rng.randrange(6) * LINES + rng.randrange(LINES)


def driver(dut, reference, backing, stats, rng):
    up = dut.upstream
    for _ in range(OPERATIONS):
        if rng.random() < 0.1:
            # The GPU writes VRAM straight to DDR3.
            address = rng.randrange(BYPASS_BASE, BYPASS_END)
            reference[address] = backing[address] = rng.getrandbits(256)
        if rng.random() < 0.05:
            yield dut.l2.enable.eq(rng.random() < 0.7)
        address = addresses(rng)
        write = rng.random() < 0.4
        yield up.cmd.valid.eq(1)
        yield up.cmd.we.eq(write)
        yield up.cmd.addr.eq(address)
        yield
        while not (yield up.cmd.ready):
            yield
        yield up.cmd.valid.eq(0)
        if write:
            data = rng.getrandbits(256)
            we = rng.getrandbits(32) or 1
            for _ in range(rng.randrange(3)):
                yield
            yield up.wdata.valid.eq(1)
            yield up.wdata.data.eq(data)
            yield up.wdata.we.eq(we)
            yield
            while not (yield up.wdata.ready):
                yield
            yield up.wdata.valid.eq(0)
            old = reference.get(address, 0)
            mask = sum(0xff << (8 * b) for b in range(32) if we >> b & 1)
            reference[address] = (old & ~mask) | (data & mask)
            stats["writes"] += 1
        else:
            yield up.rdata.ready.eq(1)
            yield
            while not (yield up.rdata.valid):
                yield
            value = yield up.rdata.data
            yield up.rdata.ready.eq(0)
            expected = reference.get(address, 0)
            if value != expected:
                raise AssertionError(f"read {address:#x}: {value:#x} != {expected:#x}")
            stats["reads"] += 1
            if address >= BYPASS_BASE and address < BYPASS_END:
                stats["bypass"] += 1
    for _ in range(4):
        yield
    stats["hits"] = yield dut.l2.read_hits
    stats["misses"] = yield dut.l2.read_misses
    stats["bypass_counted"] = yield dut.l2.bypass_reads
    stats["writes_counted"] = yield dut.l2.writes


@passive
def memory(dut, backing, rng):
    down = dut.downstream
    while True:
        yield down.cmd.ready.eq(0)
        while not (yield down.cmd.valid):
            yield
        for _ in range(rng.randrange(3)):
            yield
        yield down.cmd.ready.eq(1)
        yield
        yield down.cmd.ready.eq(0)
        address = yield down.cmd.addr
        write = yield down.cmd.we
        if write:
            while not (yield down.wdata.valid):
                yield
            for _ in range(rng.randrange(3)):
                yield
            data = yield down.wdata.data
            we = yield down.wdata.we
            # Commit as the write is accepted, before the driver continues.
            old = backing.get(address, 0)
            mask = sum(0xff << (8 * b) for b in range(32) if we >> b & 1)
            backing[address] = (old & ~mask) | (data & mask)
            yield down.wdata.ready.eq(1)
            yield
            yield down.wdata.ready.eq(0)
        else:
            for _ in range(rng.randrange(1, 12)):
                yield
            yield down.rdata.valid.eq(1)
            yield down.rdata.data.eq(backing.get(address, 0))
            yield
            while not (yield down.rdata.ready):
                yield
            yield down.rdata.valid.eq(0)


def main():
    rng = random.Random(0x12c)
    reference, backing = {}, {}
    stats = {"reads": 0, "writes": 0, "bypass": 0}
    dut = DUT()
    run_simulation(dut, [driver(dut, reference, backing, stats, rng),
        memory(dut, backing, rng)])
    if backing != {k: v for k, v in reference.items()}:
        raise AssertionError("downstream memory differs from the reference")
    accounted = stats["hits"] + stats["misses"] + stats["bypass_counted"]
    if accounted != stats["reads"] or stats["writes_counted"] != stats["writes"]:
        raise AssertionError(f"counters do not account for every access: {stats}")
    if stats["hits"] == 0 or stats["misses"] == 0:
        raise AssertionError(f"hits and misses must both occur: {stats}")
    print(f"PASS: {stats['reads']} reads ({stats['hits']} hits, {stats['misses']} misses, "
          f"{stats['bypass_counted']} bypassed), {stats['writes']} writes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
