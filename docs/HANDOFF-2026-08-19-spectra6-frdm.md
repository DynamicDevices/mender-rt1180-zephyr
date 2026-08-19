# Handoff — Spectra6 on FRDM-1186 (Zephyr)

**Lane:** `spectra6-frdm` (`LANE.id` = `zephyr-rt1170-eink`)  
**Worktree:** `/data_drive/dd/zephyr-rt1170-eink-spectra6-frdm`  
**Branch:** `feat/frdm-imxrt1186-el133` @ `09d3cde` (ahead of `origin/main` by 3, **not pushed**)  
**Primary `main`:** do not use — sibling `frdm-ocram-enroll` exists  

## Progress

- **FRDM Hosted enroll (silicon):** MCU-Link `WGUPS4RWFPGOT`. DHCP `192.168.2.16`. Device `2880a639-b593-4d6b-8b18-02d29ac14d3c` **accepted**, group `rt1180-lab`, type `frdm_imxrt1186`. This **overwrote the F1/Ocre image** on that board.
- **FRDM OTA slot swap (silicon):** rebuilt `build-frdm-rt1186-eink` as artifact **`dev-2`**, uploaded, deployed **this UUID only**. Hosted deployment `69212139-9ace-4ff9-86dd-ebb09ca5e9ea` **finished / success:1**. Serial: download `dev-2` → install → reboot → MCUboot **Swap type: test** / **swap using move** → confirm (`rollback canceled`) → `deployment_status_cb: success`. Inventory `artifact_name=dev-2`. Still pingable `192.168.2.16`.
- **Why it boots:** MCUboot DTCM overlay (HyperRAM/FlexSPI1 made slot0 `-ENODEV`); DSA DHCP on user ports; continue without panel; skip scheduler when HTTP off. Commit `09d3cde`. Tenant token is gitignored `mender-local.conf` — not in git.
- **Still true:** Jaguar Linux is behaviour SoT. Portal TAP = `native_sim`. Renode UART PASS ≠ FRDM ≠ Spectra6. Pin contract: [FRDM-IMXRT1186-EL133-PIN-CONTRACT.md](FRDM-IMXRT1186-EL133-PIN-CONTRACT.md). e-tabelone HTTP **off** on this image.
- Untracked leftover: `scripts/pydev/rt118x_trdc.py` (not in the `.repl`).
- Footgun: `scripts/create-rt1180-deployment.sh` `set -o pipefail` + `mender-artifact read | awk` → SIGPIPE 141; UUID deploy was `mender-cli artifacts upload` + curl.

## Blockers

1. No spare panel + 60-pin FFC (do not unplug live Jaguar).
2. `eink_scheduler_init()` hangs if HTTP is on (not root-caused).

## Next

1. Restore F1/Ocre on `WGUPS4RWFPGOT` if that lane needs the board back.
2. Loom + BUSY/RESET when spare glass exists.
3. Product full loop on **RT1170** when that board is the bench — no MCXC.

**Proof:** `native_sim` = portal/Mender noop. `renode` ≠ FRDM. Enroll + **slot swap = FRDM**. Spectra6 paint / RT1170 = not yet.
