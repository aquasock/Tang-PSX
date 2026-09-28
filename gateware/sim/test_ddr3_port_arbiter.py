#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Simulate fair DDR3 command arbitration and in-order read-response routing."""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import passive, run_simulation
from migen.fhdl.specials import Memory

# Migen's simulator cannot lower write-only memory ports used by buffered
# FIFOs; provide an unused read side for simulation only.
_get_port = Memory.get_port
def _sim_get_port(self, *args, read_capable=True, **kwargs):
    return _get_port(self, *args, read_capable=True, **kwargs)
Memory.get_port = _sim_get_port

from litex.gen import LiteXModule
from litedram.common import LiteDRAMNativePort

from ddr3_port_arbiter import DDR3PortArbiter


READS = 96


class DUT(LiteXModule):
    def __init__(self):
        self.cpu = LiteDRAMNativePort("both", address_width=10, data_width=256)
        self.video = LiteDRAMNativePort("read", address_width=10, data_width=256)
        self.target = LiteDRAMNativePort("both", address_width=10, data_width=256)
        self.arbiter = DDR3PortArbiter(self.cpu, self.video, self.target, owner_depth=16)


def read_commands(port, addresses, rng, state=None, name="reader"):
    for address in addresses:
        yield port.cmd.we.eq(0)
        yield port.cmd.addr.eq(address)
        yield port.cmd.valid.eq(1)
        yield
        while not (yield port.cmd.ready):
            yield
        yield port.cmd.valid.eq(0)
        if state is not None:
            state[f"{name}_commands"] = state.get(f"{name}_commands", 0) + 1
        for _ in range(rng.randrange(3)):
            yield



def read_responses(port, count, received, rng, state=None, name="reader"):
    while len(received) != count:
        yield port.rdata.ready.eq(rng.random() < 0.7)
        if (yield port.rdata.valid) and (yield port.rdata.ready):
            received.append((yield port.rdata.data))
            if state is not None:
                state[f"{name}_responses"] = len(received)
        yield
    yield port.rdata.ready.eq(0)


def cpu_client(dut, addresses, received, state, rng):
    port = dut.cpu
    yield port.cmd.we.eq(1)
    yield port.cmd.addr.eq(0x155)
    yield port.cmd.valid.eq(1)
    yield
    while not (yield port.cmd.ready):
        yield
    yield port.cmd.valid.eq(0)
    yield port.wdata.data.eq(0x123456789abcdef)
    yield port.wdata.we.eq(0xffffffff)
    yield port.wdata.valid.eq(1)
    yield
    while not (yield port.wdata.ready):
        yield
    yield port.wdata.valid.eq(0)
    state["write_done"] = True
    yield from read_commands(port, addresses, rng, state, "cpu")


@passive
def target(dut, accepted, state, rng):
    port = dut.target
    pending = []
    write_pending = False
    cycle = 0
    while True:
        yield port.cmd.ready.eq((not write_pending) and rng.random() < 0.8)
        yield port.wdata.ready.eq(write_pending and rng.random() < 0.8)
        if pending and pending[0][0] <= cycle:
            yield port.rdata.valid.eq(1)
            yield port.rdata.data.eq(0x10000000 | pending[0][1])
        else:
            yield port.rdata.valid.eq(0)
        yield
        cycle += 1

        if (yield port.rdata.valid) and (yield port.rdata.ready):
            pending.pop(0)
        if (yield port.cmd.valid) and (yield port.cmd.ready):
            address = yield port.cmd.addr
            if (yield port.cmd.we):
                write_pending = True
                state["write_address"] = address
            else:
                accepted.append(address)
                due = max(cycle + rng.randrange(2, 9), pending[-1][0] + 1 if pending else 0)
                pending.append((due, address))
        if write_pending and (yield port.wdata.valid) and (yield port.wdata.ready):
            state["write_data"] = yield port.wdata.data
            state["write_enables"] = yield port.wdata.we
            write_pending = False


def watchdog(state):
    for _ in range(20000):
        yield
        if state.get("done"):
            return
    raise AssertionError(f"arbiter simulation timed out: {state}")


def finish(cpu_values, video_values, cpu_addresses, video_addresses, state):
    while len(cpu_values) != len(cpu_addresses) or len(video_values) != len(video_addresses):
        yield
    state["done"] = True


def main():
    dut = DUT()
    rng = random.Random(7)
    cpu_addresses = [2*i + 1 for i in range(READS)]
    video_addresses = [2*i + 2 for i in range(READS)]
    cpu_values, video_values, accepted = [], [], []
    state = {}
    run_simulation(dut, [
        cpu_client(dut, cpu_addresses, cpu_values, state, random.Random(1)),
        read_commands(dut.video, video_addresses, random.Random(2), state, "video"),
        read_responses(dut.cpu, len(cpu_addresses), cpu_values, random.Random(3), state, "cpu"),
        read_responses(dut.video, len(video_addresses), video_values, random.Random(4), state, "video"),
        target(dut, accepted, state, rng),
        finish(cpu_values, video_values, cpu_addresses, video_addresses, state),
        watchdog(state),
    ])

    expected_cpu = [0x10000000 | address for address in cpu_addresses]
    expected_video = [0x10000000 | address for address in video_addresses]
    assert cpu_values == expected_cpu, "CPU responses were lost, reordered, or misrouted"
    assert video_values == expected_video, "video responses were lost, reordered, or misrouted"
    assert state.get("write_done")
    assert state.get("write_address") == 0x155
    assert state.get("write_data") == 0x123456789abcdef
    assert state.get("write_enables") == 0xffffffff

    # With both readers active, fairness should prevent long command runs from
    # either owner.  Odd addresses are CPU and even addresses are video.
    longest = run = 1
    for previous, current in zip(accepted, accepted[1:]):
        run = run + 1 if (previous & 1) == (current & 1) else 1
        longest = max(longest, run)
    assert longest <= 3, f"unfair command run of {longest} requests"
    print(f"PASS: {len(accepted)} reads routed, longest same-client run {longest}, CPU write preserved")
    return 0


if __name__ == "__main__":
    sys.exit(main())
