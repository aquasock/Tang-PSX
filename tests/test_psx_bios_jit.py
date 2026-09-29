#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot SCPH-1001 to the logo through the RV32 JIT under qemu-riscv32.

The hardware service loop is event driven, so the guest execution, and with
it every JIT statistic, is identical on the AE350. The run must reproduce the
host interpreter's telemetry and framebuffer exactly; the JIT profile is
printed for comparison with hardware timing.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BIOS_SHA256 = "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3"
FRAME_SHA256 = "0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7"
EXPECTED = (
    "calls=23136 instructions=27870497 accelerated=21984983 vblank=144 "
    "gpu_words=10768 primitives=414 uploads=63 dma_words=158497 complete=1"
)


def main() -> int:
    compiler = os.environ.get(
        "RISCV_CC", "/home/vash/.cache/tangcore-dev/toolchain/bin/"
        "riscv64-unknown-elf-gcc")
    qemu = shutil.which("qemu-riscv32")
    if not Path(compiler).is_file() or not qemu:
        raise RuntimeError("RV32 compiler and qemu-riscv32 are required")
    bios = Path(os.environ.get("PSX_BIOS", ROOT.parent / "scph1001.bin"))
    if not bios.is_file() or hashlib.sha256(bios.read_bytes()).hexdigest() != BIOS_SHA256:
        raise RuntimeError("PSX_BIOS must be the verified 512 KiB SCPH-1001 image")
    with tempfile.TemporaryDirectory(prefix="tang-psx-bios-jit-") as temporary:
        work = Path(temporary)
        shutil.copyfile(bios, work / "scph1001.bin")
        executable = work / "psx_bios_rv32"
        subprocess.run([
            compiler, "-march=rv32im", "-mabi=ilp32", "-O2",
            "-ffreestanding", "-fno-builtin", "-nostdlib", "-static",
            "-Wall", "-Wextra", "-Werror",
            f"-T{ROOT / 'tests/psx_jit_rv32.ld'}",
            f"-I{ROOT / 'software/psx'}", f"-Wa,-I{work}",
            str(ROOT / "tests/psx_bios_rv32.c"),
            str(ROOT / "software/programs/psx_bios/bios.S"),
            *(str(ROOT / "software/psx" / name) for name in (
                "r3000.c", "gte.c", "gpu.c", "jit.c", "machine.c",
                "cdrom.c", "sio.c")),
            "-lgcc", "-o", str(executable),
        ], check=True)
        completed = subprocess.run([qemu, str(executable)],
                                   capture_output=True)
        report = completed.stderr.decode("ascii", "replace")
        print(report, end="")
        if completed.returncode or EXPECTED not in report:
            raise RuntimeError("SCPH-1001 JIT execution telemetry changed")
        digest = hashlib.sha256(completed.stdout).hexdigest()
        if digest != FRAME_SHA256:
            raise RuntimeError(f"JIT framebuffer SHA-256 changed: {digest}")
        stats = dict((key, int(value)) for key, value in
                     re.findall(r"(\w+)=(\d+)", report))
        executed = stats["jit_instructions"] + stats["jit_fallback"]
        print(f"SCPH-1001 JIT: exact logo checkpoint, "
              f"{100 * stats['jit_instructions'] / executed:.1f}% of "
              f"{executed} executed instructions compiled, "
              f"framebuffer sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
