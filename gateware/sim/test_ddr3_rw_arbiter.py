#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Randomized simulation of the CPU/GPU read-write DDR3 arbiter."""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import passive, run_simulation
from litex.gen import LiteXModule
from litedram.common import LiteDRAMNativePort

from ddr3_port_arbiter import DDR3RWArbiter


TRANSACTIONS = 64
MASK = (1 << 256) - 1


class DUT(LiteXModule):
    def __init__(self):
        self.first = LiteDRAMNativePort("both", address_width=10, data_width=256)
        self.second = LiteDRAMNativePort("both", address_width=10, data_width=256)
        self.target = LiteDRAMNativePort("both", address_width=10, data_width=256)
        self.arbiter = DDR3RWArbiter(self.first, self.second, self.target)


def client(port, addresses, seed, state, name):
    rng = random.Random(seed)
    for sequence, address in enumerate(addresses):
        value = ((seed << 240) | (sequence << 224) |
            (address * 0x0101010101010101)) & MASK

        yield port.cmd.we.eq(1)
        yield port.cmd.addr.eq(address)
        yield port.cmd.valid.eq(1)
        yield
        while not (yield port.cmd.ready):
            yield
        yield port.cmd.valid.eq(0)

        yield port.wdata.data.eq(value)
        yield port.wdata.we.eq(0xffffffff)
        yield port.wdata.valid.eq(1)
        yield
        while not (yield port.wdata.ready):
            yield
        yield port.wdata.valid.eq(0)

        for _ in range(rng.randrange(3)):
            yield

        yield port.cmd.we.eq(0)
        yield port.cmd.addr.eq(address)
        yield port.cmd.valid.eq(1)
        yield
        while not (yield port.cmd.ready):
            yield
        yield port.cmd.valid.eq(0)

        yield port.rdata.ready.eq(rng.random() < 0.5)
        while True:
            if not (yield port.rdata.ready):
                yield port.rdata.ready.eq(rng.random() < 0.75)
            yield
            if (yield port.rdata.valid) and (yield port.rdata.ready):
                received = yield port.rdata.data
                assert received == value, (
                    f"{name} read {address:#x}: {received:#x} != {value:#x}")
                break
        yield port.rdata.ready.eq(0)
        state[name] = sequence + 1
    state[name + "_done"] = True


@passive
def target(dut, seed, state):
    rng = random.Random(seed)
    port = dut.target
    memory = {}
    write_address = None
    read_address = None
    read_delay = 0
    while True:
        yield port.cmd.ready.eq(write_address is None and read_address is None and
            rng.random() < 0.8)
        yield port.wdata.ready.eq(write_address is not None and rng.random() < 0.7)
        if read_address is not None and read_delay == 0:
            yield port.rdata.valid.eq(1)
            yield port.rdata.data.eq(memory[read_address])
        else:
            yield port.rdata.valid.eq(0)
        yield

        if read_address is not None and read_delay:
            read_delay -= 1
        if (yield port.rdata.valid) and (yield port.rdata.ready):
            read_address = None
        if (yield port.cmd.valid) and (yield port.cmd.ready):
            address = yield port.cmd.addr
            if (yield port.cmd.we):
                assert write_address is None and read_address is None
                write_address = address
            else:
                assert write_address is None and read_address is None
                assert address in memory
                read_address = address
                read_delay = rng.randrange(1, 6)
        if (yield port.wdata.valid) and (yield port.wdata.ready):
            assert write_address is not None
            assert (yield port.wdata.we) == 0xffffffff
            memory[write_address] = yield port.wdata.data
            write_address = None
            state["writes"] = state.get("writes", 0) + 1


def finish(state):
    while not state.get("first_done") or not state.get("second_done"):
        yield
    state["done"] = True


def watchdog(state):
    for _ in range(50000):
        yield
        if state.get("done"):
            return
    raise AssertionError(f"read-write arbiter timed out: {state}")


def main():
    dut = DUT()
    state = {}
    first_addresses = [2 * n + 1 for n in range(TRANSACTIONS)]
    second_addresses = [2 * n + 2 for n in range(TRANSACTIONS)]
    run_simulation(dut, [
        client(dut.first, first_addresses, 11, state, "first"),
        client(dut.second, second_addresses, 29, state, "second"),
        target(dut, 47, state),
        finish(state),
        watchdog(state),
    ])
    assert state.get("first") == TRANSACTIONS
    assert state.get("second") == TRANSACTIONS
    assert state.get("writes") == 2 * TRANSACTIONS
    print(f"PASS: {2 * TRANSACTIONS} writes and reads preserved across both clients")
    return 0


if __name__ == "__main__":
    sys.exit(main())
