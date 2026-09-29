#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the bare-metal Lightrec library and run its tests in qemu-riscv32.

tools/lightrec_build.py builds Lightrec and the patched GNU Lightning for the
AE350 ABI (rv32imafdc, ilp32, no mmap). Both programs link it with
software/lightrec/runtime.c and newlib-nano and no startup files:
- tests/lightrec_smoke_rv32.c: Lightning without mmap, and Lightrec running a
  small MIPS program compiled and interpreted;
- tests/psx_lightrec_unit_rv32.c: software/lightrec/psx_lightrec.c driving the
  PSX machine through interrupts, a GTE command at EPC, DMA over compiled
  code, a system call and cache isolation.
The test also checks each linked image is really bare metal: the only system
calls are the two qemu-user platform hooks, which the AE350 build replaces.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import lightrec_build  # noqa: E402

PLATFORM_HOOKS = {"tpx_runtime_write", "tpx_runtime_exit"}
MACHINE_SOURCES = [ROOT / "software/psx" / name for name in (
    "r3000.c", "gte.c", "gpu.c", "jit.c", "machine.c", "cdrom.c", "sio.c")]
PROGRAMS = {
    "lightrec_smoke_rv32": [ROOT / "tests/lightrec_smoke_rv32.c"],
    "psx_lightrec_unit_rv32": [ROOT / "tests/psx_lightrec_unit_rv32.c",
                               ROOT / "software/lightrec/psx_lightrec.c",
                               *MACHINE_SOURCES],
}


def system_call_sites(objdump: str, image: Path) -> set[str]:
    """Return the functions of image that contain an ecall."""
    listing = subprocess.run([objdump, "-d", str(image)], check=True,
                             capture_output=True, text=True).stdout
    function, sites = "", set()
    for line in listing.splitlines():
        label = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
        if label:
            function = label.group(1)
        elif re.search(r"\secall\b", line):
            sites.add(function)
    return sites


def main() -> int:
    qemu = shutil.which("qemu-riscv32")
    if not qemu:
        raise RuntimeError("qemu-riscv32 is required")
    compiler = lightrec_build.tool("gcc")
    with tempfile.TemporaryDirectory(prefix="tang-lightrec-") as temporary:
        work = Path(temporary)
        library = work / "lightrec"
        lightrec_build.build(library)
        for name, sources in PROGRAMS.items():
            image = work / name
            subprocess.run([
                compiler, *lightrec_build.ARCH_FLAGS, "-O2", "-g",
                "-Wall", "-Wextra", "-Werror", "-nostdlib", "-nostartfiles",
                "-Wl,--no-relax", "-Wl,--gc-sections",
                f"-T{ROOT / 'tests/psx_jit_rv32.ld'}",
                *lightrec_build.include_flags(library),
                f"-I{ROOT / 'software/psx'}", *map(str, sources),
                str(ROOT / "software/lightrec/runtime.c"),
                *lightrec_build.link_flags(library), "-o", str(image),
            ], check=True)
            sites = system_call_sites(lightrec_build.tool("objdump"), image)
            if sites - PLATFORM_HOOKS:
                raise RuntimeError(f"{name}: system calls outside the "
                                   "platform hooks: "
                                   + ", ".join(sorted(sites - PLATFORM_HOOKS)))
            completed = subprocess.run([qemu, str(image)], capture_output=True,
                                       text=True, timeout=60)
            print(f"== {name}")
            print(completed.stderr, end="")
            if completed.returncode or not completed.stderr.endswith("PASS\n"):
                raise RuntimeError(
                    f"{name} failed (exit {completed.returncode})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
