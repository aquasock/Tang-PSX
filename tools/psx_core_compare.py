#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the R3000A cores on the SCPH-1001 logo checkpoint under QEMU.

Builds tests/psx_bios_cores_rv32.c for the AE350 ISA (rv32imafdc, ilp32,
-O2, newlib-nano) once per core, runs every build to the logo checkpoint
under a plugin-enabled qemu-riscv32, and reports for each:

  correctness  framebuffer SHA-256 and checkpoint telemetry; the interpreter
               and the JIT must agree exactly with and without accelerators
  cost         RV32 instructions, loads, and stores executed, by code group
               and by function (tools/qemu_insn_profile.c counts them)
  code size    host code in the image and code the recompiler generated
  memory       static data, heap used, and the recompiler's buffers

The cores are the interpreter and the current JIT (software/psx), each with
and without the machine's BIOS loop accelerators, and Lightrec, which has no
accelerators. QEMU does not model the AE350's caches or DDR3, so instruction
and access counts are a cost proxy, not a time prediction.

The Debian/Ubuntu qemu-user package has no plugin support. Build one from
the QEMU source (glib development headers are required):

    ../configure --target-list=riscv32-linux-user --enable-plugins \\
        --without-default-features && ninja qemu-riscv32

    PSX_BIOS=/path/to/scph1001.bin tools/psx_core_compare.py \\
        --qemu QEMU/build/qemu-riscv32 --qemu-source QEMU [--json OUT]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from bisect import bisect_right
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import lightrec_build


ROOT = Path(__file__).resolve().parents[1]
BIOS_SHA256 = "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3"
FRAME_SHA256 = "0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7"
# The interpreter/JIT checkpoint of tests/test_psx_bios_jit.py.
ACCELERATED = ("calls=23136 instructions=27870497 vblank=144 "
               "gpu_words=10768 primitives=414 uploads=63 dma_words=158497 "
               "complete=1")
MACHINE_SOURCES = ("r3000.c", "gte.c", "gpu.c", "jit.c", "machine.c",
                   "cdrom.c", "sio.c")
CORES = {
    "interpreter": ["-DPSX_JIT_INTERPRET_ONLY"],
    "jit": [],
    "interpreter-noaccel": ["-DPSX_JIT_INTERPRET_ONLY", "-DPSX_DISABLE_ACCEL"],
    "jit-noaccel": ["-DPSX_DISABLE_ACCEL"],
    "lightrec": ["-DPSX_BIOS_LIGHTREC"],
}
# Code groups, by linked object; the display output is split out by function
# because every core runs the same conversion after the checkpoint.
GROUPS = ("generated code", "recompiler", "interpreter", "GTE", "GPU",
          "machine and devices", "libc and runtime", "display output",
          "other")
DISPLAY_FUNCTIONS = {"write_display", "psx_machine_copy_display",
                     "write_all", "tpx_runtime_write"}


def object_group(path: str) -> str:
    name = path.rsplit("/", 1)[-1]
    member = re.search(r"\((.+)\)$", name)
    if member:
        archive, name = name.split("(")[0], member.group(1)
        if archive == "liblightrec.a":
            return "recompiler"
        if archive.startswith(("libc", "libg", "libm")):
            return "libc and runtime"
    stem = name.removesuffix(".o")
    return {
        "jit": "recompiler", "psx_lightrec": "recompiler",
        "r3000": "interpreter", "gte": "GTE", "gpu": "GPU",
        "machine": "machine and devices", "cdrom": "machine and devices",
        "sio": "machine and devices", "runtime": "libc and runtime",
    }.get(stem, "other")


def link_map(path: Path) -> list[tuple[int, int, str]]:
    """Executable input sections as (start, end, group)."""
    sections = []
    pattern = re.compile(
        r"^ \.(?:text|init|fini)\S*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$",
        re.M)
    # ld wraps a long section name onto its own line.
    text = re.sub(r"^( \.\S+)\n\s+(0x)", r"\1 \2", path.read_text(),
                  flags=re.M)
    for start, size, owner in pattern.findall(text):
        start, size = int(start, 16), int(size, 16)
        if size:
            sections.append((start, start + size, object_group(owner)))
    return sorted(sections)


def symbols(image: Path) -> list[tuple[int, int, str]]:
    output = subprocess.run([lightrec_build.tool("nm"), "-S", "-n",
                             "--defined-only", str(image)],
                            capture_output=True, text=True, check=True).stdout
    functions = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[2] in "tTW":
            start = int(fields[0], 16)
            functions.append((start, start + int(fields[1], 16), fields[3]))
    return functions


def section_sizes(image: Path) -> dict[str, int]:
    output = subprocess.run([lightrec_build.tool("size"), "-A", str(image)],
                            capture_output=True, text=True, check=True).stdout
    sizes = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith("."):
            sizes[fields[0]] = int(fields[1])
    return sizes


def symbol_size(image: Path, name: str) -> int:
    for start, end, symbol in object_symbols(image):
        if symbol == name:
            return end - start
    return 0


