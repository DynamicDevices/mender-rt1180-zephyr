# Handoff — Spectra6 on FRDM-1186 (Zephyr)

**Lane:** `spectra6-frdm`  
**Worktree:** `/data_drive/dd/zephyr-rt1170-eink-spectra6-frdm`  
**Branch:** `feat/frdm-imxrt1186-el133` (`db04db6`, ahead of `origin/main` by 1)  
**Primary `main`:** do not use — sibling `frdm-ocram-enroll` exists  

Paste this into a new chat if you want a clean lane. Do not share the primary checkout.

Working tree leftover (untracked, not in the `.repl`): `scripts/pydev/rt118x_trdc.py` — generic TRDC stub; `rt118x_trdc{1,2,3}.py` are what Renode loads.

## Progress

- **Target:** same Spectra 6 (EL133UF1) as Jaguar, on **FRDM-IMXRT1186** Zephyr — not Yocto. Jaguar Linux (`eink-spectra6` + `eink-scheduler-rust`) stays the behaviour SoT.
- **Portal loop (already in tree):** `eink_http.c` / `eink_scheduler.c` = Jaguar e-tabelone `GET /node/v0/device/{id}/config` + image GET + `POST telemetry` + cron. Device wants **ES6F** (JPEG/PNG rejected). **`native_sim` live TAP is the portal proof.**
- **FRDM = one board:** Spectra6 SPI **and** Hosted **Mender MCU** over **NETC**. Not Foundries. Not MIMXRT1180-EVK unless we add an EVK loom. FRDM **has** Ethernet.
- **No PMU MCU** (no Jaguar MCXC). Rails/sleep = RT GPIOs + SNVS/SRTC (`eink_power.*`). FRDM lab HV is external; FRDM does not make ±16 V.
- **Pin contract:** [docs/FRDM-IMXRT1186-EL133-PIN-CONTRACT.md](FRDM-IMXRT1186-EL133-PIN-CONTRACT.md) — LPSPI2 J3-18/16, GPIO D2–D5+D8; strap BS[1:0]=01 (not EVK quad).
- **Build:** `./scripts/build-rt1186-frdm-eink.sh`. Overlay `frdm_imxrt1186_mimxrt1186_cm33_eink_el133.{overlay,conf}`. e-tabelone **HTTP off** on this image; Mender client **on**; net-buffer shrink **removed** after RAM fix.
- **RAM bugfix:** `eink_display.c` had `static uint32_t frame[1600*1200]` (7.5 MiB SDL) on hardware. Now POSIX-only. App RAM ~95% → **~4.3%** of 8 MB.
- **Renode:** harness in `tests/renode/` + `./scripts/renode-frdm-eink-uart.sh`. hello_world on this `.repl` **PASS**. This Mender+eink ELF UART **PASS** (`Booting Zephyr OS` on LPUART1, ~7 s). Models added: BLK_CTRL_WAKEUPMIX + NETC PRIV/IERB RAM, TRDC HWCFG0/DACFG, FlexSPI idle + self-clear `MCR0.SWRESET`. TAP exists in Renode but **NETC is Tags** — no Hosted/portal over TAP for this ELF. Portal TAP = `native_sim`.

## Blockers

1. No spare panel + 60-pin FFC on the bench (do not unplug live Jaguar).
2. Hosted Mender enroll + slot swap **not** FRDM-silicon proven; Renode cannot prove it.
3. e-tabelone HTTP still off on FRDM image (RAM was the old reason; SDL frame gone — re-enable is a later choice).

## Next

1. Loom + BUSY/RESET smoke on FRDM when spare glass exists (`FRDM` proof).
2. FRDM Ethernet DHCP → Hosted Pending → Accept → `zephyr.mender` swap (`FRDM` proof).
3. Product full loop (wake → portal → paint → SNVS) on **RT1170** when that board is the bench — no MCXC.

**Proof classes:** `native_sim` = portal/Mender noop. `renode` ≠ FRDM ≠ Spectra6 ≠ Hosted OTA.
