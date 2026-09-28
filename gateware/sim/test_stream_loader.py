#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Simulate StreamLoader across its stream and system clock domains.

The stream side imitates iosys_bl616: single-cycle start/end/cancel strobes and bytes held with
valid until ready, with end sent only after the last byte is taken. Sessions have random
lengths (including lengths that are not multiples of four) and some are cancelled part-way.
A 4-entry FIFO and a randomly stalling consumer exercise backpressure. The consumer must see
START, the payload as little-endian words (a final partial word zero-padded), then END with the
byte count, or CANCEL with the partial word discarded.

    python3 gateware/sim/test_stream_loader.py
"""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import run_simulation
from migen.fhdl.specials import Memory

# Migen's simulator cannot lower write-only memory ports (used by the clock-crossing FIFO);
# give them an unused read side for simulation only.
_get_port = Memory.get_port
def _sim_get_port(self, *args, read_capable=True, **kwargs):
    return _get_port(self, *args, read_capable=True, **kwargs)
Memory.get_port = _sim_get_port

from stream_loader import StreamLoader, TAG_CANCEL, TAG_DATA, TAG_END, TAG_START

SESSIONS = 60


def expected_entries(payload, cancelled):
    entries = [(TAG_START, 0)]
    whole = len(payload) // 4 * 4
    for i in range(0, whole, 4):
        entries.append((TAG_DATA, int.from_bytes(payload[i:i + 4], "little")))
    if cancelled:
        entries.append((TAG_CANCEL, 0))
    else:
        if whole != len(payload):
            entries.append((TAG_DATA, int.from_bytes(payload[whole:].ljust(4, b"\0"), "little")))
        entries.append((TAG_END, len(payload)))
    return entries


def producer(dut, sessions, state, rng):
    for payload, cancel_at in sessions:
        yield dut.start.eq(1)
        yield
        yield dut.start.eq(0)
        for _ in range(rng.randrange(3)):
            yield
        sent = payload if cancel_at is None else payload[:cancel_at]
        for byte in sent:
            yield dut.data.eq(byte)
            yield dut.valid.eq(1)
            yield
            while not (yield dut.ready):
                yield
            yield dut.valid.eq(0)
            if rng.random() < 0.2:
                yield
        for _ in range(rng.randrange(3)):
            yield
        if cancel_at is None:
            yield dut.end.eq(1)
        else:
            yield dut.cancel.eq(1)
        yield
        yield dut.end.eq(0)
        yield dut.cancel.eq(0)
        # Tang-Control may open the next session before the FPGA has queued this END.
        for _ in range(rng.choice([0, 0, 1, 2, 10])):
            yield
    state["produced"] = True


def consumer(dut, received, state, rng):
    source = dut.cdc.source
    idle = 0
    while True:
        valid = yield source.valid
        if valid and rng.random() < 0.4:
            received.append(((yield source.tag), (yield source.data)))
            # Hold the pop strobe for one cycle, then wait for the head to advance before
            # sampling again, as firmware does with a CSR read followed by a CSR write.
            yield dut._pop.re.eq(1)
            yield
            yield dut._pop.re.eq(0)
            yield
        if rng.random() < 0.05:
            for _ in range(rng.randrange(20, 60)):
                yield
        yield
        idle = 0 if valid else idle + 1
        if state.get("produced") and idle > 200:
            return


def main():
    rng = random.Random(3)
    sessions = []
    for _ in range(SESSIONS):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 40)))
        cancel_at = rng.randrange(len(payload) + 1) if payload and rng.random() < 0.2 else None
        sessions.append((payload, cancel_at))

    dut = StreamLoader(stream_domain="diag", depth=4)
    received, state = [], {}
    run_simulation(dut,
        {"diag": [producer(dut, sessions, state, rng)], "sys": [consumer(dut, received, state, rng)]},
        clocks={"diag": 13, "sys": 10})

    expected = []
    for payload, cancel_at in sessions:
        expected += expected_entries(payload if cancel_at is None else payload[:cancel_at],
                                     cancel_at is not None)
    if received != expected:
        for index, (got, want) in enumerate(zip(received, expected)):
            if got != want:
                print(f"FAIL: entry {index}: got {got}, expected {want}")
                break
        else:
            print(f"FAIL: received {len(received)} entries, expected {len(expected)}")
        return 1
    print(f"PASS: {SESSIONS} sessions, {len(received)} entries in order")
    return 0


if __name__ == "__main__":
    sys.exit(main())
