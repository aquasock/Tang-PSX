#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate deterministic R3000A/GTE reference vectors for psx_diag.

The target interpreter never runs this model.  Python computes the expected
values on the development PC and emits a C header consumed by both the native
regression and the RV32 loader image.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


def r(rs: int, rt: int, rd: int, sa: int, fn: int) -> int:
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn


def i(op: int, rs: int, rt: int, imm: int) -> int:
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def j(op: int, target: int) -> int:
    return (op << 26) | ((target >> 2) & 0x03FFFFFF)


def cop(op: int, rs: int, rt: int, rd: int, low: int = 0) -> int:
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | low


def pack_xy(x: int, y: int) -> int:
    return (x & 0xFFFF) | ((y & 0xFFFF) << 16)


def pack_matrix(values: list[int]) -> dict[int, int]:
    result: dict[int, int] = {}
    for n, value in enumerate(values):
        reg = n // 2
        result[reg] = result.get(reg, 0) | ((value & 0xFFFF) << (16 * (n & 1)))
    return result


def leading_zeros_16(value: int) -> int:
    return 16 if value == 0 else 16 - value.bit_length()


def gte_divide(numerator: int, denominator: int) -> tuple[int, int]:
    if numerator >= denominator * 2:
        return 0x1FFFF, 0x80020000
    shift = leading_zeros_16(denominator)
    r1 = (denominator << shift) & 0x7FFF
    index = (r1 + 0x40) >> 7
    unr = max(0, ((0x40000 // (index + 0x100)) + 1) // 2 - 0x101)
    r2 = unr + 0x101
    r3 = ((0x80 - r2 * (r1 + 0x8000)) >> 8) & 0x1FFFF
    reciprocal = (r2 * r3 + 0x80) >> 8
    result = (reciprocal * (numerator << shift) + 0x8000) >> 16
    return min(0x1FFFF, result), 0


CPU_VECTORS = [
    {
        "name": "integer_muldiv",
        "program": [
            i(0x09, 0, 1, -7), i(0x09, 0, 2, 3), r(1, 2, 3, 0, 0x21),
            r(2, 1, 4, 0, 0x23), r(0, 2, 5, 4, 0x00),
            r(0, 1, 6, 2, 0x03), r(1, 2, 7, 0, 0x2A),
            r(1, 2, 0, 0, 0x18), r(0, 0, 8, 0, 0x12),
            r(0, 0, 9, 0, 0x10), r(1, 2, 0, 0, 0x1A),
            r(0, 0, 10, 0, 0x12), r(0, 0, 11, 0, 0x10),
        ],
        "checks": [(3, 0xFFFFFFFC), (4, 10), (5, 48), (6, 0xFFFFFFFE),
                   (7, 1), (8, 0xFFFFFFEB), (9, 0xFFFFFFFF),
                   (10, 0xFFFFFFFE), (11, 0xFFFFFFFF)],
    },
    {
        "name": "logic_shift_unsigned",
        "program": [
            i(0x0F, 0, 1, 0xA5A5), i(0x0D, 1, 1, 0x5A5A),
            i(0x0F, 0, 2, 0x0F0F), i(0x0D, 2, 2, 0xF0F0),
            r(1, 2, 3, 0, 0x24), r(1, 2, 4, 0, 0x25),
            r(1, 2, 5, 0, 0x26), r(1, 2, 6, 0, 0x27),
            i(0x0C, 1, 7, 0x00FF), i(0x0E, 2, 8, 0xFFFF),
            i(0x0A, 1, 9, -1), i(0x0B, 1, 10, -1),
            i(0x09, 0, 14, 4), r(14, 2, 11, 0, 0x04),
            r(14, 1, 12, 0, 0x06), r(14, 1, 13, 0, 0x07),
            r(1, 2, 0, 0, 0x19), r(0, 0, 15, 0, 0x12),
            r(0, 0, 16, 0, 0x10), r(1, 2, 0, 0, 0x1B),
            r(0, 0, 17, 0, 0x12), r(0, 0, 18, 0, 0x10),
        ],
        "checks": [(3, 0x05055050), (4, 0xAFAFFAFA), (5, 0xAAAAAAAA),
                   (6, 0x50500505), (7, 0x5A), (8, 0x0F0F0F0F),
                   (9, 1), (10, 1), (11, 0xF0FF0F00),
                   (12, 0x0A5A55A5), (13, 0xFA5A55A5),
                   (15, 0xE0FF1460), (16, 0x09BF00E1),
                   (17, 10), (18, 0x0F05F0FA)],
    },
    {
        "name": "memory_width_unaligned",
        "program": [
            i(0x09, 0, 8, 0x200), i(0x0F, 0, 1, 0x80FF),
            i(0x0D, 1, 1, 0x7F01), i(0x2B, 8, 1, 0),
            i(0x20, 8, 2, 0), 0, i(0x20, 8, 3, 1), 0,
            i(0x20, 8, 4, 2), 0, i(0x20, 8, 5, 3), 0,
            i(0x24, 8, 6, 2), 0, i(0x21, 8, 7, 0), 0,
            i(0x21, 8, 9, 2), 0, i(0x25, 8, 10, 2), 0,
            i(0x09, 0, 11, 0x55), i(0x28, 8, 11, 1),
            i(0x0D, 0, 12, 0x1234), i(0x29, 8, 12, 2),
            i(0x0F, 0, 13, 0x4433), i(0x0D, 13, 13, 0x2211),
            i(0x2B, 8, 13, 4), i(0x0F, 0, 20, 0xAABB),
            i(0x0D, 20, 20, 0xCCDD), i(0x22, 8, 20, 5), 0,
            r(20, 0, 21, 0, 0x21), i(0x26, 8, 20, 6), 0,
            i(0x0F, 0, 14, 0xDEAD), i(0x0D, 14, 14, 0xBEEF),
            i(0x2B, 8, 14, 8), i(0x2A, 8, 13, 9),
            i(0x2E, 8, 13, 10),
        ],
        "checks": [(2, 1), (3, 127), (4, 0xFFFFFFFF),
                   (5, 0xFFFFFF80), (6, 255), (7, 0x7F01),
                   (9, 0xFFFF80FF), (10, 0x80FF),
                   (20, 0x22114433), (21, 0x2211CCDD),
                   (0x1000 + 0x200 // 4, 0x12345501),
                   (0x1000 + 0x208 // 4, 0x22114433)],
    },
    {
        "name": "branch_delay",
        "program": [
            i(0x09, 0, 1, 1), i(0x04, 1, 1, 2), i(0x09, 0, 2, 0x22),
            i(0x09, 0, 2, 0x33), j(0x03, 28), i(0x09, 0, 3, 0x44),
            i(0x09, 0, 3, 0x55), i(0x09, 31, 4, 0),
            i(0x05, 1, 1, 1), i(0x09, 0, 5, 0x66),
        ],
        "steps": 8,
        "checks": [(2, 0x22), (3, 0x44), (4, 24), (5, 0x66), (31, 24),
                   (0x40, 40), (0x41, 44)],
    },
    {
        "name": "load_delay",
        "program": [
            i(0x09, 0, 8, 0x100), i(0x0F, 0, 1, 0x1122),
            i(0x0D, 1, 1, 0x3344), i(0x2B, 8, 1, 0),
            i(0x09, 0, 2, 7), i(0x23, 8, 2, 0), i(0x09, 2, 3, 1),
            i(0x09, 2, 4, 1), i(0x23, 8, 5, 0), i(0x09, 0, 5, 9),
            0,
        ],
        "checks": [(2, 0x11223344), (3, 8), (4, 0x11223345), (5, 9),
                   (0x1000 + 0x100 // 4, 0x11223344)],
    },
    {
        "name": "delay_slot_exception",
        "program": [i(0x09, 0, 1, 1), i(0x04, 1, 1, 1), i(0x23, 0, 2, 1)],
        "checks": [(0x40, 0x80000080), (0x4D, 0x80000010),
                   (0x4E, 4), (0x48, 1), (0x42, 1)],
    },
    {
        "name": "overflow_exception",
        "program": [i(0x0F, 0, 1, 0x7FFF), i(0x0D, 1, 1, 0xFFFF),
                    i(0x08, 1, 2, 1)],
        "checks": [(1, 0x7FFFFFFF), (2, 0), (0x40, 0x80000080),
                   (0x4D, 0x30), (0x4E, 8), (0x42, 1)],
    },
    {
        "name": "cop0_load_delay",
        "program": [i(0x09, 0, 1, 12), cop(0x10, 4, 1, 12),
                    cop(0x10, 0, 2, 12), i(0x09, 2, 3, 1),
                    i(0x09, 2, 4, 1), 0x42000010],
        "checks": [(2, 12), (3, 1), (4, 13), (0x4C, 3)],
    },
    {
        "name": "cop2_nclip",
        "program": [
            i(0x0F, 0, 1, 20), i(0x0D, 1, 1, 10), cop(0x12, 4, 1, 12),
            i(0x0F, 0, 1, 25), i(0x0D, 1, 1, 100), cop(0x12, 4, 1, 13),
            i(0x0F, 0, 1, 90), i(0x0D, 1, 1, 40), cop(0x12, 4, 1, 14),
            0x4A000006, cop(0x12, 0, 2, 24), 0, 0,
        ],
        "checks": [(2, 6150), (0x98, 6150)],
    },
]


def make_gte_vectors() -> list[dict]:
    identity = pack_matrix([4096, 0, 0, 0, 4096, 0, 0, 0, 4096])
    vectors = []

    control = dict(identity)
    control.update({5: 1000, 6: 2000, 7: (-3000) & 0xFFFFFFFF})
    vectors.append({
        "name": "mvmva_identity", "data": {0: pack_xy(100, -200), 1: 300},
        "control": control, "command": 0x00080012,
        "checks": [(0x89, 1100), (0x8A, 1800), (0x8B, (-2700) & 0xFFFFFFFF),
                   (0x99, 1100), (0x9A, 1800), (0x9B, (-2700) & 0xFFFFFFFF),
                   (0xBF, 0)],
    })
    vectors.append({
        "name": "nclip", "data": {12: pack_xy(10, 20),
                                      13: pack_xy(100, 25),
                                      14: pack_xy(40, 90)},
        "control": {}, "command": 0x00000006,
        "checks": [(0x98, 6150), (0xBF, 0)],
    })
    avsz3_mac = 0x555 * (100 + 200 + 300)
    vectors.append({
        "name": "avsz3", "data": {17: 100, 18: 200, 19: 300},
        "control": {29: 0x555}, "command": 0x0000002D,
        "checks": [(0x98, avsz3_mac), (0x87, avsz3_mac >> 12), (0xBF, 0)],
    })
    avsz4_mac = 0x400 * (100 + 200 + 300 + 400)
    vectors.append({
        "name": "avsz4", "data": {16: 100, 17: 200, 18: 300, 19: 400},
        "control": {30: 0x400}, "command": 0x0000002E,
        "checks": [(0x98, avsz4_mac), (0x87, avsz4_mac >> 12), (0xBF, 0)],
    })

    control = dict(identity)
    control.update({7: 1000, 24: 320 << 16, 25: 240 << 16,
                    26: 256, 27: 32, 28: 0})
    quotient, flag = gte_divide(256, 2000)
    sx = (320 << 16) + 100 * quotient
    sy = (240 << 16) - 50 * quotient
    depth = 32 * quotient
    vectors.append({
        "name": "rtps", "data": {0: pack_xy(100, -50), 1: 1000},
        "control": control, "command": 0x00080001,
        "checks": [(0x89, 100), (0x8A, (-50) & 0xFFFFFFFF), (0x8B, 2000),
                   (0x93, 2000), (0x8E, pack_xy(sx >> 16, sy >> 16)),
                   (0x98, depth), (0x88, min(0x1000, depth >> 12)),
                   (0xBF, flag)],
    })

    control = dict(identity)
    control.update({7: 1000, 24: 320 << 16, 25: 240 << 16,
                    26: 256, 27: 16, 28: 0})
    rtpt_data = {0: pack_xy(-100, -50), 1: 1000,
                 2: pack_xy(0, 0), 3: 1200,
                 4: pack_xy(100, 50), 5: 1400}
    screens = []
    for x, y, z in [(-100, -50, 2000), (0, 0, 2200), (100, 50, 2400)]:
        q, _ = gte_divide(256, z)
        screens.append(pack_xy(((320 << 16) + x * q) >> 16,
                               ((240 << 16) + y * q) >> 16))
    final_q, final_flag = gte_divide(256, 2400)
    vectors.append({
        "name": "rtpt", "data": rtpt_data, "control": control,
        "command": 0x00080030,
        "checks": [(0x8C, screens[0]), (0x8D, screens[1]),
                   (0x8E, screens[2]), (0x91, 2000), (0x92, 2200),
                   (0x93, 2400), (0x98, 16 * final_q),
                   (0xBF, final_flag)],
    })
    return vectors


def emit_array(name: str, values: list[int]) -> list[str]:
    lines = [f"static const uint32_t {name}[] = {{"]
    for pos in range(0, len(values), 4):
        row = ", ".join(f"0x{v & 0xFFFFFFFF:08x}u" for v in values[pos:pos + 4])
        lines.append(f"\t{row},")
    lines.append("};")
    return lines


def generate() -> str:
    lines = [
        "// Generated by tools/gen_psx_vectors.py; do not edit.",
        "#ifndef PSX_VECTORS_H", "#define PSX_VECTORS_H", "",
        "struct psx_vector_check { uint16_t selector; uint16_t reserved; uint32_t expected; };",
        "struct psx_cpu_vector { const uint32_t *program; uint16_t words; uint16_t steps;",
        "\tconst struct psx_vector_check *checks; uint16_t check_count; };",
        "struct psx_gte_write { uint8_t control; uint8_t reg; uint16_t reserved; uint32_t value; };",
        "struct psx_gte_vector { const struct psx_gte_write *writes; uint16_t write_count;",
        "\tuint16_t reserved; uint32_t command; const struct psx_vector_check *checks;",
        "\tuint16_t check_count; };", "",
    ]
    hash_words: list[int] = []
    for number, vector in enumerate(CPU_VECTORS):
        name = f"cpu_program_{number}"
        program = vector["program"]
        steps = vector.get("steps", len(program))
        checks = vector["checks"]
        lines += emit_array(name, program)
        lines.append(f"static const struct psx_vector_check cpu_checks_{number}[] = {{")
        for selector, expected in checks:
            lines.append(f"\t{{0x{selector:04x}u, 0, 0x{expected & 0xFFFFFFFF:08x}u}},")
        lines += ["};", ""]
        hash_words += program + [steps] + [item for check in checks for item in check]
    lines.append("static const struct psx_cpu_vector psx_cpu_vectors[] = {")
    for number, vector in enumerate(CPU_VECTORS):
        lines.append(f"\t{{cpu_program_{number}, {len(vector['program'])}u, "
                     f"{vector.get('steps', len(vector['program']))}u, cpu_checks_{number}, "
                     f"{len(vector['checks'])}u}},")
    lines += ["};", ""]

    gte_vectors = make_gte_vectors()
    for number, vector in enumerate(gte_vectors):
        writes = [(0, reg, value) for reg, value in sorted(vector["data"].items())]
        writes += [(1, reg, value) for reg, value in sorted(vector["control"].items())]
        lines.append(f"static const struct psx_gte_write gte_writes_{number}[] = {{")
        for control, reg, value in writes:
            lines.append(f"\t{{{control}u, {reg}u, 0, 0x{value & 0xFFFFFFFF:08x}u}},")
        lines.append("};")
        lines.append(f"static const struct psx_vector_check gte_checks_{number}[] = {{")
        for selector, expected in vector["checks"]:
            lines.append(f"\t{{0x{selector:04x}u, 0, 0x{expected & 0xFFFFFFFF:08x}u}},")
        lines += ["};", ""]
        hash_words += [item for write in writes for item in write]
        hash_words += [vector["command"]] + [item for check in vector["checks"] for item in check]
    lines.append("static const struct psx_gte_vector psx_gte_vectors[] = {")
    for number, vector in enumerate(gte_vectors):
        write_count = len(vector["data"]) + len(vector["control"])
        lines.append(f"\t{{gte_writes_{number}, {write_count}u, 0, 0x{vector['command']:08x}u, "
                     f"gte_checks_{number}, {len(vector['checks'])}u}},")
    payload = b"".join(struct.pack("<I", word & 0xFFFFFFFF) for word in hash_words)
    lines += ["};", "", f"#define PSX_CPU_VECTOR_COUNT {len(CPU_VECTORS)}u",
              f"#define PSX_GTE_VECTOR_COUNT {len(gte_vectors)}u",
              f"#define PSX_VECTOR_CRC32 0x{zlib.crc32(payload):08x}u", "", "#endif", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args()
    text = generate()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    print(f"{args.output}: {len(CPU_VECTORS)} CPU and {len(make_gte_vectors())} GTE vectors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
