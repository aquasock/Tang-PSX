#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read and decode the standalone DDR3 vendor-controller test over Tang-Control.

Uses Tang-Control's tangctl.py (TANG_CONTROL_DIR, default ../Tang-Control) to
peek the diagnostic register file of build/ddr3-vendor/tang-psx-ddr3-*.bin.
"""

import argparse
import contextlib
import io
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TANG_CONTROL = Path(os.environ.get("TANG_CONTROL_DIR", ROOT.parent / "Tang-Control"))
sys.path.insert(0, str(TANG_CONTROL / "scripts"))
import tangctl  # noqa: E402

MAGIC = 0x54504433
UI_CLK_HZ = 100_000_000
BOARD_CLK_HZ = 50_000_000
BURST_BYTES = 32
ARRAY_BURSTS = 1 << 25

REGISTERS = [
    "magic", "abi", "live_flags", "tester_flags", "passes", "error_passes",
    "progress", "error_bursts", "error_mask", "first_error_burst",
    "first_error_pass", "first_error_mask", "last_error_burst",
    "write_cycles", "read_cycles", "verified_bursts", "stall_events",
    "heartbeat", "snapshots", "calib_time", "uptime", "crc_errors",
    "bad_requests",
]
PHASES = {0: "idle", 1: "write", 2: "read"}


def peek(port, address, count):
    with contextlib.redirect_stdout(io.StringIO()):
        lines = tangctl.run_command(port, f"peek 0x{address:08x} {count}")
    return [int(line.split(":")[1], 16) for line in lines]


def byte_lanes(mask):
    """Byte i of the 256-bit word is DQ lane i % 4 in beat i // 4."""
    lanes = sorted({i % 4 for i in range(32) if mask >> i & 1})
    beats = sorted({i // 4 for i in range(32) if mask >> i & 1})
    return lanes, beats


def words(values):
    return " ".join(f"{v:08x}" for v in reversed(values))


def report(port):
    r = dict(zip(REGISTERS, peek(port, 0x00, len(REGISTERS))))
    if r["magic"] != MAGIC:
        print(f"unexpected magic 0x{r['magic']:08x}; is tang-psx-ddr3 loaded?")
        return 1
    live, flags = r["live_flags"], r["tester_flags"]
    print(f"abi               {r['abi'] >> 16}.{r['abi'] & 0xffff}")
    print(f"uptime (mod 85.9) {r['uptime'] / BOARD_CLK_HZ:.2f} s")
    print(f"pll_lock          {live >> 1 & 1}")
    print(f"calib_complete    {live >> 2 & 1}   ddr_rst {live >> 3 & 1}")
    if live >> 4 & 1:
        print(f"calib_time        {r['calib_time'] / BOARD_CLK_HZ * 1e3:.3f} ms")
    print(f"snapshots         {r['snapshots']}   ui_clk heartbeat {r['heartbeat']}")
    print(f"phase             {PHASES.get(flags & 3, flags & 3)}"
          f"   progress {r['progress']}/{ARRAY_BURSTS}   complement {flags >> 8 & 1}")
    print(f"passes            {r['passes']}   with errors {r['error_passes']}")
    # The 32-bit burst, uptime, and heartbeat counters wrap (128 GiB, 85.9 s,
    # 42.9 s); coverage is derived from the pass counter instead.
    print(f"coverage          {r['passes'] * ARRAY_BURSTS * BURST_BYTES / 2**30:.0f} GiB"
          f" in completed passes")
    if r["write_cycles"] and r["read_cycles"]:
        size = ARRAY_BURSTS * BURST_BYTES
        print(f"last sweep        write {size / (r['write_cycles'] / UI_CLK_HZ) / 1e6:.0f} MB/s,"
              f" read {size / (r['read_cycles'] / UI_CLK_HZ) / 1e6:.0f} MB/s")
    print(f"read stalls       {r['stall_events']}")
    print(f"error bursts      {r['error_bursts']}   byte mask 0x{r['error_mask']:08x}")
    if flags >> 12 & 1:
        lanes, beats = byte_lanes(r["first_error_mask"])
        first = r["first_error_burst"]
        print(f"first error       burst {first} (addr 0x{first * 8:07x}, byte 0x{first * BURST_BYTES:08x})"
              f" pass {r['first_error_pass']} mask 0x{r['first_error_mask']:08x}"
              f" lanes {lanes} beats {beats}")
        expected = peek(port, 0x80, 8)
        observed = peek(port, 0xa0, 8)
        print(f"  expected        {words(expected)}")
        print(f"  observed        {words(observed)}")
        print(f"  xor             {words([e ^ o for e, o in zip(expected, observed)])}")
        print(f"last error burst  {r['last_error_burst']}")
    print(f"transport         crc_errors {r['crc_errors']}   bad_requests {r['bad_requests']}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial device; auto-detected when omitted")
    parser.add_argument("--watch", type=float, metavar="SECONDS",
        help="repeat the report at this interval")
    args = parser.parse_args()

    port = tangctl.open_port(args.port or tangctl.find_port())
    try:
        while True:
            status = report(port)
            if not args.watch:
                return status
            print()
            time.sleep(args.watch)
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
