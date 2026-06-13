# Mender OTA on NXP i.MX RT1180 (Zephyr)

Public repo: https://github.com/DynamicDevices/mender-rt1180-zephyr

Public overlay for [mender-mcu-integration](https://github.com/mendersoftware/mender-mcu-integration): RT1180 EVK (CM33) board configuration, build/flash notes, and Hosted Mender bring-up documentation.

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

## Hardware bringup (Phase 1+)

**Status: TBD — pending MIMXRT1180-EVK arrival.** Phase 0b (`native_sim`) is complete; **Phase 1+ on actual RT1180 EVK hardware has not started.** Flash, Ethernet, Hosted Mender OTA, and CM7 phases will be run when the board is available. See **[PROJECT-NOTES — Phase 1](mender-mcu-integration/PROJECT-NOTES.md#phase-1--evk-flash-cm33-mender-image)**.

## What this repo contains

| Path | Purpose |
|------|---------|
| `mender-mcu-integration/PROJECT-NOTES.md` | RT1180 workspace, build, flash, and OTA notes |
| `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | Board Kconfig fragment (NETC Ethernet, MCUboot, Mender storage) |
| `mender-mcu-integration/west.yml` | West manifest (Zephyr v4.2.0 + mender-mcu) |
| `mender-mcu-integration/README.md` | Pointer to PROJECT-NOTES for RT1180 |
| `mender-mcu-integration/.gitignore` | Local secrets and build paths |
| `scripts/` | Host helpers — `build-native-sim.sh`, `run-native-sim-network.sh`, `test-mender-native-sim.sh`, `create-native-sim-deployment.sh`, `test-vemu.sh` (see [Phase 0b](mender-mcu-integration/PROJECT-NOTES.md#phase-0b--native_sim-smoke-test-no-evk)) |

## Secrets

Create `mender-mcu-integration/mender-local.conf` locally (gitignored). Never commit tenant tokens or PATs. See PROJECT-NOTES.
