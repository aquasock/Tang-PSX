#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package, upload, and run AE350 programs through the Gate 1 ROM loader.

  pack <program.bin> -o <image.tpx> [--load ADDR] [--entry ADDR]
      Prefix a flat binary with the loader header (software/common/tpx_api.h).
  upload <image.tpx> [--remote PATH]
      Copy an image to the SD card. Tang-Control only accepts this from the
      TangCore main menu, before the Gate 1 core is loaded.
  run <remote> [--reset] [--detach] [--timeout SECONDS]
      With the Gate 1 core running, optionally restart the AE350, stream the
      image from the SD card, and wait for the program's result. --detach
      returns after the image starts and is intended for persistent programs.
  status
      Show loader state, stream counters, result registers, and the log.
  blob -o <file> / blob-crc
      Write the blob program's deterministic data, or print its expected result.

Uses Tang-Control's tangctl.py (TANG_CONTROL_DIR, default ../Tang-Control).
"""

import argparse
import contextlib
import io
import os
import re
import struct
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TANG_CONTROL = Path(os.environ.get("TANG_CONTROL_DIR", ROOT.parent / "Tang-Control"))
sys.path.insert(0, str(TANG_CONTROL / "scripts"))
import tangctl  # noqa: E402

IMAGE_MAGIC = 0x31495054
HEADER_SIZE = 32
DEFAULT_LOAD = 0x42000000
REMOTE_DIR = "tpx"

RESET_ADDRESS = 0x100
STATE_NAMES = {
    0x00: "boot", 0x01: "wait", 0x02: "receive", 0x03: "run", 0x04: "returned",
    0x81: "error: bad header", 0x82: "error: bad load range", 0x83: "error: truncated",
    0x84: "error: length mismatch", 0x85: "error: CRC mismatch", 0x86: "error: cancelled",
    0x87: "error: stream overflow",
}
RESULT_REGISTERS = [
    ("stage", 0x08), ("failure", 0x0c), ("words", 0x10), ("checksum", 0x14),
    ("jit", 0x18), ("cycles", 0x1c), ("features", 0x20),
    ("fail_address", 0xc0), ("fail_expected", 0xc4), ("fail_observed", 0xc8),
]
PROFILE_REGISTERS = [
    ("profile_cpu_ms", 0x110), ("profile_gpu_ms", 0x114),
    ("profile_accel_ms", 0x118), ("profile_sync_ms", 0x11c),
    ("profile_display_ms", 0x120),
]


BLOB_BYTES = 48 * 1024


def blob_data():
    """Deterministic xorshift32 bytes embedded by software/programs/blob."""
    state, out = 0x9E3779B9, bytearray()
    while len(out) < BLOB_BYTES:
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        out += state.to_bytes(4, "little")
    return bytes(out)


def pack(payload, load, entry):
    header = struct.pack("<8I", IMAGE_MAGIC, HEADER_SIZE, load, entry, len(payload),
                         zlib.crc32(payload), 0, 0)
    return header + payload


def quiet(port, command, timeout=5):
    with contextlib.redirect_stdout(io.StringIO()):
        return tangctl.run_command(port, command, timeout=timeout)


def peek(port, address, count=1):
    lines = quiet(port, f"peek 0x{address:08x} {count}")
    return [int(line.split(":")[1], 16) for line in lines]


def poke(port, address, value):
    quiet(port, f"poke 0x{address:08x} 0x{value:08x}")


def describe_state(value):
    return f"{STATE_NAMES.get(value & 0xff, hex(value & 0xff))} (runs {value >> 16})"


def read_log(port):
    head = peek(port, 0x30)[0]
    ring = b"".join(word.to_bytes(4, "little") for word in peek(port, 0x40, 32))
    if head <= len(ring):
        text = ring[:head]
    else:
        start = head % len(ring)
        text = ring[start:] + ring[:start]
    return text.decode("ascii", "replace")


def print_status(port):
    state, size, crc, result = peek(port, 0xd0, 4)
    sessions, stream_bytes, ends, cancels, overflow = peek(port, 0xe0, 5)
    video = peek(port, 0xf4)[0]
    print(f"loader     {describe_state(state)}")
    print(f"image      {size} bytes, crc32 {crc:08x}, result 0x{result:08x}")
    print(f"stream     sessions {sessions}, bytes {stream_bytes}, ends {ends}, "
          f"cancels {cancels}, overflow {overflow}")
    print(f"video      enabled {video & 1}, underflow {(video >> 1) & 1}")
    values = peek(port, 0x08, 7) + peek(port, 0xc0, 3)
    print("registers  " + " ".join(f"{name}={value:08x}"
                                   for (name, _), value in zip(RESULT_REGISTERS, values)))
    values = peek(port, 0x110, len(PROFILE_REGISTERS))
    print("profile    " + " ".join(f"{name}={value}"
                                  for (name, _), value in
                                  zip(PROFILE_REGISTERS, values)))
    print("log        " + repr(read_log(port)))


def wait_state(port, predicate, timeout):
    deadline = time.monotonic() + timeout
    while True:
        state = peek(port, 0xd0)[0]
        if predicate(state):
            return state
        if time.monotonic() > deadline:
            raise TimeoutError(f"loader state stayed {describe_state(state)}")
        time.sleep(0.05)


def command_pack(args):
    payload = Path(args.binary).read_bytes()
    entry = args.load if args.entry is None else args.entry
    image = pack(payload, args.load, entry)
    Path(args.output).write_bytes(image)
    print(f"{args.output}: {len(payload)} payload bytes, load 0x{args.load:08x}, "
          f"entry 0x{entry:08x}, crc32 {zlib.crc32(payload):08x}")
    return 0


def command_upload(args, port):
    remote = args.remote or f"{REMOTE_DIR}/{Path(args.image).name}"
    with contextlib.suppress(RuntimeError):
        quiet(port, f"mkdir {REMOTE_DIR}")
    tangctl.run_put(port, args.image, remote)
    return 0


def command_run(args, port):
    remote = args.remote if "/" in args.remote else f"{REMOTE_DIR}/{args.remote}"
    if args.reset:
        poke(port, RESET_ADDRESS, 1)
        # The self-checks run first; streamed data waits in the FPGA FIFO meanwhile.
        wait_state(port, lambda s: s == 0x00000001, args.timeout)
    before = peek(port, 0xd0)[0]
    started = time.monotonic()
    for line in quiet(port, f"stream {remote}", timeout=600):
        match = re.search(r"STREAM bytes=(\d+) ms=(\d+) crc32=([0-9a-f]+)", line)
        if match:
            size, ms = int(match.group(1)), int(match.group(2))
            rate = size / (ms / 1000) / 1024 if ms else 0
            print(f"streamed   {remote}: {size} bytes in {ms} ms ({rate:.1f} KiB/s)")
    if args.detach:
        print(f"started    {time.monotonic() - started:.2f} s after stream start")
        print_status(port)
        return 0
    state = wait_state(port,
        lambda s: s != before and (s & 0xff) not in (0x02, 0x03), args.timeout)
    print(f"finished   {time.monotonic() - started:.2f} s after stream start")
    print_status(port)
    return 0 if (state & 0xff) == 0x04 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial device; auto-detected when omitted")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("pack")
    p.add_argument("binary")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--load", type=lambda v: int(v, 0), default=DEFAULT_LOAD)
    p.add_argument("--entry", type=lambda v: int(v, 0))
    p = sub.add_parser("upload")
    p.add_argument("image")
    p.add_argument("--remote", help=f"SD path (default {REMOTE_DIR}/<image name>)")
    p = sub.add_parser("run")
    p.add_argument("remote", help=f"SD path, or a name under {REMOTE_DIR}/")
    p.add_argument("--reset", action="store_true", help="restart the AE350 first")
    p.add_argument("--detach", action="store_true",
                   help="return after starting a persistent program")
    p.add_argument("--timeout", type=float, default=30.0)
    sub.add_parser("status")
    p = sub.add_parser("blob")
    p.add_argument("-o", "--output", required=True)
    sub.add_parser("blob-crc")
    args = parser.parse_args()

    if args.command == "pack":
        return command_pack(args)
    if args.command == "blob":
        Path(args.output).write_bytes(blob_data())
        return 0
    if args.command == "blob-crc":
        print(f"0x{zlib.crc32(blob_data()):08x}")
        return 0
    port = tangctl.open_port(args.port or tangctl.find_port())
    try:
        if args.command == "upload":
            return command_upload(args, port)
        if args.command == "run":
            return command_run(args, port)
        print_status(port)
        return 0
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
