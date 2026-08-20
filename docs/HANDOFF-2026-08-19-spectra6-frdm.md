# Handoff — Spectra6 on FRDM-1186 (Zephyr)

**Lane:** `spectra6-frdm`  
**Worktree:** `/data_drive/dd/zephyr-rt1170-eink-spectra6-frdm`  
**Branch:** `feat/frdm-gpc-wait`  
**Primary `main`:** do not use — sibling `frdm-ocram-enroll` exists  

Paste this into a new chat if you want a clean lane. Do not share the primary checkout.

## Progress

- **Target:** same Spectra 6 (EL133UF1) as Jaguar, on **FRDM-IMXRT1186** Zephyr — not Yocto. Jaguar Linux (`eink-spectra6` + `eink-scheduler-rust`) stays the behaviour SoT.
- **Portal loop (already in tree):** `eink_http.c` / `eink_scheduler.c` = Jaguar e-tabelone `GET /node/v0/device/{id}/config` + image GET + `POST telemetry` + cron. Device wants **ES6F** (JPEG/PNG rejected). **`native_sim` live TAP is the portal proof.**
- **cJSON / Mender heap:** `cjson_arena_leave` restores `mender_malloc`/`mender_free` (was `cJSON_InitHooks(NULL)` → libc free on mender blocks) and suspends `mender_work_queue` during arena parse. UART soak ~55s green after rebuild/flash.
- **FRDM = one board:** Spectra6 SPI **and** Hosted **Mender MCU** over **NETC**. Not Foundries. Not MIMXRT1180-EVK unless we add an EVK loom. FRDM **has** Ethernet.
- **No PMU MCU** (no Jaguar MCXC). Rails/sleep = RT GPIOs + BBNSM (`eink_power.*`). Field sleep = **BOM** (main 3V3 off; BBSM stays up). FRDM lab HV is external; FRDM does not make ±16 V.
- **Pin contract:** [docs/FRDM-IMXRT1186-EL133-PIN-CONTRACT.md](FRDM-IMXRT1186-EL133-PIN-CONTRACT.md) — LPSPI2 J3-18/16, GPIO D2–D5+D8; strap BS[1:0]=01 (not EVK quad).
- **Build:** `./scripts/build-rt1186-frdm-eink.sh`. Overlay `frdm_imxrt1186_mimxrt1186_cm33_eink_el133.{overlay,conf}`. e-tabelone **HTTP off** on this image; Mender client **on**.
- **BOM lab image (committed `60a92cd`):** `BOM_POWER_LOOP=1 ./scripts/build-rt1186-frdm-eink.sh` → settle → TOSP+RTC → POR or soft-retry. GPC **STOP**+SLEEPDEEP. App RAM was ~**7.9%** of 8 MB on the BOM image.
- **Also on branch:** LittleFS/heap telemetry (`a7f490d`), portal UI handoff docs (`efe2f78`).
- **Renode:** hello_world on this `.repl` **PASS** historically. This Mender+eink ELF: **LPUART Robot FAIL** (no Booting/Hello on tester; open). **BOM PC-path PASS** (Gemba 2026-08-20): BBNSM CTRL write `0x2` + sticky-read after settle = `bbnsm_rtc_start()` / BOM `enter_snvs`. Helper: `./scripts/renode-frdm-eink-bom-pc-smoke.sh`. NETC still Tags. Portal TAP = `native_sim`.

## Blockers

1. No spare panel + 60-pin FFC on the bench (do not unplug live Jaguar).
2. Hosted Mender enroll + slot swap **not** FRDM-silicon proven; Renode cannot prove it.
3. Renode **LPUART banner** for this e-ink ELF still dark — do not claim UART PASS; use PC-path smoke for BOM until fixed.
4. e-tabelone HTTP still off on FRDM image (re-enable is a later choice).
5. **Worktree collision (2026-08-20):** another shell pristine-rebuilt this same dir (`frdm-http-enable-build*`) mid-lane and wiped the BOM ELF. One-writer-per-tree — serialize HTTP experiments or use a sibling worktree.

## Next

1. Portal: assign schedule/asset for screen `B1EF425C3B305DADE90FBB2D10211000` → device re-sync (paint when spare glass exists).
2. `native_sim` TAP soak if portal work continues without board.
3. Loom + BUSY/RESET smoke on FRDM when spare glass exists (`FRDM` proof).
4. FRDM Ethernet DHCP → Hosted Pending → Accept → `zephyr.mender` swap (`FRDM` proof).
5. Optional: chase Renode LPUART TX for this ELF (separate from BOM PC-path).

**Proof classes:** `native_sim` = portal/Mender noop. `renode` UART ≠ `renode` PC-path ≠ FRDM ≠ Spectra6 ≠ Hosted OTA.
