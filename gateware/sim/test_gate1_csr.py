#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Simulate Gate 1's timing-pipelined Wishbone-to-CSR read/write path."""

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "gateware")] + [str(ROOT / "third_party" / p)
    for p in ("migen", "litex", "litedram", "litex-boards")]

from migen import run_simulation
from litex.gen import LiteXModule
from litex.soc.interconnect import csr_bus, wishbone
from litex.soc.interconnect.csr import CSRStatus, CSRStorage

from ae350_gate1 import Gate1CSRBank, Gate1Wishbone2CSR


STATUSES = 20


def status_value(n):
    return (0x51aacc33 ^ (n * 0x01010101)) & 0xffffffff


class DUT(LiteXModule):
    def __init__(self):
        self.wb = wishbone.Interface(data_width=32, address_width=32,
            addressing="word")
        self.csr = csr_bus.Interface(data_width=32, address_width=14,
            alignment=32)
        self.bridge = Gate1Wishbone2CSR(self.wb, self.csr, register=True)
        self.control = CSRStorage(32, name="control")
        # Twenty status registers span three read-mux groups, the last one
        # partial, so every group and lane select is exercised.
        self.statuses = [CSRStatus(32, name=f"status{n}")
            for n in range(STATUSES)]
        self.bank_bus = csr_bus.Interface(data_width=32, address_width=14,
            alignment=32)
        self.bank = Gate1CSRBank([self.control] + self.statuses, address=3,
            bus=self.bank_bus, paging=0x800)
        self.interconnect = csr_bus.Interconnect(self.csr, [self.bank_bus])
        self.comb += [status.status.eq(status_value(n))
            for n, status in enumerate(self.statuses)]


def access(dut, address, write=False, value=0):
    yield dut.wb.adr.eq(address)
    yield dut.wb.dat_w.eq(value)
    yield dut.wb.sel.eq(0xf)
    yield dut.wb.we.eq(write)
    yield dut.wb.cyc.eq(1)
    yield dut.wb.stb.eq(1)
    cycles = 0
    yield
    while not (yield dut.wb.ack):
        cycles += 1
        assert cycles < 12
        yield
    result = yield dut.wb.dat_r
    yield dut.wb.cyc.eq(0)
    yield dut.wb.stb.eq(0)
    yield dut.wb.we.eq(0)
    yield
    while (yield dut.wb.ack):
        yield
    return result, cycles


def test(dut):
    base = 3 * (0x800 // 4)
    _, write_cycles = yield from access(dut, base, True, 0x1234fedc)
    assert (yield dut.control.storage) == 0x1234fedc
    assert write_cycles == 4
    order = list(range(STATUSES))
    random.Random(5).shuffle(order)
    for n in order:
        value, read_cycles = yield from access(dut, base + 1 + n)
        assert value == status_value(n), \
            f"status{n} read {value:08x}, expected {status_value(n):08x}"
        assert read_cycles == 4, f"status{n} read took {read_cycles} cycles"
    value, _ = yield from access(dut, base + 1 + STATUSES)
    assert value == 0, f"unmapped register read {value:08x}"


def main():
    dut = DUT()
    run_simulation(dut, test(dut))
    print(f"PASS: pipelined Gate 1 CSR write and {STATUSES} reads completed in four cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
