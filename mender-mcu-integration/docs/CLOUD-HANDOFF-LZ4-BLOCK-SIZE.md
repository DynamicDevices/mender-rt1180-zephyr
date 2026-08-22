# Cloud handoff — LZ4 frame block size for Zephyr devices

**From:** device lane (`zephyr-rt1186-eink` / `feat/frdm-gpc-wait`)  
**To:** Cloud API implementation (`/data_drive/esl/etablone-cloud`)  
**Date:** 2026-08-20  
**Priority:** ~~blocks FRDM LZ4 expand / show~~ — **closed 2026-08-21**
(expand + `refresh done result=0`; no heap grow)

## Problem

FRDM `eink sync` against etablone-dev:

```text
downloaded …es6f.download (3824 bytes)
LZ4 frame detected — expanding
eink_lz4: LZ4F decompress: ERROR_allocation_failed @off=0
```

Frame inspection on the served `.es6f.lz4`:

| Field | Value |
|-------|--------|
| magic | `04 22 4D 18` |
| blockSizeID | **7 (max 4 MiB)** |
| raw ES6F | ~960 KiB |

`LZ4F_decompress` mallocs scratch ≈ block size. Device keeps a **~96 KiB**
system heap on purpose — do **not** ask firmware to grow a multi‑MiB heap.

Source: `src/lz4frame.ts` → `compressEs6fLz4Frame` → `lz4js.compress(es6f)`
(default large blocks). Derivatives land in R2 as `frame.es6f.lz4`.

## Implement in etablone-cloud

1. **`compressEs6fLz4Frame`:** emit **≤64 KiB** independent blocks
   (prefer `blockSizeID=4`). If `lz4js` cannot set block size, switch compressor
   or post-process with a tool that can (`lz4 --block-size=64K`, python
   `lz4.frame` + `BLOCKSIZE_MAX64KB`, etc.).
2. **Regenerate** stored `r2_key_es6f_lz4` / `lz4_byte_size` (or invalidate so
   next serve recompresses). Old 4 MiB–block objects must not stay sticky.
3. **Test:** assert served frame `block_size <= 65536` (or document 256 KiB max
   if you deliberately pick ID=5).
4. **Docs:** note device LZ4 limit in OpenAPI / `BOARD-API-COMPAT.md`.

## Out of scope (device lane)

- Inflating `CONFIG_HEAP_MEM_POOL_SIZE` to ~6 MiB (rejected).
- Editing this tree from the cloud lane for unrelated work.

## Verify

1. `GET` any device `.es6f.lz4` — `block_size_id` is 4 (64 KiB), not 7.  
   **Cloud:** etablone-dev @ `fc4cda7` (PR #15+#16); warmed asset
   `f50a3f20-1e63-44fe-9b04-1ead111e8d4d` → header `04 22 4d 18 40 40 c0`, 4020 B.
2. FRDM (HTTP image, ~96 KiB heap): after DHCP, `eink sync` → LZ4 expand OK →
   `show job` / `refresh done` (glass optional for expand proof).  
   **Device expand (2026-08-20):** FRDM `feat/frdm-gpc-wait` @ `232abc5`, screen
   `B1EF425C3B305DADE90FBB2D10211000` — download 4020 B → expand 960032 B →
   store accepted → sync `ret=0`; **no** `ERROR_allocation_failed`; heap unchanged.  
   **Follow-up (2026-08-21):** `eink_disp` stack overflow fixed — EL133
   `stream_write` 4 KiB chunk moved to BSS (`el133_data`); `EINK_DISP_STACK_SIZE`
   4→8 KiB. FRDM prove: `show job` → `refresh done result=0` (no USAGE FAULT).

## Device status when you land this

- Screen `B1EF425C3B305DADE90FBB2D10211000` already has schedule + SDL fixture.
- FRDM Ethernet sync/plan/download + **LZ4 expand** + **show/refresh** green
  vs blockSizeID=4 cloud (glass optional; SPI stream path proven).
