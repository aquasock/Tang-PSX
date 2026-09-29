#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Produce a GNU Lightning source tree with the Tang-PSX RV32 port applied.

The submodule stays at its pinned upstream commit; this exports that commit
and applies third_party/patches/gnu-lightning-rv32.patch.
"""

from __future__ import annotations

import argparse
import subprocess
import tarfile
import io
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = ROOT / "third_party/gnu-lightning"
PATCH = ROOT / "third_party/patches/gnu-lightning-rv32.patch"


def prepare(destination: Path) -> Path:
    """Export the pinned commit to destination and apply the RV32 patch."""
    destination.mkdir(parents=True, exist_ok=True)
    archive = subprocess.run(["git", "-C", str(SUBMODULE), "archive", "HEAD"],
                             check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(destination, filter="data")
    subprocess.run(["git", "apply", "--directory=", str(PATCH)],
                   cwd=destination, check=True)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    prepare(parser.parse_args().destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
