# Cloud handoff — T2000 model type (API + portal)

**From:** FRDM / device lane (`zephyr-rt1170-eink-spectra6-frdm` `feat/frdm-gpc-wait`)  
**To:** Cloud API implementation (`etablone-cloud`)  
**Date:** 2026-08-21  
**Why not here:** device-side rule — no edits under `/data_drive/esl/etablone-cloud`.

## Device change summary

- FRDM USB host MVP for T2000 TCON (VID `0x3558`): shell `eink t2000 info|clear|fill`.
- PR: https://github.com/DynamicDevices/zephyr-rt1186-eink/pull/20
- Bench: [`docs/FRDM-T2000-BENCH-MICHAEL.md`](../../dd/zephyr-rt1170-eink-spectra6-frdm/docs/FRDM-T2000-BENCH-MICHAEL.md) (J23 flash / J63 OTG + VBUS).
- Silicon proof still pending (VBUS mod). Panel W/H from `eink t2000 info` is SoT when available.

## Desired cloud / portal actions

Today e-tabelone assumes a **single** ES6F geometry (`ES6F_WIDTH/HEIGHT = 1200×1600` for 13.3″ EL133). Add an explicit **device / panel model type** so 25″ T2000 is first-class in API + portal.

### Suggested model ids (pick / adjust)

| `model` (stable string) | Panel | Nominal geometry | Notes |
|-------------------------|-------|------------------|--------|
| `el133-13` (or keep implicit default) | 13.3″ Spectra 6 SPI | **1200×1600** | Current SoT |
| `t2000-25` | 25.3″ via T2000 USB | **confirm on silicon** | Linux SoT historically quotes **3200×1800** (Kaleido-era docs); do **not** hard-lock until `info` on real TCON |

### Wire contract (proposed)

1. **DB:** `devices.model` TEXT NOT NULL DEFAULT `'el133-13'` (migration + backfill).
2. **Admin API:** get/patch device includes `model`; validate against allowlist.
3. **Portal UI:** device settings — model dropdown (`13.3″ EL133` / `25″ T2000`); asset upload / convert path uses model geometry (scale target), not a single global 1200×1600.
4. **OpenAPI:** document `model` on device resources; note ES6F packing may stay 4-bit Spectra 6 but **canvas size is model-dependent**.
5. **Device sync (v2):** optional echo of `model` in config if firmware will key off it later (FRDM T2000 path does not consume portal assets yet — portal `show`/`sync` still deferred on device).
6. **Tests:** convert + upload fixtures for `t2000-25` size once geometry confirmed; keep `el133-13` regression green.

### Out of scope for this handoff

- FRDM firmware portal download / paint pipeline (still deferred).
- Onboard-app BLE `BoardRegistry` profile (separate product; only if Alex wants `frdm-rt1186-t2000` there too).

## Ask back

- Confirm stable `model` string (`t2000-25` OK?).
- Lock W×H from Michael’s first `eink t2000 info` (or Linux `t2000_usb` on same panel) before shipping convert at wrong size.
