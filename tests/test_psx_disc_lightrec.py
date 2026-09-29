#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare JIT and Lightrec disc boot under QEMU with PSX_DISC set to a BIN."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import lightrec_build  # noqa: E402

BIOS_SHA256 = "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3"


def main() -> int:
    bios = Path(os.environ.get("PSX_BIOS", ROOT.parent / "scph1001.bin"))
    disc = Path(os.environ["PSX_DISC"])
    if hashlib.sha256(bios.read_bytes()).hexdigest() != BIOS_SHA256:
        raise RuntimeError("PSX_BIOS is not the verified SCPH-1001 image")
    if not disc.is_file() or disc.stat().st_size % 2352:
        raise RuntimeError("PSX_DISC must be a raw 2352-byte-sector BIN")
    if disc.stat().st_size // 2352 != 281270:
        raise RuntimeError("PSX_DISC must be the 281270-sector Spyro image")
    checkpoint = int(os.environ.get("PSX_DISC_VBLANKS", "600"))
    if checkpoint < 600:
        raise RuntimeError("checkpoint must include the Spyro handoff at VBlank 600")
    qemu = shutil.which("qemu-riscv32")
    if not qemu:
        raise RuntimeError("qemu-riscv32 is required")
    compiler = lightrec_build.tool("gcc")
    sources = [ROOT / "tests/psx_disc_cores_rv32.c",
               ROOT / "software/programs/psx_bios/bios.S",
               ROOT / "software/lightrec/runtime.c",
               *(ROOT / "software/psx" / name for name in
                 ("r3000.c", "gte.c", "gpu.c", "jit.c", "machine.c",
                  "cdrom.c", "sio.c"))]
    with tempfile.TemporaryDirectory(prefix="tang-disc-cores-") as tmp:
        work = Path(tmp)
        shutil.copyfile(bios, work / "scph1001.bin")
        library = work / "lightrec"
        lightrec_build.build(library)
        reports = {}
        with disc.open("rb") as image:
            for name, define in (("jit", []), ("lightrec", ["-DPSX_DISC_LIGHTREC"])):
                if checkpoint > 600 and name == "jit":
                    continue
                program = work / f"psx_disc_{name}"
                subprocess.run([
                    compiler, *lightrec_build.ARCH_FLAGS, "-O2", "-g",
                    "-Wall", "-Wextra", "-Werror", "-nostdlib", "-nostartfiles",
                    "-Wl,--no-relax", "-Wl,--gc-sections",
                    f"-DDISC_FD={image.fileno()}",
                    f"-DCHECKPOINT_VBLANKS={checkpoint}u", *define,
                    f"-T{ROOT / 'tests/psx_jit_rv32.ld'}",
                    *lightrec_build.include_flags(library),
                    f"-I{ROOT / 'software/psx'}", f"-Wa,-I{work}",
                    *map(str, sources),
                    *([str(ROOT / "software/lightrec/psx_lightrec.c")]
                      if define else []),
                    *lightrec_build.link_flags(library), "-o", str(program),
                ], check=True)
                completed = subprocess.run(
                    [qemu, str(program)], pass_fds=(image.fileno(),),
                    capture_output=True, text=True, timeout=600)
                print(f"== {name}\n{completed.stderr}", end="", flush=True)
                if completed.returncode:
                    raise RuntimeError(f"{name} failed: exit {completed.returncode}")
                reports[name] = completed.stderr
        for name, report in reports.items():
            match = re.search(rf"^vblank={checkpoint} .*?gpu=(\d+) "
                              r"cd_cmds=(\d+) sectors=(\d+)", report, re.M)
            if not match or "exit_flags=00000000" not in report:
                raise RuntimeError(f"{name}: checkpoint or exit flags missing")
            gpu, commands, sectors = map(int, match.groups())
            if checkpoint == 600:
                if (gpu, commands) != (798541, 70) or sectors < 200:
                    raise RuntimeError(f"{name}: Spyro handoff stalled: "
                                       f"GPU={gpu}, CD commands={commands}, "
                                       f"sectors={sectors}")
            elif checkpoint >= 2700:
                if gpu < 8000000 or commands < 900 or sectors < 1000:
                    raise RuntimeError(f"{name}: Spyro menu-range progress stalled: "
                                       f"GPU={gpu}, CD commands={commands}, "
                                       f"sectors={sectors}")
            elif gpu <= 798541 or sectors <= 200:
                raise RuntimeError(f"{name}: no Spyro progress after VBlank 600")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
