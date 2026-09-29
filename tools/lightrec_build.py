#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build Lightrec and the patched GNU Lightning as one bare-metal RV32 library.

The library targets the AE350 program ABI (rv32imafdc, ilp32) with no OS:
Lightning is built without mmap (HAVE_MMAP=0), so every emission goes to the
caller's code buffer, and Lightrec uses the static configuration in
software/lightrec/lightrec-config.h (no threads, TLSF-managed code buffer).
Programs link the library with software/lightrec/runtime.c and newlib-nano.

    tools/lightrec_build.py DESTINATION [--log-level LEVEL]

writes DESTINATION/liblightrec.a and prints the compiler flags a program
needs to include lightrec.h and link the library.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import lightning_source


ROOT = Path(__file__).resolve().parents[1]
LIGHTREC = ROOT / "third_party/lightrec"
RUNTIME = ROOT / "software/lightrec"
ARCH_FLAGS = ("-march=rv32imafdc", "-mabi=ilp32")
LIGHTNING_SOURCES = ("jit_disasm.c", "jit_memory.c", "jit_note.c",
                     "jit_print.c", "jit_size.c", "lightning.c")
LIGHTREC_SOURCES = ("blockcache.c", "constprop.c", "emitter.c",
                    "interpreter.c", "lightrec.c", "memmanager.c",
                    "optimizer.c", "regcache.c", "tlsf/tlsf.c")
LOG_LEVELS = ("NOLOG", "ERROR", "WARNING", "INFO", "DEBUG")


def tool(name: str) -> str:
    path = shutil.which(f"riscv64-unknown-elf-{name}")
    if not path:
        raise RuntimeError(f"riscv64-unknown-elf-{name} is required; "
                           "source scripts/env.sh")
    return path


def include_flags(destination: Path) -> list[str]:
    """Flags for a program that includes lightrec.h and the runtime."""
    return [f"-I{destination / 'include'}",
            f"-I{destination / 'lightning/include'}", f"-I{RUNTIME}",
            f"-I{LIGHTREC}"]


def link_flags(destination: Path) -> list[str]:
    """Libraries a -nostdlib program links after its own objects."""
    return [str(destination / "liblightrec.a"), "-Wl,--start-group",
            "-lc_nano", "-lgcc", "-Wl,--end-group"]


def build(destination: Path, log_level: str = "WARNING") -> Path:
    """Build destination/liblightrec.a and return its path."""
    compiler, archiver = tool("gcc"), tool("ar")
    destination.mkdir(parents=True, exist_ok=True)
    lightning = destination / "lightning"
    if lightning.exists():
        shutil.rmtree(lightning)
    lightning_source.prepare(lightning)
    include = destination / "include"
    include.mkdir(exist_ok=True)
    header = (lightning / "include/lightning.h.in").read_text()
    (include / "lightning.h").write_text(
        header.replace("@MAYBE_INCLUDE_STDINT_H@", "#include <stdint.h>"))
    objects = destination / "objects"
    if objects.exists():
        shutil.rmtree(objects)
    objects.mkdir()

    common = [*ARCH_FLAGS, "-O2", "-g", "-ffunction-sections",
              "-fdata-sections", "-DHAVE_MMAP=0", f"-I{include}",
              f"-I{lightning / 'include'}"]
    # Upstream Lightning is not warning-clean in the no-mmap configuration.
    lightning_flags = [*common, "-w"]
    # Lightrec's CMake build: C11 with extensions, hidden visibility.
    lightrec_flags = [*common, "-std=gnu11", "-Wall", "-fvisibility=hidden",
                      "-DLIGHTREC_STATIC", f"-DLOG_LEVEL={log_level}_L",
                      f"-I{RUNTIME}", f"-I{LIGHTREC / 'tlsf'}"]
    jobs = [(lightning_flags, lightning / "lib" / name, f"lightning_{name}")
            for name in LIGHTNING_SOURCES]
    jobs += [(lightrec_flags, LIGHTREC / name,
              f"lightrec_{Path(name).name}") for name in LIGHTREC_SOURCES]

    def compile_one(job: tuple[list[str], Path, str]) -> Path:
        flags, source, name = job
        output = objects / Path(name).with_suffix(".o")
        result = subprocess.run([compiler, *flags, "-c", str(source),
                                 "-o", str(output)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"{source.name} failed:\n{result.stderr}")
        if result.stderr:
            print(result.stderr, end="")
        return output

    with ThreadPoolExecutor(os.cpu_count()) as pool:
        outputs = list(pool.map(compile_one, jobs))
    library = destination / "liblightrec.a"
    library.unlink(missing_ok=True)
    subprocess.run([archiver, "rcs", str(library), *map(str, outputs)],
                   check=True)
    return library


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--log-level", choices=LOG_LEVELS, default="WARNING")
    args = parser.parse_args()
    destination = args.destination.resolve()
    build(destination, args.log_level)
    print(" ".join([*ARCH_FLAGS, *include_flags(destination),
                    *link_flags(destination)]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
