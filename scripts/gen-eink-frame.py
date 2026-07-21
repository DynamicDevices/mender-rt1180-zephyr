#!/usr/bin/env python3
"""Generate ES6F v1 packed Spectra-6 frames for Zephyr e-ink."""
from __future__ import annotations
import argparse
import struct
import zlib
from pathlib import Path

WIDTH, HEIGHT = 1200, 1600
HALF = 480_000
PAYLOAD = 960_000
MAGIC = b"ES6F"
COLORS = {
    "black": 0x0, "white": 0x1, "yellow": 0x2, "red": 0x3, "blue": 0x5, "green": 0x6,
}

def pack_nibble(c: int) -> int:
    c &= 0xF
    return (c << 4) | c

def fill_solid(color: int) -> bytes:
    return bytes([pack_nibble(color)]) * PAYLOAD

def fill_lr(left: int, right: int) -> bytes:
    return bytes([pack_nibble(left)]) * HALF + bytes([pack_nibble(right)]) * HALF

def fill_bars() -> bytes:
    # vertical bars across full width in each half
    row_bytes = 300  # 600 px / 2
    left = bytearray()
    right = bytearray()
    palette = [0, 1, 2, 3, 5, 6]
    for y in range(HEIGHT):
        for half, out in ((0, left), (1, right)):
            for xb in range(row_bytes):
                x = xb * 2
                idx = (x * len(palette)) // 600
                out.append(pack_nibble(palette[idx % len(palette)]))
    return bytes(left + right)

def build(payload: bytes, orientation: int = 0) -> bytes:
    assert len(payload) == PAYLOAD
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    hdr = struct.pack(
        "<IHHHBBHII10x",
        int.from_bytes(MAGIC, "little"),
        1,
        WIDTH,
        HEIGHT,
        1,  # pixel format
        orientation,
        0,
        PAYLOAD,
        crc,
    )
    assert len(hdr) == 32
    return hdr + payload

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--solid", choices=COLORS.keys())
    g.add_argument("--lr", nargs=2, metavar=("LEFT", "RIGHT"), choices=COLORS.keys())
    g.add_argument("--bars", action="store_true")
    ap.add_argument("--orientation", type=int, default=0)
    args = ap.parse_args()
    if args.solid:
        payload = fill_solid(COLORS[args.solid])
    elif args.lr:
        payload = fill_lr(COLORS[args.lr[0]], COLORS[args.lr[1]])
    else:
        payload = fill_bars()
    out = Path(args.output)
    out.write_bytes(build(payload, args.orientation))
    print(f"wrote {out} ({out.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
