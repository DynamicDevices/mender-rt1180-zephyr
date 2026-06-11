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

## What this repo contains

| Path | Purpose |
|------|---------|
| `mender-mcu-integration/PROJECT-NOTES.md` | RT1180 workspace, build, flash, and OTA notes |
| `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | Board Kconfig fragment (NETC Ethernet, MCUboot, Mender storage) |
| `mender-mcu-integration/west.yml` | West manifest (Zephyr v4.2.0 + mender-mcu) |
| `mender-mcu-integration/README.md` | Pointer to PROJECT-NOTES for RT1180 |
| `mender-mcu-integration/.gitignore` | Local secrets and build paths |

## Secrets

Create `mender-mcu-integration/mender-local.conf` locally (gitignored). Never commit tenant tokens or PATs. See PROJECT-NOTES.
