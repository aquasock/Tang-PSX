#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the CD-ROM FIFO sequence used by PsyQ LibCD sector reads."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="tang-psx-cdrom-") as temporary:
        executable = Path(temporary) / "psx_cdrom_host"
        subprocess.run([
            "cc", "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", f"-I{ROOT / 'software/psx'}",
            str(ROOT / "software/psx/cdrom.c"),
            str(ROOT / "tests/psx_cdrom_host.c"), "-o", str(executable),
        ], check=True)
        completed = subprocess.run([str(executable)], check=True, text=True,
                                   capture_output=True)
        print(completed.stdout, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
