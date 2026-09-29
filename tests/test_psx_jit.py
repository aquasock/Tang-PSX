#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-build the RV32 block compiler and execute generated code in QEMU."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    compiler = os.environ.get(
        "RISCV_CC", "/home/vash/.cache/tangcore-dev/toolchain/bin/"
        "riscv64-unknown-elf-gcc")
    qemu = shutil.which("qemu-riscv32")
    if not Path(compiler).is_file() or not qemu:
        raise RuntimeError("RV32 compiler and qemu-riscv32 are required")
    with tempfile.TemporaryDirectory(prefix="tang-psx-jit-") as temporary:
        executable = Path(temporary) / "psx_jit_rv32"
        subprocess.run([
            compiler, "-march=rv32im", "-mabi=ilp32", "-O2",
            "-ffreestanding", "-fno-builtin", "-nostdlib", "-static",
            f"-T{ROOT / 'tests/psx_jit_rv32.ld'}",
            f"-I{ROOT / 'software/psx'}",
            str(ROOT / "tests/psx_jit_rv32_start.S"),
            str(ROOT / "tests/psx_jit_rv32.c"),
            str(ROOT / "software/psx/jit.c"),
            str(ROOT / "software/psx/r3000.c"),
            str(ROOT / "software/psx/gte.c"),
            "-lgcc", "-o", str(executable),
        ], check=True)
        completed = subprocess.run([qemu, str(executable)])
        if completed.returncode:
            raise RuntimeError(
                f"RV32 generated-code test failed: {completed.returncode}")
    print("PSX JIT: RV32 ALU, branch, delay-slot, RAM, zero-progress "
          "bailout, fallback, and invalidation checks passed under QEMU")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
