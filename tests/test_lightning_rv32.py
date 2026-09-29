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

Typical use, from the repository root with scripts/env.sh sourced:
  tests/test_lightning_rv32.py --target all          all three targets, ~15 s
  tests/test_lightning_rv32.py --no-nodata --no-c-tests   quick RV32 check
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import time
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


def compile_all(jobs: list[list[str]]) -> None:
    """Run independent compiler invocations concurrently."""
    with ThreadPoolExecutor() as pool:
        for result in pool.map(lambda command: subprocess.run(command), jobs):
            if result.returncode:
                raise subprocess.CalledProcessError(result.returncode,
                                                    result.args)


def timed(function, *arguments):
    start = time.monotonic()
    result = function(*arguments)
    return result, time.monotonic() - start


def build_c_tests(target: str, work: Path, library: list[Path]) -> list[Path]:
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
    # Each program is compiled and linked against the library objects that
    # build() already produced, all programs at once.  The generated cbit.c
    # (80,000 lines for 64-bit words) takes 86 s at -O2 and 4 s at -O0; it is
    # test-side code, and the Lightning library under test stays at -O2.
    programs = [work / f"c_{name}" for name in C_TESTS]
    compile_all([
        [compiler, *([f if f != "-O2" else "-O0" for f in flags]
                     if name == "cbit" else flags),
         str(cbit if name == "cbit" else LIGHTNING / "check" / f"{name}.c"),
         *map(str, library), "-lm", "-o", str(program)]
        for name, program in zip(C_TESTS, programs)])
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


def build(target: str, work: Path,
          measure: bool = False) -> tuple[Path, list[Path]]:
    """Build the check driver; return it and the library objects it uses."""
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
    size_flags = []
    if measure:
        # Lightning's size-measurement mode appends "code size" lines to a
        # file; under qemu user mode they are sent to stderr instead.
        flags += ["-DGET_JIT_SIZE=1", '-DJIT_SIZE_PATH="stderr"']
        size_flags = ["-Dfopen=shim_size_fopen", "-Dfclose=fflush"]
    objects = work / "objects"
    objects.mkdir()
    sources = [LIGHTNING / "lib" / name for name in LIBRARY_SOURCES]
    sources += [SHIM / "shim.c", LIGHTNING / "check/lightning.c"]
    # lib/lightning.c and check/lightning.c share a stem.
    outputs = [objects / f"{source.parent.name}_{source.stem}.o"
               for source in sources]
    compile_all([
        [compiler, *flags, *(size_flags if source.name == "jit_size.c" else []),
         "-c", str(source), "-o", str(output)]
        for source, output in zip(sources, outputs)])
    library, driver_object = outputs[:-1], outputs[-1]
    driver = work / "lightning"
    subprocess.run([compiler, *flags, str(driver_object), *map(str, library),
                    "-lm", "-o", str(driver)], check=True)
    return driver, library


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


def run_target(target: str, args: argparse.Namespace, names: list[str],
               code_name: list[str], work: Path) -> tuple[list[str], bool]:
    """Build and run one target; return its report lines and success."""
    work.mkdir()
    report = []
    driver, library = build(target, work, bool(args.measure_sizes))
    jobs = [(name, False) for name in names]
    if not args.no_nodata:
        jobs += [(name, True) for name in names]
    programs = []
    if not (args.no_c_tests or args.tests or args.measure_sizes):
        programs = build_c_tests(target, work, library)
    with ThreadPoolExecutor() as pool:
        timed_results = list(pool.map(
            lambda job: timed(run_test, target, driver, *job, work), jobs))
        timed_results += list(pool.map(
            lambda program: timed(run_c_test, target, program), programs))
    results = [result for result, _ in timed_results]
    if args.timings:
        slowest = sorted(timed_results, key=lambda item: -item[1])
        report.append(f"{target}: slowest of {len(slowest)} tests "
                      "(wall seconds, run concurrently):")
        for (label, *_), seconds in slowest[:args.timings]:
            report.append(f"  {seconds:7.2f}  {label}")
    expected = EXPECTED_FAILURES.get(target, set())
    failures = [(label, detail) for label, ok, detail, _ in results
                if not ok and label not in expected]
    for label, detail in failures:
        report.append(f"FAIL {target} {label}\n{detail}\n")
    for label, ok, _, _ in results:
        if label in expected:
            report.append(f"{'UNEXPECTED PASS' if ok else 'expected failure'} "
                          f"{target} {label}")
            if ok:
                failures.append((label, "unexpected pass"))
    if args.measure_sizes:
        sizes = [0] * len(code_name)
        for *_, stderr in results:
            for code, size in re.findall(r"^(\d+) (\d+)$", stderr, re.M):
                sizes[int(code)] = max(sizes[int(code)], int(size))
        wordsize = TARGETS[target][3]
        lines = [f"#if __WORDSIZE == {wordsize}",
                 f"#define JIT_INSTR_MAX {max(sizes)}"]
        lines += [f"    {size},\t/* {name} */"
                  for size, name in zip(sizes, code_name)]
        lines.append("#endif /* __WORDSIZE */")
        args.measure_sizes.write_text("\n".join(lines) + "\n")
        report.append(f"wrote {len(code_name)} instruction sizes to "
                      f"{args.measure_sizes}")
    passed = sum(ok for _, ok, _, _ in results)
    report.append(f"GNU Lightning {target}: {passed}/{len(results)} check "
                  f"tests passed ({len(programs)} C programs, "
                  f"{sum(label in expected for label, *_ in results)} "
                  "expected failures)")
    return report, not failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=[*TARGETS, "all"], default="rv32d",
                        help="target to test, or all three concurrently")
    parser.add_argument("--no-nodata", action="store_true",
                        help="skip the -d (no data buffer) variants")
    parser.add_argument("--timings", type=int, default=0, metavar="N",
                        help="print the N slowest tests")
    parser.add_argument("--no-c-tests", action="store_true",
                        help="skip the check/*.c programs")
    parser.add_argument("--measure-sizes", type=Path, metavar="FILE",
                        help="build in GET_JIT_SIZE mode and write the "
                             "measured instruction-size table to FILE")
    parser.add_argument("tests", nargs="*",
                        help="check/*.tst names (default: all base tests)")
    args = parser.parse_args()
    targets = list(TARGETS) if args.target == "all" else [args.target]
    if args.measure_sizes and len(targets) != 1:
        parser.error("--measure-sizes needs a single --target")

    global LIGHTNING
    names = args.tests or base_tests()
    with tempfile.TemporaryDirectory(prefix="tang-psx-lightning-") as temporary:
        work = Path(temporary)
        LIGHTNING = lightning_source.prepare(work / "gnu-lightning")
        code_name = code_names()
        with ThreadPoolExecutor(max_workers=len(targets)) as pool:
            outcomes = list(pool.map(
                lambda target: run_target(target, args, names, code_name,
                                          work / target), targets))
    for report, _ in outcomes:
        print("\n".join(report))
    return 0 if all(ok for _, ok in outcomes) else 1


if __name__ == "__main__":
    raise SystemExit(main())
