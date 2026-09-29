#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the software GPU with its pinned reference rasterizer.

The reference is software/psx/gpu.c at REFERENCE_REVISION, whose per-pixel
64-bit barycentric division defines the expected output bit for bit.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REFERENCE_REVISION = "8ec11cf"
PUBLIC = ("psx_gpu_reset", "psx_gpu_write_gp0", "psx_gpu_write_gp1",
          "psx_gpu_read_data", "psx_gpu_read_status")


def main() -> int:
    reference = subprocess.run(
        ["git", "-C", str(ROOT), "show",
         f"{REFERENCE_REVISION}:software/psx/gpu.c"],
        check=True, capture_output=True, text=True).stdout
    with tempfile.TemporaryDirectory(prefix="tang-psx-gpu-") as temporary:
        work = Path(temporary)
        (work / "gpu_reference.c").write_text(reference)
        executable = work / "psx_gpu_diff"
        renames = [f"-D{name}=ref_{name[4:]}" for name in PUBLIC]
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", f"-I{ROOT / 'software/psx'}"]
        subprocess.run(["cc", *flags, *renames, "-c",
                        str(work / "gpu_reference.c"),
                        "-o", str(work / "gpu_reference.o")], check=True)
        subprocess.run(["cc", *flags, str(ROOT / "tests/psx_gpu_diff.c"),
                        str(ROOT / "software/psx/gpu.c"),
                        str(work / "gpu_reference.o"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
