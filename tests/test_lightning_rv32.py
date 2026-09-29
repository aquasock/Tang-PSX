#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run GNU Lightning's check suite for a RISC-V target under qemu user mode.

The tree under test is third_party/gnu-lightning's pinned commit with
third_party/patches/gnu-lightning-rv32.patch applied (tools/lightning_source.py).

Lightning's own harness preprocesses each check/*.tst file with the host C
preprocessor, runs it through the check/lightning driver, and compares the
output with check/*.ok.  This script does the same for a bare-metal newlib
build: the preprocessing happens here on the build host with the defines the
driver would pass plus the __riscv macros a native RISC-V preprocessor
predefines, and tests/lightning_shim supplies popen (stdin), dlsym (a
fixed table of the C functions the tests call), and mmap/munmap/mprotect as
Linux system calls serviced by qemu user mode.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import lightning_source  # noqa: E402

# Replaced in main() by the pinned submodule commit with the RV32 patch.
LIGHTNING = ROOT / "third_party/gnu-lightning"
SHIM = ROOT / "tests/lightning_shim"
LIBRARY_SOURCES = ("jit_disasm.c", "jit_memory.c", "jit_note.c",
                   "jit_print.c", "jit_size.c", "lightning.c")
# check/*.c programs from check/Makefile.am TESTS that pass by exit status;
# catomic needs threads, which the bare-metal newlib build does not have.
C_TESTS = ("ccall", "self", "setcode", "nodata", "ctramp", "carg", "cva_list",
           "protect", "riprel", "cbit", "callee")
# The RISC-V backend passes floating-point arguments in FP registers (the
# ilp32d/lp64d convention).  Under ilp32 compiled C expects them in integer
# registers, so the C programs that call C with float or double arguments
# cannot interoperate; Lightning's generated code is identical under both
# 32-bit ABIs, and Lightrec passes no floating-point arguments.
EXPECTED_FAILURES = {"rv32": {"c_ccall", "c_carg", "c_cva_list"}}
TARGETS = {
    # name: (march, mabi, qemu, word size)
    "rv64": ("rv64imafdc", "lp64d", "qemu-riscv64", 64),
    "rv32d": ("rv32imafdc", "ilp32d", "qemu-riscv32", 32),
    "rv32": ("rv32imafdc", "ilp32", "qemu-riscv32", 32),
}


def base_tests() -> list[str]:
    text = (LIGHTNING / "check/Makefile.am").read_text()
    block = re.search(r"^base_TESTS\s*=(.*?)^\S", text, re.M | re.S).group(1)
    return block.replace("\\", " ").split()


def code_names() -> list[str]:
    text = (LIGHTNING / "lib/jit_names.c").read_text()
    body = text[text.index("code_name[] = {"):text.index("};")]
    return re.findall(r'"([^"]*)"', body)


def build_c_tests(target: str, work: Path) -> list[Path]:
    march, mabi, _, _ = TARGETS[target]
    compiler = shutil.which("riscv64-unknown-elf-gcc")
    subprocess.run(["gcc", "-O2", str(LIGHTNING / "check/gen_cbit.c"),
                    "-o", str(work / "gen_cbit")], check=True)
    cbit = work / "cbit.c"
    text = subprocess.run([str(work / "gen_cbit")], check=True,
                          capture_output=True, text=True).stdout
    if TARGETS[target][3] == 32:
        # gen_cbit's full-width field case exists only for 32-bit words; it
        # expects 0 (GENMASK32 sets t = 0) but loads ((1L << 32) - 1) ^ 1,
        # which is undefined in C when long is 32 bits.  Load the value the
        # check expects (other occurrences are in 64-bit-only sections).
        full_width = "jit_movi(JIT_R1, ((ONE << 32) - 1) ^ 1);"
        assert full_width in text
        text = text.replace(full_width, "jit_movi(JIT_R1, 0);")
    cbit.write_text(text)
    flags = [f"-march={march}", f"-mabi={mabi}", "-O2", "-g", "-D_GNU_SOURCE",
             "-DHAVE_MMAP=1", f"-I{work / 'include'}",
             f"-I{LIGHTNING / 'include'}", f"-I{SHIM}", "-w", "-fwrapv"]
    library = [str(LIGHTNING / "lib" / name) for name in LIBRARY_SOURCES]
    programs = []
    for name in C_TESTS:
        source = cbit if name == "cbit" else LIGHTNING / "check" / f"{name}.c"
        program = work / f"c_{name}"
        subprocess.run([compiler, *flags, str(source), *library,
                        str(SHIM / "shim.c"), "-lm", "-o", str(program)],
                       check=True)
        programs.append(program)
    return programs


def run_c_test(target: str, program: Path) -> tuple[str, bool, str, str]:
    qemu = TARGETS[target][2]
    try:
        result = subprocess.run([qemu, str(program)], capture_output=True,
                                text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return program.name, False, "timeout", ""
    ok = result.returncode == 0
    detail = "" if ok else (f"exit {result.returncode}: "
                            + (result.stderr + result.stdout)[-600:])
    return program.name, ok, detail, ""


def build(target: str, work: Path, measure: bool = False) -> Path:
    march, mabi, _, _ = TARGETS[target]
    compiler = shutil.which("riscv64-unknown-elf-gcc")
    if not compiler:
        raise RuntimeError("riscv64-unknown-elf-gcc is required; source scripts/env.sh")
    include = work / "include"
    include.mkdir()
    header = (LIGHTNING / "include/lightning.h.in").read_text()
    (include / "lightning.h").write_text(
        header.replace("@MAYBE_INCLUDE_STDINT_H@", "#include <stdint.h>"))
    flags = [f"-march={march}", f"-mabi={mabi}", "-O2", "-g", "-D_GNU_SOURCE",
             "-DHAVE_MMAP=1",
             f"-I{include}", f"-I{LIGHTNING / 'include'}", f"-I{SHIM}",
             "-w"]
    sources = [LIGHTNING / "lib" / name for name in LIBRARY_SOURCES]
    sources += [LIGHTNING / "check/lightning.c", SHIM / "shim.c"]
    objects = []
    if measure:
        # Lightning's size-measurement mode appends "code size" lines to a
        # file; under qemu user mode they are sent to stderr instead.
        flags += ["-DGET_JIT_SIZE=1", '-DJIT_SIZE_PATH="stderr"']
        sources.remove(LIGHTNING / "lib/jit_size.c")
        size_object = work / "jit_size.o"
        subprocess.run([compiler, *flags, "-Dfopen=shim_size_fopen",
                        "-Dfclose=fflush", "-c",
                        str(LIGHTNING / "lib/jit_size.c"),
                        "-o", str(size_object)], check=True)
        objects.append(str(size_object))
    driver = work / "lightning"
    subprocess.run([compiler, *flags, *map(str, sources), *objects, "-lm",
                    "-o", str(driver)], check=True)
    return driver


def run_test(target: str, driver: Path, name: str, nodata: bool,
             work: Path) -> tuple[str, bool, str, str]:
    _, _, qemu, wordsize = TARGETS[target]
    label = name + (".nodata" if nodata else "")
    source = LIGHTNING / "check" / f"{name}.tst"
    expected = (LIGHTNING / "check" / f"{name}.ok").read_text()
    preprocessed = subprocess.run([
        "gcc", "-E", "-x", "c", str(source), f"-D__WORDSIZE={wordsize}",
        "-D__LITTLE_ENDIAN=1234", "-D__BIG_ENDIAN=4321",
        "-D__BYTE_ORDER=1234", "-D__riscv=1", f"-D__riscv_xlen={wordsize}",
    ], check=True, capture_output=True, text=True).stdout
    # The shim's popen returns stdin whatever the name; newlib's getopt
    # mishandles a lone "-" argument, so the test's own name is passed.
    command = [qemu, str(driver)] + (["-d"] if nodata else []) + [f"{name}.tst"]
    try:
        result = subprocess.run(command, input=preprocessed,
                                capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return label, False, "timeout", ""
    output = result.stdout.replace("\r", "")
    if result.returncode != 0:
        return label, False, (f"exit {result.returncode}: "
                              + (result.stderr.strip() or output)[-600:]), ""
    if output != expected:
        (work / f"{label}.out").write_text(output)
        diff = subprocess.run(["diff", str(LIGHTNING / "check" / f"{name}.ok"),
                               str(work / f"{label}.out")],
                              capture_output=True, text=True).stdout
        return label, False, diff[-600:], ""
    return label, True, "", result.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=TARGETS, default="rv32d")
    parser.add_argument("--no-nodata", action="store_true",
                        help="skip the -d (no data buffer) variants")
    parser.add_argument("--no-c-tests", action="store_true",
                        help="skip the check/*.c programs")
    parser.add_argument("--measure-sizes", type=Path, metavar="FILE",
                        help="build in GET_JIT_SIZE mode and write the "
                             "measured instruction-size table to FILE")
    parser.add_argument("tests", nargs="*",
                        help="check/*.tst names (default: all base tests)")
    args = parser.parse_args()

    global LIGHTNING
    names = args.tests or base_tests()
    with tempfile.TemporaryDirectory(prefix="tang-psx-lightning-") as temporary:
        work = Path(temporary)
        LIGHTNING = lightning_source.prepare(work / "gnu-lightning")
        driver = build(args.target, work, bool(args.measure_sizes))
        jobs = [(name, False) for name in names]
        if not args.no_nodata:
            jobs += [(name, True) for name in names]
        programs = []
        if not (args.no_c_tests or args.tests or args.measure_sizes):
            programs = build_c_tests(args.target, work)
        with ThreadPoolExecutor() as pool:
            results = list(pool.map(
                lambda job: run_test(args.target, driver, *job, work), jobs))
            results += list(pool.map(
                lambda program: run_c_test(args.target, program), programs))
    expected = EXPECTED_FAILURES.get(args.target, set())
    failures = [(label, detail) for label, ok, detail, _ in results
                if not ok and label not in expected]
    for label, detail in failures:
        print(f"FAIL {label}\n{detail}\n")
    for label, ok, _, _ in results:
        if label in expected:
            print(f"{'UNEXPECTED PASS' if ok else 'expected failure'} {label}")
            if ok:
                failures.append((label, "unexpected pass"))
    if args.measure_sizes:
        names = code_names()
        sizes = [0] * len(names)
        for *_, stderr in results:
            for code, size in re.findall(r"^(\d+) (\d+)$", stderr, re.M):
                sizes[int(code)] = max(sizes[int(code)], int(size))
        wordsize = TARGETS[args.target][3]
        lines = [f"#if __WORDSIZE == {wordsize}",
                 f"#define JIT_INSTR_MAX {max(sizes)}"]
        lines += [f"    {size},\t/* {name} */"
                  for size, name in zip(sizes, names)]
        lines.append("#endif /* __WORDSIZE */")
        args.measure_sizes.write_text("\n".join(lines) + "\n")
        print(f"wrote {len(names)} instruction sizes to {args.measure_sizes}")
    passed = sum(ok for _, ok, _, _ in results)
    print(f"GNU Lightning {args.target}: {passed}/{len(results)} check tests "
          f"passed ({len(programs)} C programs, "
          f"{sum(label in expected for label, *_ in results)} expected failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
