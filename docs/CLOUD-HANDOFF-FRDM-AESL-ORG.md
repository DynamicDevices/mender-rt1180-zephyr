# Cloud handoff — FRDM screen under AESL org (etablone-dev)

**From:** device lane (`zephyr-rt1186-eink` / `feat/frdm-gpc-wait`)  
**To:** Cloud API implementation (`/data_drive/esl/etablone-cloud`)  
**Date:** 2026-08-21  
**Portal:** `https://etablone-dev.active-esl.com` (dev / OTP)  
**Why not here:** device-side rule — no edits/deploy under `etablone-cloud`.

## Ask

This screen **already exists** on the portal. Ensure it is under the **AESL** (Active-Edge) organisation on the **dev** portal — do not invent a second device.

## Device identity (SoT — existing)

| Field | Value |
|-------|--------|
| Display name | **FRDM-RT1186 lab** |
| `device_id` | `B1EF425C3B305DADE90FBB2D10211000` (uppercase SoC UID hex) |
| Location | **London** |
| Preferred format | `es6f` |
| Geometry | 13.3″ EL133 — 1200×1600 (existing ES6F / LZ4 path) |
| Proven sync | etablone-dev LZ4 expand + show/refresh green (2026-08-20/21) |

## Desired cloud / portal actions

1. Find existing screen **FRDM-RT1186 lab** / `B1EF425C3B305DADE90FBB2D10211000` on etablone-dev.
2. Ensure it sits under org **AESL** / **Active-Edge** (move membership if it is under another org). Keep name **FRDM-RT1186 lab** and location **London**.
3. Do **not** create a duplicate UID. If a move requires token rotate, return the new Bearer (tokens are not portable).
4. Confirm schedule + warm ES6F.LZ4 still attached (`blockSizeID=4`).
5. Return to device lane only if token changed (Bitwarden / file, **not chat**):
   - `ETABLONE_BASE=https://etablone-dev.active-esl.com`
   - `ETABLONE_DEVICE_ID=B1EF425C3B305DADE90FBB2D10211000`
   - `ETABLONE_DEVICE_TOKEN=<Bearer>` (if rotated)
   - org id + console deep-link
6. Confirm visible under AESL in operator console on etablone-dev.

## Device lane follow-up (after you land this)

On FRDM HTTP image (`FRDM_EINK_HTTP=1`), after DHCP:

```text
eink creds https://etablone-dev.active-esl.com B1EF425C3B305DADE90FBB2D10211000 <token>
eink sync
```

Expect: plan download → LZ4 expand → store accepted → (optional glass) show/refresh.

## Out of scope (device lane)

- Editing or deploying `etablone-cloud`
- Committing device tokens
- T2000 / 25″ model type (separate handoff: `docs/CLOUD-HANDOFF-T2000-MODEL.md`)
