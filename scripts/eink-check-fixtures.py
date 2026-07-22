#!/usr/bin/env python3
import struct
import zlib
from pathlib import Path

for p in Path("/tmp/eink-zephyr/images").glob("*.es6f"):
    data = p.read_bytes()
    assert len(data) == 32 + 960000, (p, len(data))
    magic, ver, w, h, pf, ori, flags, plen, crc = struct.unpack_from("<IHHHBBHII", data, 0)
    assert magic == 0x46365345 and ver == 1 and w == 1200 and h == 1600
    assert plen == 960000
    assert zlib.crc32(data[32:]) & 0xFFFFFFFF == crc
    print(f"OK fixture {p.name}")
