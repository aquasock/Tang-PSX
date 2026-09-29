#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and run the portable PSX CPU/GTE vectors, including a mutation check."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def compile_test(directory: Path, r3000: Path) -> Path:
    output = directory / "psx_diag_host"
    subprocess.run([
        "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        f"-I{ROOT / 'software/psx'}",
        f"-I{ROOT / 'software/programs/psx_diag'}", f"-I{directory}",
        str(r3000), str(ROOT / "software/psx/gte.c"),
        str(ROOT / "software/programs/psx_diag/diag.c"),
        str(ROOT / "tests/psx_diag_host.c"), "-o", str(output),
    ], check=True)
    return output


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="tang-psx-vectors-") as temporary:
        work = Path(temporary)
        header = work / "psx_vectors.h"
        repeat = work / "psx_vectors-repeat.h"
        generator = ROOT / "tools/gen_psx_vectors.py"
        subprocess.run(["python3", str(generator), "-o", str(header)], check=True)
        subprocess.run(["python3", str(generator), "-o", str(repeat)], check=True)
        if header.read_bytes() != repeat.read_bytes():
            raise RuntimeError("vector generation is not deterministic")

        completed = subprocess.run([str(compile_test(work,
            ROOT / "software/psx/r3000.c"))], check=True, text=True,
            capture_output=True)
        print(completed.stdout, end="")

        mutant = work / "r3000-mutant.c"
        source = (ROOT / "software/psx/r3000.c").read_text()
        before = "cpu->gpr[rs] + cpu->gpr[rt], written); break;"
        after = "cpu->gpr[rs] - cpu->gpr[rt], written); break;"
        if source.count(before) != 1:
            raise RuntimeError("mutation site changed")
        mutant.write_text(source.replace(before, after))
        mutated = subprocess.run([str(compile_test(work, mutant))], text=True,
                                 capture_output=True)
        if mutated.returncode == 0:
            raise RuntimeError("arithmetic mutation escaped the vectors")
        print("Mutation check: altered ADDU was detected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