def object_symbols(image: Path) -> list[tuple[int, int, str]]:
    output = subprocess.run([lightrec_build.tool("nm"), "-S",
                             "--defined-only", str(image)],
                            capture_output=True, text=True, check=True).stdout
    found = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[2] in "bBdD":
            start = int(fields[0], 16)
            found.append((start, start + int(fields[1], 16), fields[3]))
    return found


def attribute(profile: Path, sections, functions):
    starts = [start for start, _, _ in sections]
    function_starts = [start for start, _, _ in functions]
    groups = defaultdict(lambda: [0, 0, 0])
    by_function = defaultdict(lambda: [0, 0, 0, ""])
    for line in profile.read_text().splitlines():
        address, executed, loads, stores = line.split()
        address = int(address, 16)
        counts = (int(executed), int(loads), int(stores))
        index = bisect_right(starts, address) - 1
        if index >= 0 and address < sections[index][1]:
            group = sections[index][2]
            f = bisect_right(function_starts, address) - 1
            name = (functions[f][2] if f >= 0 and address < functions[f][1]
                    else f"{sections[index][2]}@{address:08x}")
            if name in DISPLAY_FUNCTIONS:
                group = "display output"
        else:
            group, name = "generated code", "(generated code)"
        for n, value in enumerate(counts):
            groups[group][n] += value
            by_function[name][n] += value
        by_function[name][3] = group
    return dict(groups), dict(by_function)


def build_plugin(source: Path, destination: Path) -> Path:
    glib = subprocess.run(["pkg-config", "--cflags", "glib-2.0"],
                          capture_output=True, text=True, check=True)
    plugin = destination / "libqemu_insn_profile.so"
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-Wall", "-Wextra",
                    "-Werror", "-shared", "-fPIC", *glib.stdout.split(),
                    f"-I{source / 'include/qemu'}",
                    str(ROOT / "tools/qemu_insn_profile.c"), "-o",
                    str(plugin)], check=True)
    return plugin


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qemu", type=Path, required=True,
                        help="plugin-enabled qemu-riscv32")
    parser.add_argument("--qemu-source", type=Path, required=True,
                        help="QEMU source tree, for qemu-plugin.h")
    parser.add_argument("--json", type=Path, help="write the results here")
    parser.add_argument("--top", type=int, default=12,
                        help="functions to list per core (default 12)")
    args = parser.parse_args()
    compiler = lightrec_build.tool("gcc")
    bios = Path(os.environ.get("PSX_BIOS", ROOT.parent / "scph1001.bin"))
    if not bios.is_file() or hashlib.sha256(bios.read_bytes()).hexdigest() != BIOS_SHA256:
        raise RuntimeError("PSX_BIOS must be the verified 512 KiB SCPH-1001 image")

    with tempfile.TemporaryDirectory(prefix="tang-psx-core-compare-") as temporary:
        work = Path(temporary)
        shutil.copyfile(bios, work / "scph1001.bin")
        library = work / "lightrec"
        lightrec_build.build(library)
        plugin = build_plugin(args.qemu_source.resolve(), work)

        def run(core: str) -> dict:
            image = work / f"psx_bios_{core}"
            sources = [ROOT / "tests/psx_bios_cores_rv32.c",
                       ROOT / "software/lightrec/runtime.c",
                       ROOT / "software/programs/psx_bios/bios.S",
                       *(ROOT / "software/psx" / name
                         for name in MACHINE_SOURCES)]
            if core == "lightrec":
                sources.append(ROOT / "software/lightrec/psx_lightrec.c")
            # Named objects, so the link map attributes code to its source.
            objects = work / f"{core}-objects"
            objects.mkdir()
            for source in sources:
                subprocess.run([
                    compiler, *lightrec_build.ARCH_FLAGS, "-O2", "-g",
                    "-Wall", "-Wextra", "-Werror", *CORES[core],
                    *lightrec_build.include_flags(library),
                    f"-I{ROOT / 'software/psx'}", f"-Wa,-I{work}", "-c",
                    str(source), "-o", str(objects / f"{source.stem}.o"),
                ], check=True)
            subprocess.run([
                compiler, *lightrec_build.ARCH_FLAGS, "-nostdlib",
                "-nostartfiles", "-Wl,--no-relax", "-Wl,--gc-sections",
                f"-Wl,-Map={image}.map",
                f"-T{ROOT / 'tests/psx_jit_rv32.ld'}",
                *(str(objects / f"{source.stem}.o") for source in sources),
                *lightrec_build.link_flags(library), "-o", str(image),
            ], check=True)
            profile = work / f"{core}.profile"
            completed = subprocess.run(
                [str(args.qemu), "-plugin", f"{plugin},out={profile}",
                 str(image)], capture_output=True, timeout=3600)
            report = completed.stderr.decode("ascii", "replace")
            if completed.returncode:
                raise RuntimeError(f"{core}: exit {completed.returncode}\n"
                                   f"{report}")
            groups, functions = attribute(profile,
                                          link_map(Path(f"{image}.map")),
                                          symbols(image))
            sizes = section_sizes(image)
            reserved = (symbol_size(image, "heap") +
                        symbol_size(image, "code_buffer"))
            stats = {key: int(value) for key, value in
                     re.findall(r"(\w+)=(\d+)", report)}
            return {
                "core": core, "report": report,
                "frame_sha256": hashlib.sha256(completed.stdout).hexdigest(),
                "stats": stats, "groups": groups, "functions": functions,
                "text_bytes": sizes.get(".text", 0),
                "rodata_data_bytes": sizes.get(".rodata", 0) +
                sizes.get(".data", 0),
                "bss_bytes": sizes.get(".bss", 0) - reserved,
            }

        with ThreadPoolExecutor(len(CORES)) as pool:
            results = {result["core"]: result
                       for result in pool.map(run, CORES)}

    check(results)
    print_report(results, args.top)
    if args.json:
        args.json.write_text(json.dumps(results, indent=1) + "\n")
    return 0


