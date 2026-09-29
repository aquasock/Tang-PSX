#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run SCPH-1001 through the portable machine and verify its final framebuffer."""

from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BIOS_SHA256 = "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3"
FRAME_SHA256 = "0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7"


def main() -> int:
    bios = Path(os.environ.get("PSX_BIOS", ROOT.parent / "scph1001.bin"))
    if not bios.is_file() or hashlib.sha256(bios.read_bytes()).hexdigest() != BIOS_SHA256:
        raise RuntimeError("PSX_BIOS must be the verified 512 KiB SCPH-1001 image")

    with tempfile.TemporaryDirectory(prefix="tang-psx-bios-") as temporary:
        work = Path(temporary)
        executable = work / "psx_bios_host"
        framebuffer = work / "psx_bios.ppm"
        sources = [
            ROOT / "software/psx/r3000.c",
            ROOT / "software/psx/gte.c",
            ROOT / "software/psx/gpu.c",
            ROOT / "software/psx/machine.c",
            ROOT / "tests/psx_bios_host.c",
        ]
        subprocess.run([
            "cc", "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", f"-I{ROOT / 'software/psx'}",
            *(str(source) for source in sources), "-o", str(executable),
        ], check=True)
        completed = subprocess.run([
            str(executable), str(bios), "100", str(framebuffer),
        ], check=True, text=True, capture_output=True)
        expected = (
            "instructions=99999544 pc=80059d68 ra=80059d18 sp=801ffd50 "
            "exceptions=462 status=00000401 cause=00000000 irq=00000000/00000009 "
            "vblank=199 gpu_words=10768 primitives=414 uploads=63 unknown_gpu=0 "
            "dma_words=158497 unknown=0/0"
        )
        if expected not in completed.stdout:
            print(completed.stdout, end="")
            raise RuntimeError("SCPH-1001 execution telemetry changed")
        digest = hashlib.sha256(framebuffer.read_bytes()).hexdigest()
        if digest != FRAME_SHA256:
            raise RuntimeError(f"framebuffer SHA-256 changed: {digest}")
        print("SCPH-1001 BIOS: 99,999,544 instructions, 10,768 GPU words, "
              f"framebuffer sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
