#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Simulate Gate1RAMBridge's DDR3 path: WishboneRegisterSlice then BurstWishbone2Native.

A randomized 64-bit Wishbone master issues single accesses with random byte selects,
4-beat wrapping bursts (BTE 1) starting at any lane, and linear bursts of 2-6 beats that may
cross a 256-bit native word, holding CYC between beats as the AE350's bursting AHB bridge
does. The 256-bit native port is served by an in-order memory model with random latency.
Every read is checked against a reference, and every master beat must be acknowledged once.

    python3 gateware/sim/test_ae350_ram_bridge.py
"""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram")]

from migen import passive, run_simulation

from litex.gen import LiteXModule
from litex.soc.interconnect import wishbone

from litedram.common import LiteDRAMNativePort

from ae350_ram_bridge import BurstWishbone2Native, WishboneRegisterSlice

CTI_CLASSIC, CTI_INCR, CTI_END = 0, 2, 7
WORDS = 64          # 64-bit words exercised (16 native words)
OPERATIONS = 3000


class DUT(LiteXModule):
    def __init__(self):
        self.master = wishbone.Interface(data_width=64, address_width=32, addressing="word")
        frontend = wishbone.Interface(data_width=64, address_width=32, addressing="word")
        self.port = LiteDRAMNativePort("both", address_width=27, data_width=256)
        self.slice = WishboneRegisterSlice(self.master, frontend)
        self.frontend = BurstWishbone2Native(frontend, self.port, base_address=0)


def master(dut, model, stats, rng):
    bus = dut.master

    def beat(address, write, data=0, sel=0xff, cti=CTI_CLASSIC, bte=0):
        yield bus.adr.eq(address)
        yield bus.bte.eq(bte)
        yield bus.we.eq(write)
        yield bus.dat_w.eq(data)
        yield bus.sel.eq(sel)
        yield bus.cti.eq(cti)
        yield bus.cyc.eq(1)
        yield bus.stb.eq(1)
        yield
        while not (yield bus.ack):
            if (yield bus.err):
                raise AssertionError("unexpected Wishbone error")
            yield
        value = yield bus.dat_r
        yield bus.stb.eq(0)
        stats["beats"] += 1
        return value

    def check(address, value):
        if value != model[address]:
            raise AssertionError(f"word {address}: read {value:016x}, expected {model[address]:016x}")
        stats["checked"] += 1

    for _ in range(OPERATIONS):
        kind = rng.randrange(4)
        if kind == 0:       # single write with random byte selects
            address = rng.randrange(WORDS)
            data, sel = rng.getrandbits(64), rng.randrange(1, 256)
            yield from beat(address, 1, data, sel)
            for byte in range(8):
                if sel >> byte & 1:
                    mask = 0xff << (8 * byte)
                    model[address] = (model[address] & ~mask) | (data & mask)
        elif kind == 1:     # single read
            address = rng.randrange(WORDS)
            check(address, (yield from beat(address, 0)))
        else:               # wrapping line burst or linear burst, write or read
            write = rng.randrange(2) == 1
            if kind == 2:
                line, start = rng.randrange(WORDS // 4) * 4, rng.randrange(4)
                addresses = [line + (start + i) % 4 for i in range(4)]
                bte = 1
            else:
                length = rng.randrange(2, 7)
                first = rng.randrange(WORDS - length)
                addresses = [first + i for i in range(length)]
                bte = 0
            for index, address in enumerate(addresses):
                cti = CTI_END if index == len(addresses) - 1 else CTI_INCR
                if write:
                    data = rng.getrandbits(64)
                    yield from beat(address, 1, data, 0xff, cti, bte)
                    model[address] = data
                else:
                    check(address, (yield from beat(address, 0, cti=cti, bte=bte)))
        yield bus.cyc.eq(0)
        for _ in range(rng.randrange(3)):
            yield
    stats["done"] = True


@passive
def memory(dut, rng):
    """In-order native-port memory with random command acceptance and read latency."""
    port = dut.port
    storage = {}
    pending = []   # (due cycle, data)
    write_address = None
    cycle = 0
    while True:
        yield port.cmd.ready.eq(write_address is None and rng.random() < 0.7)
        yield port.wdata.ready.eq(write_address is not None and rng.random() < 0.7)
        rdata_ready = yield port.rdata.ready
        if pending and pending[0][0] <= cycle:
            yield port.rdata.valid.eq(1)
            yield port.rdata.data.eq(pending[0][1])
        else:
            yield port.rdata.valid.eq(0)
        yield
        cycle += 1
        if (yield port.rdata.valid) and rdata_ready:
            pending.pop(0)
        if (yield port.cmd.valid) and (yield port.cmd.ready):
            address = yield port.cmd.addr
            if (yield port.cmd.we):
                write_address = address
            else:
                due = max([cycle + rng.randrange(2, 12)] + [p[0] + 1 for p in pending])
                pending.append((due, storage.get(address, 0)))
        if write_address is not None and (yield port.wdata.valid) and (yield port.wdata.ready):
            data, enables = (yield port.wdata.data), (yield port.wdata.we)
            word = storage.get(write_address, 0)
            for byte in range(32):
                if enables >> byte & 1:
                    mask = 0xff << (8 * byte)
                    word = (word & ~mask) | (data & mask)
            storage[write_address] = word
            write_address = None


def main():
    rng = random.Random(1)
    dut = DUT()
    model = [0] * WORDS
    stats = {"beats": 0, "checked": 0, "done": False}
    run_simulation(dut, [master(dut, model, stats, rng), memory(dut, rng)])
    if not stats["done"]:
        print("FAIL: master did not finish")
        return 1
    print(f"PASS: {stats['beats']} beats, {stats['checked']} reads checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