def first_line(result: dict) -> str:
    return result["report"].splitlines()[0]


def check(results: dict) -> None:
    for core, result in results.items():
        if result["frame_sha256"] != FRAME_SHA256:
            raise RuntimeError(f"{core}: framebuffer SHA-256 changed: "
                               f"{result['frame_sha256']}")
        if result["stats"].get("complete") != 1:
            raise RuntimeError(f"{core}: logo checkpoint not reached")
        if result["stats"].get("exit_flags", 0):
            raise RuntimeError(f"{core}: unexpected exit")
    for core in ("interpreter", "jit"):
        if first_line(results[core]) != ACCELERATED:
            raise RuntimeError(f"{core}: checkpoint telemetry changed:\n"
                               f"{first_line(results[core])}")
    if first_line(results["interpreter-noaccel"]) != \
            first_line(results["jit-noaccel"]):
        raise RuntimeError("interpreter and JIT differ without accelerators")


def print_report(results: dict, top: int) -> None:
    cores = list(results)
    width = max(len(group) for group in GROUPS) + 2

    def row(label: str, values) -> None:
        print(f"{label:<{width}}" + "".join(f"{value:>20}" for value in values))

    def millions(value: int) -> str:
        return f"{value / 1e6:,.1f}M"

    print("SCPH-1001 logo checkpoint, RV32 host cost under QEMU "
          "(framebuffer and telemetry verified)\n")
    row("", cores)
    row("guest instructions", [f"{r['stats']['instructions']:,}"
                               for r in results.values()])
    row("  accelerated", [f"{r['stats'].get('accelerated', 0):,}"
                          for r in results.values()])
    executed = {core: r["stats"]["instructions"] -
                r["stats"].get("accelerated", 0)
                for core, r in results.items()}
    row("  executed", [f"{executed[core]:,}" for core in cores])
    print()
    for n, title in enumerate(("RV32 instructions", "loads", "stores")):
        print(title)
        for group in GROUPS:
            values = [r["groups"].get(group, [0, 0, 0])[n]
                      for r in results.values()]
            if any(values):
                row(f"  {group}", [millions(value) for value in values])
        totals = {core: sum(v[n] for g, v in r["groups"].items()
                            if g != "display output")
                  for core, r in results.items()}
        row("  total (no display)", [millions(totals[core]) for core in cores])
        if n == 0:
            cpu = {core: sum(r["groups"].get(g, [0])[0] for g in (
                "generated code", "recompiler", "interpreter", "GTE"))
                for core, r in results.items()}
            row("  CPU core only", [millions(cpu[core]) for core in cores])
            row("  CPU / executed guest", [
                f"{cpu[core] / executed[core]:.2f}" for core in cores])
            row("  total / checkpoint", [
                f"{totals[core] / totals['interpreter']:.2f}x"
                for core in cores])
        print()
    print("code and memory (bytes)")
    row("  host .text", [f"{r['text_bytes']:,}" for r in results.values()])
    row("  .rodata + .data", [f"{r['rodata_data_bytes']:,}"
                              for r in results.values()])
    row("  .bss (no reservations)", [f"{r['bss_bytes']:,}"
                                     for r in results.values()])
    row("  heap used", [f"{r['stats'].get('heap_bytes', 0):,}"
                        for r in results.values()])
    row("  generated code", [
        f"{r['stats'].get('code_bytes', r['stats'].get('jit_compiled_words', 0) * 4):,}"
        for r in results.values()])
    row("  blocks compiled", [
        f"{r['stats'].get('code_emissions', r['stats'].get('jit_compiled', 0)):,}"
        for r in results.values()])
    print()
    for core, result in results.items():
        print(f"{core}: top functions by RV32 instructions")
        ranked = sorted(result["functions"].items(),
                        key=lambda item: -item[1][0])[:top]
        total = sum(v[0] for v in result["functions"].values())
        for name, (count, loads, stores, group) in ranked:
            print(f"  {count / total:6.1%} {millions(count):>9} "
                  f"{name} [{group}]")
        print()


if __name__ == "__main__":
    raise SystemExit(main())
