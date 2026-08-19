#!/usr/bin/env python3
"""Print likely Cortex-M VTOR/SP/PC from an NXP XIP Zephyr ELF."""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def iter_load_segments(data: bytes):
    """Yield (vaddr, blob) for every PT_LOAD. MCUboot slot0 apps put the
    vector table in a small rom_start LOAD, not the large .text LOAD."""
    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (
            p_type,
            p_offset,
            p_vaddr,
            _p_paddr,
            p_filesz,
            _p_memsz,
            _p_flags,
            _p_align,
        ) = struct.unpack_from("<IIIIIIII", data, off)
        if p_type != 1 or p_filesz < 8:
            continue
        yield p_vaddr, data[p_offset : p_offset + p_filesz]


def ram_ok(sp: int) -> bool:
    if sp & 3:
        return False
    return (
        0x20000000 <= sp < 0x30000000
        or 0x38000000 <= sp < 0x40000000
        or 0x80000000 <= sp < 0xA0000000
    )


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} zephyr.elf")
    data = Path(sys.argv[1]).read_bytes()
    entry = struct.unpack_from("<I", data, 24)[0]
    needle = struct.pack("<I", entry)
    found = []
    for base, blob in iter_load_segments(data):
        idx = 0
        while True:
            j = blob.find(needle, idx)
            if j < 0:
                break
            if j >= 4:
                sp = struct.unpack_from("<I", blob, j - 4)[0]
                vtor = base + j - 4
                found.append((ram_ok(sp), vtor, sp, entry & ~1, base))
            idx = j + 1
    if not found:
        raise SystemExit(f"entry {entry:#x} not found as vector[1]")
    found.sort(key=lambda t: (not t[0], t[1]))
    print(f"entry={entry:#x}")
    for ok, vtor, sp, pc, _base in found[:6]:
        mark = "RAM" if ok else "skip"
        print(f"  {mark} VTOR={vtor:#x} SP={sp:#x} PC={pc:#x}")


if __name__ == "__main__":
    main()
