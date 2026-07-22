# LZ4 v1.9.4 (https://github.com/lz4/lz4) — BSD 2-Clause

Vendored `lib/` sources used by `CONFIG_APP_EINK_LZ4` to expand LZ4-*framed*
ES6F downloads on device (`eink_lz4.c`).

Wire contract for the parallel server/bridge work:
- Body is a standard LZ4 frame (magic `04 22 4D 18`), as from `lz4 -f`.
- Decompressed payload is a complete ES6F v1 file (960032 bytes).
- Raw ES6F remains accepted; JPEG/PNG remain rejected.
- Helper: `scripts/eink-lz4-wrap.py`
