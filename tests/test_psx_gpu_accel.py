#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verilate the fabric GPU and compare it with the portable renderer.

    python3 tests/test_psx_gpu_accel.py [--read-latency CLOCKS]
        [--trace FILE --first VBLANK --last VBLANK]

The memory model answers reads after CLOCKS clocks (default 1); about 30
approaches the board's DDR3 path and makes the clocks-per-pixel report
comparable with hardware. --trace replays a GPU trace recorded by
tests/psx_disc_cores_rv32.c with GPU_TRACE instead of the random primitives,
with the fabric drawing VBlanks FIRST to LAST.
"""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ("psx_gpu_reset", "psx_gpu_write_gp0", "psx_gpu_write_gp1",
          "psx_gpu_read_data", "psx_gpu_read_status", "psx_gpu_sync",
          "psx_gpu_accel_stats")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--read-latency", type=int, default=1)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--first", type=int, default=1700)
    parser.add_argument("--last", type=int, default=1760)
    parser.add_argument("--histogram", action="store_true",
                        help="report clocks spent in each rasterizer state")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="tang-psx-gpu-accel-") as temporary:
        work = Path(temporary)
        current = work / "gpu_accel.cpp"
        reference = work / "gpu_reference.cpp"
        current.write_text(
            "#define PSX_GPU_ACCEL_TEST 1\n"
            f'#include "{ROOT / "software/psx/gpu.c"}"\n')
        renames = "\n".join(f"#define {name} ref_{name[4:]}" for name in PUBLIC)
        reference.write_text(
            renames + "\n" + f'#include "{ROOT / "software/psx/gpu.c"}"\n')
        subprocess.run([
            "verilator", "--cc", "--exe", "--build", "--timing",
            "-Wno-fatal", "--top-module", "gpu_rasterizer",
            "-Mdir", str(work / "obj"),
            "-CFLAGS", f"-O2 -I{ROOT / 'software/psx'} "
                       f"-DREAD_LATENCY={args.read_latency}"
                       + (" -DSTATE_HISTOGRAM" if args.histogram else ""),
            str(ROOT / "gateware/gpu_rasterizer.sv"),
            str(ROOT / "tests/psx_gpu_accel_diff.cpp"),
            str(current), str(reference),
        ], check=True)
        replay = ([str(args.trace.resolve()), str(args.first), str(args.last)]
                  if args.trace else [])
        subprocess.run([str(work / "obj/Vgpu_rasterizer"), *replay], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
