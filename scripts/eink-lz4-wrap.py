#!/usr/bin/env python3
"""Wrap a raw ES6F into an LZ4 frame for device download tests.

Wire contract (matches firmware eink_lz4.h):
  - Output is a standard LZ4 *frame* (magic 04 22 4D 18), same as `lz4 -f`.
  - Decompressed payload must be a complete ES6F v1 file.

Examples:
  ./scripts/eink-lz4-wrap.py /tmp/eink-zephyr/images/white.es6f \\
      -o /tmp/eink-zephyr/images/white.es6f.lz4
  lz4 -f -12 white.es6f white.es6f.lz4   # equivalent CLI
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path, help="raw .es6f file")
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output .es6f.lz4 (default: <input>.lz4)",
    )
    ap.add_argument(
        "-1",
        "--fast",
        action="store_true",
        help="fast compression (lz4 -1); default uses -12",
    )
    args = ap.parse_args()
    src = args.input
    if not src.is_file():
        print(f"missing input: {src}", file=sys.stderr)
        return 2
    head = src.read_bytes()[:4]
    if head != b"ES6F":
        print(f"input is not ES6F (magic={head!r})", file=sys.stderr)
        return 2
    dst = args.output or Path(str(src) + ".lz4")
    lz4 = shutil.which("lz4")
    if lz4 is None:
        print("lz4 CLI not found on PATH", file=sys.stderr)
        return 2
    level = ["-1"] if args.fast else ["-12"]
    # -f overwrite, --rm not used (keep source)
    cmd = [lz4, *level, "-f", "--no-frame-crc", str(src), str(dst)]
    # Prefer frame CRC when supported; fall back without.
    try:
        subprocess.run(
            [lz4, *level, "-f", str(src), str(dst)],
            check=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as err:
        print(err.stderr.decode("utf-8", "replace"), file=sys.stderr)
        return err.returncode or 1
    raw = src.stat().st_size
    comp = dst.stat().st_size
    print(
        f"wrote {dst} ({comp} bytes, {100.0 * comp / raw:.1f}% of {raw} ES6F)",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
