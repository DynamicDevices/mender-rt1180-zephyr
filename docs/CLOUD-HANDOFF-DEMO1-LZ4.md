# Cloud handoff — Demo #1 LZ4 assets break FRDM sync

**From:** device lane (`zephyr-rt1186-eink` / `feat/frdm-gpc-wait`)  
**To:** Cloud API implementation (`/data_drive/esl/etablone-cloud`)  
**Date:** 2026-08-21  
**Portal:** `https://etablone-dev.active-esl.com` · org **Active ESL Ltd**  
**Why not here:** device-side rule — no edits under `etablone-cloud`.

## Ask

Demo campaign **Demo Screen #1** / schedule **Demo #1** assets fail to expand on FRDM.

## Evidence (FRDM UART + host)

| Asset | Download | Expand |
|-------|----------|--------|
| `45bbc848-4237-4121-85f0-f03240f233ac` | 650209 B | `LZ4F decompress: ERROR_decompressionFailed @off=0` → `-5` |
| `36174f58-6cfa-40be-a42e-fa57fd35cac0` | 524165 B | expand → `-28` (`ENOSPC` before device fix) |
| `0fada4c3-72fb-4cfd-855a-79650e8fc23d` | 689874 B | expand → `-28` |
| `10c68631-5335-44a6-9f89-f84744616fbf` | 668239 B | device + **host** `lz4 -d` / `lz4.frame` → `ERROR_decompressionFailed` |

Header looks like a frame (`04 22 4d 18`, `blockSizeID=4`) but **payload is corrupt** — not a Zephyr-only bug.

Known-good SDL fixture `f50a3f20-…` (~4 KiB) decompresses to 960032-byte ES6F on host and device.

## Root causes (cloud)

1. **`src/lz4frame.ts` still calls raw `lz4.compress(es6f)`** — no ≤64 KiB independent blocks. Sticky R2 `frame.es6f.lz4` objects for Demo assets were never recompressed (or are corrupt / non-ES6F payloads).
2. Demo photo LZ4 objects are **~0.5–0.7 MiB** each; FRDM eink LittleFS is **~1.75 MiB** — even with gallery skip, a single bad/oversized frame hurts.

## Desired cloud actions

1. Fix `compressEs6fLz4Frame` to emit **≤64 KiB** blocks (`blockSizeID=4`); do not leave sticky max4MiB frames.
2. **Invalidate / recompress** all Demo #1 assets under Active ESL Ltd (schedule `Demo #1` / group `039b9231-…`), especially the three IDs above.
3. Verify each `.es6f.lz4` header is `04 22 4d 18 …` with **blockSizeID=4**, and that `LZ4F_decompress` yields a valid **960032-byte ES6F**.
4. Optionally trim Demo #1 to fewer / smaller slots for MCU boards, or serve geometry-aware assets.

## Device lane (done / in progress)

- **Done:** `CONFIG_APP_EINK_HTTP_SKIP_GALLERY` + `LZ4_EXPAND_ON_DISPLAY` on FRDM HTTP image; flashed slot0.
- **Done:** Cleared device-local soak schedule (effective = 10 Demo #1 slots only).
- **Blocked on cloud:** Demo `.es6f.lz4` objects fail `lz4 -d` on host — recompress / regenerate before FRDM can paint Demo #1.
