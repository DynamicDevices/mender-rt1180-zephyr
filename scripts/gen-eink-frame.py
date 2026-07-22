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
RGB_PALETTE = [
    (0, 0, 0),
    (255, 255, 255),
    (255, 255, 0),
    (255, 0, 0),
    (0, 0, 255),
    (0, 255, 0),
]
PALETTE_CODES = [0x0, 0x1, 0x2, 0x3, 0x5, 0x6]

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

def fill_image(path: Path, crop: bool = False, rotate_to_panel: bool = False) -> bytes:
    """Scale an ordinary image and quantize it to packed Spectra-6 colours.

    Default: EXIF-correct, then centre-crop/pad to upright 1200x1600.
    Pass rotate_to_panel=True for Spectra6 / eink-scheduler-rust (90° CW when
    source aspect disagrees with the portrait panel).
    """
    try:
        from PIL import Image, ImageOps
    except ImportError as exc:
        raise SystemExit(
            "Pillow (PIL) is required for --image; install with: pip install pillow"
        ) from exc

    with Image.open(path) as source:
        source = ImageOps.exif_transpose(source)
        source = source.convert("RGB")
        if rotate_to_panel and (source.width > source.height) != (WIDTH > HEIGHT):
            # image crate rotate90 is clockwise; PIL ROTATE_90 is counter-clockwise.
            source = source.transpose(Image.Transpose.ROTATE_270)
        if crop or not rotate_to_panel:
            source = ImageOps.fit(
                source, (WIDTH, HEIGHT), method=Image.Resampling.LANCZOS,
            )
        else:
            source = ImageOps.pad(
                source,
                (WIDTH, HEIGHT),
                method=Image.Resampling.LANCZOS,
                color=(255, 255, 255),
            )

        palette = Image.new("P", (1, 1))
        flat_palette = [channel for rgb in RGB_PALETTE for channel in rgb]
        palette.putpalette(flat_palette + [0] * (768 - len(flat_palette)))
        quantized = source.quantize(
            palette=palette,
            dither=Image.Dither.FLOYDSTEINBERG,
        )
        indices = quantized.tobytes()

    halves = bytearray(PAYLOAD)
    for y in range(HEIGHT):
        row = y * WIDTH
        for half in range(2):
            src = row + half * 600
            dst = half * HALF + y * 300
            for xb in range(300):
                left = PALETTE_CODES[indices[src + xb * 2]]
                right = PALETTE_CODES[indices[src + xb * 2 + 1]]
                halves[dst + xb] = (left << 4) | right
    return bytes(halves)

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
    g.add_argument("--image", type=Path, help="JPEG/PNG input to scale and quantize")
    ap.add_argument(
        "--crop",
        action="store_true",
        help="centre-crop image to 1200x1600 instead of padding white",
    )
    ap.add_argument(
        "--rotate-to-panel",
        dest="rotate_to_panel",
        action="store_true",
        default=False,
        help="90° CW when source aspect disagrees with portrait panel",
    )
    ap.add_argument(
        "--no-rotate-to-panel",
        dest="rotate_to_panel",
        action="store_false",
        help="centre-crop/pad upright into portrait (default)",
    )
    ap.add_argument("--orientation", type=int, default=0)
    args = ap.parse_args()
    if args.solid:
        payload = fill_solid(COLORS[args.solid])
    elif args.lr:
        payload = fill_lr(COLORS[args.lr[0]], COLORS[args.lr[1]])
    elif args.image:
        payload = fill_image(args.image, crop=args.crop, rotate_to_panel=args.rotate_to_panel)
    else:
        payload = fill_bars()
    out = Path(args.output)
    out.write_bytes(build(payload, args.orientation))
    print(f"wrote {out} ({out.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
