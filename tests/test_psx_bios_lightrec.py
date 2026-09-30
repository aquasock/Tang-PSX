#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot SCPH-1001 to the logo with Lightrec as the CPU under qemu-riscv32.

tests/psx_bios_cores_rv32.c runs the hardware service loop (event driven,
so the guest execution is identical on the AE350) with software/lightrec as
the R3000A core (PSX_BIOS_LIGHTREC), linked bare metal against
tools/lightrec_build.py's library. It runs three times, with the guest clock
starting at 0, just below bit 31 and just below the 32-bit wrap, which
exercises Lightrec's 31-bit cycle window.
Each run must reproduce the logo checkpoint's telemetry and framebuffer
exactly, stop with no unexpected Lightrec exit, and match the other runs in
every Lightrec statistic.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import lightrec_build  # noqa: E402

BIOS_SHA256 = "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3"
FRAME_SHA256 = "0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7"
EXPECTED = (
    "calls=114713 instructions=30182705 vblank=144 gpu_words=10768 "
    "primitives=414 uploads=63 dma_words=158497 complete=1"
)
START_CYCLES = (0x00000000, 0x7ff00000, 0xfff00000)
SOURCES = [ROOT / "tests/psx_bios_cores_rv32.c",
           ROOT / "software/lightrec/psx_lightrec.c",
           ROOT / "software/lightrec/runtime.c",
           ROOT / "software/programs/psx_bios/bios.S",
           *(ROOT / "software/psx" / name for name in (
               "r3000.c", "gte.c", "gpu.c", "jit.c", "machine.c", "cdrom.c",
               "sio.c"))]


def main() -> int:
    qemu = shutil.which("qemu-riscv32")
    if not qemu:
        raise RuntimeError("qemu-riscv32 is required")
    compiler = lightrec_build.tool("gcc")
    bios = Path(os.environ.get("PSX_BIOS", ROOT.parent / "scph1001.bin"))
    if not bios.is_file() or hashlib.sha256(bios.read_bytes()).hexdigest() != BIOS_SHA256:
        raise RuntimeError("PSX_BIOS must be the verified 512 KiB SCPH-1001 image")
    with tempfile.TemporaryDirectory(prefix="tang-psx-bios-lightrec-") as temporary:
        work = Path(temporary)
        shutil.copyfile(bios, work / "scph1001.bin")
        library = work / "lightrec"
        lightrec_build.build(library)

        def run(start: int) -> tuple[int, str, str]:
            image = work / f"psx_bios_lightrec_{start:08x}"
            subprocess.run([
                compiler, *lightrec_build.ARCH_FLAGS, "-O2", "-g",
                "-Wall", "-Wextra", "-Werror", "-nostdlib", "-nostartfiles",
                "-Wl,--no-relax", "-Wl,--gc-sections",
                "-DPSX_BIOS_LIGHTREC", f"-DSTART_CYCLES={start:#x}u",
                f"-T{ROOT / 'tests/psx_jit_rv32.ld'}",
                *lightrec_build.include_flags(library),
                f"-I{ROOT / 'software/psx'}", f"-Wa,-I{work}",
                *map(str, SOURCES), *lightrec_build.link_flags(library),
                "-o", str(image),
            ], check=True)
            completed = subprocess.run([qemu, str(image)],
                                       capture_output=True, timeout=120)
            report = completed.stderr.decode("ascii", "replace")
            print(f"== guest clock from {start:#010x}\n{report}", end="")
            if completed.returncode:
                raise RuntimeError(f"{start:#x}: exit {completed.returncode}")
            return start, report, hashlib.sha256(completed.stdout).hexdigest()

        with ThreadPoolExecutor(len(START_CYCLES)) as pool:
            results = list(pool.map(run, START_CYCLES))

    first = results[0][1]
    for start, report, digest in results:
        if EXPECTED not in report:
            raise RuntimeError(f"{start:#x}: SCPH-1001 telemetry changed")
        if digest != FRAME_SHA256:
            raise RuntimeError(f"{start:#x}: framebuffer SHA-256 changed: "
                               f"{digest}")
        if "exit_flags=0x0 " not in report:
            raise RuntimeError(f"{start:#x}: unexpected Lightrec exit")
        if report != first:
            raise RuntimeError(f"{start:#x}: statistics differ from the run "
                               "starting at 0")
    stats = dict((key, int(value)) for key, value in
                 re.findall(r"(\w+)=(\d+)", first))
    print(f"SCPH-1001 Lightrec: exact logo checkpoint from "
          f"{len(START_CYCLES)} guest clock starts, "
          f"{stats['instructions']} instructions, "
          f"{stats['code_emissions']} blocks compiled into "
          f"{stats['code_bytes']} bytes, framebuffer sha256={FRAME_SHA256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
