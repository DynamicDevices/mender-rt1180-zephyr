# Mender OTA on NXP i.MX RT1180 (Zephyr)

Public repo: https://github.com/DynamicDevices/mender-rt1180-zephyr

Public overlay for [mender-mcu-integration](https://github.com/mendersoftware/mender-mcu-integration): RT118x CM33 board configuration (EVK + FRDM-IMXRT1186), build/flash notes, and Hosted Mender bring-up documentation.

West dependencies (`zephyr/`, `modules/`, `bootloader/`) are **not** in this repository. Clone upstream, apply this overlay, then run `west update`.

## Quick start (fresh workspace)

```bash
mkdir mender-rt1180 && cd mender-rt1180

# Upstream reference application + West manifest
git clone https://github.com/mendersoftware/mender-mcu-integration.git mender-mcu-integration

# RT1180 port files from this repo
git clone https://github.com/DynamicDevices/mender-rt1180-zephyr.git _rt1180
cp -a _rt1180/mender-mcu-integration/. mender-mcu-integration/
rm -rf _rt1180

west init -l mender-mcu-integration
west update
```

Full procedure (SDK, secrets, build, flash, Mender): **[mender-mcu-integration/PROJECT-NOTES.md](mender-mcu-integration/PROJECT-NOTES.md)**.

## Simulator testing (Phase 0b)

**Status: COMPLETE (2026-06-13).** Hosted Mender validation without RT1180 hardware: Zephyr `native_sim` with the noop-update module (no MCUboot, no `zephyr-image` OTA). Verified end-to-end: build, TAP/DHCP/NAT, accept pending device, noop deployment.

From the West workspace root (after `west update` and local `mender-local.conf` — see PROJECT-NOTES):

| Terminal | Command |
|----------|---------|
| 1 (leave running) | `sudo ./scripts/run-native-sim-network.sh start` |
| 2 | `./scripts/build-native-sim.sh` |
| 2 | `./scripts/test-mender-native-sim.sh --run-only` |
| 2 (after accept in UI) | `./scripts/create-native-sim-deployment.sh` |

First check-in may log HTTP **401** until the pending device is accepted in Hosted Mender. After accept, **"No deployment available"** confirms tenant-token auth.

**vemu (nRF5340):** `./scripts/test-vemu.sh` — CPU/UART sanity only. Freeware vemu has no network stack; it does **not** substitute for Phase 0b Hosted Mender check-in.

Troubleshooting (401 pending accept, 409 overlapping deploys, network hang): **[PROJECT-NOTES — Phase 0b](mender-mcu-integration/PROJECT-NOTES.md#phase-0b--native_sim-smoke-test-no-evk)**.


## FRDM-IMXRT1186 (CM33)

NXP board page: [FRDM-IMXRT1186](https://www.nxp.com/design/design-center/development-boards-and-designs/FRDM-IMXRT1186). Same RT118x family as the **MIMXRT1180-EVK** (dual CM33/CM7, NETC Ethernet, MCUboot external flash), but Freedom form factor with on-board **MCU-Link** (no separate debug probe). Zephyr board: `frdm_imxrt1186/mimxrt1186/cm33`. Mender **device type**: `frdm_imxrt1186`.

**Requires Zephyr v4.4.0+** (`frdm_imxrt1186` is not in v4.2). After pulling this repo, run `west update` from the West workspace root.

Prerequisites: `mender-mcu-integration/mender-local.conf`, Zephyr SDK per [PROJECT-NOTES — Prerequisites](mender-mcu-integration/PROJECT-NOTES.md#prerequisites). Full EVK vs FRDM table: [PROJECT-NOTES](mender-mcu-integration/PROJECT-NOTES.md#frdm-imxrt1186-vs-mimxrt1180-evk).

From the **West workspace root** (this repo when used as the top-level checkout):

| Step | Command |
|------|---------|
| Update Zephyr | `west update` |
| Sysbuild (MCUboot + Mender app) | `./scripts/build-rt1186-frdm.sh` |
| Incremental rebuild | `./scripts/build-rt1186-frdm.sh --incremental` |
| Flash CM33 | `west flash -d build-frdm-rt1186` |
| Serial console | MCU-Link USB, **115200 8N1** (LPUART1) |
| Upload OTA artifact | `./scripts/create-rt1186-frdm-deployment.sh` |

**Hardware (Phase 1):** for CM33 debug/flash, set jumper **J60** to **1:OFF 2:OFF 3:ON** ([Zephyr board doc](https://docs.zephyrproject.org/latest/boards/nxp/frdm_imxrt1186/doc/index.html)). Connect a **1 Gbps** Ethernet cable to a TSN switch port (`swp0` or `swp2` per upstream doc); expect DHCP on those interfaces. Lab static Mender group: **`rt1180-lab`** (shared name with EVK — use `device_type` or per-device deploy targets so EVK and FRDM artifacts are not mixed).

Manual build (equivalent to the script):

```bash
west build -p --sysbuild \
  -b frdm_imxrt1186/mimxrt1186/cm33 \
  -d build-frdm-rt1186 \
  mender-mcu-integration \
  -- \
  -DEXTRA_CONF_FILE=mender-mcu-integration/mender-local.conf \
  -DCONFIG_MENDER_ARTIFACT_NAME=dev-1
```


## Hardware bringup (Phase 1+)

**Status: TBD on hardware.** EVK pending arrival; **FRDM-IMXRT1186** build/deploy scripts and board conf are in-tree (Zephyr ≥ v4.4). Phase 0b (`native_sim`) is complete; **Phase 1+ on physical RT118x boards has not been completed on the bench.** Flash, Ethernet, Hosted Mender OTA, and CM7 phases will be run when the board is available. See **[PROJECT-NOTES — Phase 1](mender-mcu-integration/PROJECT-NOTES.md#phase-1--evk-flash-cm33-mender-image)**.

When lab hardware is available, use **`scripts/create-rt1180-deployment.sh`** (EVK) or **`scripts/create-rt1186-frdm-deployment.sh`** (FRDM) (static group **`rt1180-lab`**) to upload `zephyr.mender` — see [PROJECT-NOTES — rt1180-lab](mender-mcu-integration/PROJECT-NOTES.md#device-groups--mimxrt1180_evk--rt1180-lab).

**Production security:** on-die **EdgeLock (ELE)** key storage is a project requirement for RT118x — lab uses NVS today; phased roadmap (S0–S4) in [PROJECT-NOTES — Security / EdgeLock](mender-mcu-integration/PROJECT-NOTES.md#security--edgelock).

**EU Cyber Resilience Act (CRA):** technical gap analysis and Mender/Zephyr mapping (not legal compliance sign-off) — [PROJECT-NOTES — CRA technical mapping](mender-mcu-integration/PROJECT-NOTES.md#cyber-resilience-act-cra--technical-mapping).

## What this repo contains

| Path | Purpose |
|------|---------|
| `mender-mcu-integration/PROJECT-NOTES.md` | RT1180 workspace, build, flash, and OTA notes |
| `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | EVK board Kconfig fragment |
| `mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf` | FRDM board Kconfig fragment |
| `mender-mcu-integration/west.yml` | West manifest (Zephyr v4.4.0 + mender-mcu; FRDM board) |
| `mender-mcu-integration/README.md` | Pointer to PROJECT-NOTES for RT1180 |
| `mender-mcu-integration/.gitignore` | Local secrets and build paths |
| `scripts/` | Host helpers — Phase 0b: `build-native-sim.sh`, `run-native-sim-network.sh`, `test-mender-native-sim.sh`, `create-native-sim-deployment.sh`; RT118x CM33: `build-rt1180-evk.sh`, `build-rt1186-frdm.sh`, `create-rt1180-deployment.sh`, `create-rt1186-frdm-deployment.sh`; vemu: `test-vemu.sh` — see [PROJECT-NOTES — Scripts inventory](mender-mcu-integration/PROJECT-NOTES.md#scripts-inventory) |

## Secrets

Create `mender-mcu-integration/mender-local.conf` locally (gitignored). Never commit tenant tokens or PATs. See PROJECT-NOTES.
