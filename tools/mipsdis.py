#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Minimal MIPS I disassembler for PlayStation RAM dumps.

  mipsdis.py DUMP ADDRESS [COUNT]

DUMP is a little-endian image of main RAM starting at physical address 0
(psx_disc_host writes one with PSX_RAM_DUMP). ADDRESS may be a KUSEG, KSEG0
or KSEG1 address; COUNT instructions are listed from there (default 32).
"""

import struct
import sys

REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
SPECIAL = {0x00: "sll", 0x02: "srl", 0x03: "sra", 0x04: "sllv", 0x06: "srlv",
           0x07: "srav", 0x08: "jr", 0x09: "jalr", 0x0c: "syscall", 0x0d: "break",
           0x10: "mfhi", 0x11: "mthi", 0x12: "mflo", 0x13: "mtlo", 0x18: "mult",
           0x19: "multu", 0x1a: "div", 0x1b: "divu", 0x20: "add", 0x21: "addu",
           0x22: "sub", 0x23: "subu", 0x24: "and", 0x25: "or", 0x26: "xor",
           0x27: "nor", 0x2a: "slt", 0x2b: "sltu"}
IMMEDIATE = {0x08: "addi", 0x09: "addiu", 0x0a: "slti", 0x0b: "sltiu",
             0x0c: "andi", 0x0d: "ori", 0x0e: "xori"}
MEMORY = {0x20: "lb", 0x21: "lh", 0x22: "lwl", 0x23: "lw", 0x24: "lbu",
          0x25: "lhu", 0x26: "lwr", 0x28: "sb", 0x29: "sh", 0x2a: "swl",
          0x2b: "sw", 0x2e: "swr", 0x32: "lwc2", 0x3a: "swc2"}
BRANCH = {0x04: "beq", 0x05: "bne", 0x06: "blez", 0x07: "bgtz"}


def disassemble(word, pc):
    op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
    rd, sa, fn = (word >> 11) & 31, (word >> 6) & 31, word & 63
    imm = word & 0xffff
    simm = imm - 0x10000 if imm & 0x8000 else imm
    target = (pc + 4 + (simm << 2)) & 0xffffffff
    if word == 0:
        return "nop"
    if op == 0:
        name = SPECIAL.get(fn, f"special.{fn:02x}")
        if fn in (0, 2, 3):
            return f"{name} {REGS[rd]}, {REGS[rt]}, {sa}"
        if fn == 8:
            return f"jr {REGS[rs]}"
        if fn == 9:
            return f"jalr {REGS[rd]}, {REGS[rs]}"
        if fn in (0x10, 0x12):
            return f"{name} {REGS[rd]}"
        if fn in (0x11, 0x13):
            return f"{name} {REGS[rs]}"
        if fn in (0x18, 0x19, 0x1a, 0x1b):
            return f"{name} {REGS[rs]}, {REGS[rt]}"
        if fn in (0x0c, 0x0d):
            return name
        return f"{name} {REGS[rd]}, {REGS[rs]}, {REGS[rt]}"
    if op == 1:
        name = {0: "bltz", 1: "bgez", 16: "bltzal", 17: "bgezal"}.get(rt, "regimm")
        return f"{name} {REGS[rs]}, 0x{target:08x}"
    if op in (2, 3):
        return f"{'j' if op == 2 else 'jal'} 0x{((pc + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2):08x}"
    if op in BRANCH:
        if op in (4, 5):
            return f"{BRANCH[op]} {REGS[rs]}, {REGS[rt]}, 0x{target:08x}"
        return f"{BRANCH[op]} {REGS[rs]}, 0x{target:08x}"
    if op in IMMEDIATE:
        value = imm if op >= 0x0c else simm
        return f"{IMMEDIATE[op]} {REGS[rt]}, {REGS[rs]}, {value:#x}"
    if op == 0x0f:
        return f"lui {REGS[rt]}, {imm:#06x}"
    if op in MEMORY:
        return f"{MEMORY[op]} {REGS[rt]}, {simm}({REGS[rs]})"
    if op == 0x10:
        return {0: f"mfc0 {REGS[rt]}, ${rd}", 4: f"mtc0 {REGS[rt]}, ${rd}",
                16: "rfe"}.get(rs, f"cop0 {word:08x}")
    if op == 0x12:
        return {0: f"mfc2 {REGS[rt]}, ${rd}", 2: f"cfc2 {REGS[rt]}, ${rd}",
                4: f"mtc2 {REGS[rt]}, ${rd}", 6: f"ctc2 {REGS[rt]}, ${rd}"}.get(
                    rs, f"cop2 {word & 0x1ffffff:07x}")
    return f".word {word:08x}"


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    start = int(sys.argv[2], 0)
    count = int(sys.argv[3], 0) if len(sys.argv) > 3 else 32
    for n in range(count):
        pc = start + 4 * n
        offset = pc & 0x1fffff
        if offset + 4 > len(data):
            break
        word = struct.unpack_from("<I", data, offset)[0]
        print(f"{pc:08x}: {word:08x}  {disassemble(word, pc)}")


if __name__ == "__main__":
    main()
