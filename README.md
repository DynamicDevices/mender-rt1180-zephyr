# Zephyr RT1170 e-ink (Active ESL) + RT118x Mender overlay

Public repo: https://github.com/DynamicDevices/zephyr-rt1170-eink  
Device agent: **zephyr-rt1170-eink** · MemPalace handoff lanes: **`zephyr-rt1170-eink` ↔ `cloud-eink`** (match workspace/agent names). Channel discovery: [LANE-CHANNEL.md](mender-mcu-integration/docs/LANE-CHANNEL.md) → MemPalace `LANE_DIRECTORY`.

## About

Zephyr firmware for Active ESL on **MIMXRT1170-EVK** (CM7) — e-ink display, SoC UID identity, Etablone `/node/v2` sync — plus the legacy **RT118x** Mender MCU OTA overlay (**MIMXRT1180-EVK**, **FRDM-IMXRT1186** CM33), `native_sim` lab validation, and EdgeLock / EU Cyber Resilience Act (CRA) programme tracking. Engineering programme and milestones: [PROJECT-NOTES](mender-mcu-integration/PROJECT-NOTES.md) · tracker [issue #1](https://github.com/DynamicDevices/zephyr-rt1170-eink/issues/1).

Public overlay for [mender-mcu-integration](https://github.com/mendersoftware/mender-mcu-integration): RT118x CM33 board configuration (EVK + FRDM-IMXRT1186), build/flash notes, and Hosted Mender bring-up documentation.

West dependencies (`zephyr/`, `modules/`, `bootloader/`) are **not** in this repository. Clone upstream, apply this overlay, then run `west update`.

## Prerequisites (this overlay)

| Requirement | Notes |
|-------------|-------|
| **Zephyr SDK 1.0.1** | Required for Zephyr **v4.4.0** — download [`zephyr-sdk-1.0.1_*_gnu.tar.xz`](https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v1.0.1), run `setup.sh -c -t arm-zephyr-eabi`, set `ZEPHYR_SDK_INSTALL_DIR`. |
| **mender-mcu fork** | [`west.yml`](mender-mcu-integration/west.yml) pins [`DynamicDevices/mender-mcu`](https://github.com/DynamicDevices/mender-mcu) branch `feature/zephyr-4.4-mbedtls4` @ **`1b2d374`** (Mbed TLS 4.x `tls.c` + Zephyr 4.2/4.4 version guards in storage/update-module). Run `west update` after pulling manifest changes. |

Toolchain details, expected CMake warnings, secrets, and full build/flash procedure: **[mender-mcu-integration/PROJECT-NOTES.md](mender-mcu-integration/PROJECT-NOTES.md)**.

## Quick start (fresh workspace)

```bash
mkdir zephyr-rt1170-eink && cd zephyr-rt1170-eink

# Upstream reference application + West manifest
git clone https://github.com/mendersoftware/mender-mcu-integration.git mender-mcu-integration

# Overlay from this repo
git clone https://github.com/DynamicDevices/zephyr-rt1170-eink.git _overlay
cp -a _overlay/mender-mcu-integration/. mender-mcu-integration/
rm -rf _overlay

west init -l mender-mcu-integration
west update
```

Full procedure (SDK, secrets, build, flash, Mender): **[mender-mcu-integration/PROJECT-NOTES.md](mender-mcu-integration/PROJECT-NOTES.md)**.


## West build directories

Use a **separate** build directory per target (do not point EVK and FRDM at the same `-d` path — CMake caches board-specific configuration).

| Target | Default `BUILD_DIR` | Build | Flash | Deploy |
|--------|---------------------|-------|-------|--------|
| MIMXRT1180-EVK CM33 | `build-rt1180-evk` | `./scripts/build-rt1180-evk.sh` | `west flash -d build-rt1180-evk` | `./scripts/create-rt1180-deployment.sh` |
| FRDM-IMXRT1186 CM33 | `build-frdm-rt1186` | `./scripts/build-rt1186-frdm.sh` | `west flash -d build-frdm-rt1186` | `./scripts/create-rt1186-frdm-deployment.sh` |
| `native_sim` (Phase 0b) | `build-native_sim` | `./scripts/build-native-sim.sh` | N/A (run `zephyr.exe`) | `./scripts/create-native-sim-deployment.sh` |
| MIMXRT1170-EVK CM7 | `build-rt1170-evk` | `./scripts/build-rt1170-evk.sh` | `west flash -d build-rt1170-evk` | `./scripts/create-rt1170-deployment.sh` |
| MIMXRT1170-EVKB CM7 + IW612 Improv | `build-rt1170-improv-iw612` | `./scripts/build-rt1170-improv-iw612.sh` | `west flash -d build-rt1170-improv-iw612` | Hardware validation TBD |
| `native_sim` + Improv (serial, emulated) | `build-native_sim-improv` | `./scripts/build-native-sim-improv.sh` | N/A (run `zephyr.exe`) | `./scripts/create-native-sim-deployment.sh` |
| `native_sim` + Improv (BLE, emulated) | `build-native_sim-improv-ble` | `./scripts/build-native-sim-improv-ble.sh` | N/A (run `zephyr.exe --bt-dev=hciN`) | `./scripts/create-native-sim-deployment.sh` |
| `native_sim` + e-ink (dummy_dc; optional SDL) | `build-native_sim-eink` | `./scripts/build-native-sim-eink.sh` / `eink-verify-sim.sh` | N/A (`eink show <es6f>`) | N/A |

If you previously used the legacy shared `build/` directory, remove it before rebuilding: `rm -rf build`.

## Simulator testing (Phase 0b)

**Status: COMPLETE (2026-06-13; re-verified 2026-06-17 @ mender-mcu `1b2d374`).** Hosted Mender validation without RT118x hardware: Zephyr `native_sim` with the noop-update module (no MCUboot, no `zephyr-image` OTA). Verified end-to-end: build, TAP/DHCP/NAT, accept pending device, noop deployment.

From the West workspace root (after `west update` and local `mender-local.conf` — see PROJECT-NOTES):

| Terminal | Command |
|----------|---------|
| 1 (leave running) | `./scripts/run-native-sim-network.sh start` (after one-time `sudo ./scripts/setup-native-sim-tap.sh install`) |
| 2 | `./scripts/build-native-sim.sh` |
| 2 | `./scripts/test-mender-native-sim.sh --run-only` |
| 2 (after accept in UI) | `./scripts/create-native-sim-deployment.sh` |

First check-in may log HTTP **401** until the pending device is accepted in Hosted Mender. After accept, **"No deployment available"** confirms tenant-token auth.

**vemu (nRF5340):** `./scripts/test-vemu.sh` — CPU/UART sanity only. Freeware vemu has no network stack; it does **not** substitute for Phase 0b Hosted Mender check-in.

Troubleshooting (401 pending accept, 409 overlapping deploys, network hang): **[PROJECT-NOTES — Phase 0b](mender-mcu-integration/PROJECT-NOTES.md#phase-0b--native_sim-smoke-test-no-evk)**.


## FRDM-IMXRT1186 (CM33)

NXP board page: [FRDM-IMXRT1186](https://www.nxp.com/design/design-center/development-boards-and-designs/FRDM-IMXRT1186). Same RT118x family as the **MIMXRT1180-EVK** (dual CM33/CM7, NETC Ethernet, MCUboot external flash), but Freedom form factor with on-board **MCU-Link** (no separate debug probe). Zephyr board: `frdm_imxrt1186/mimxrt1186/cm33`. Mender **device type**: `frdm_imxrt1186`.

**Requires Zephyr v4.4.0+** and **Zephyr SDK 1.0.1** (`frdm_imxrt1186` is not in v4.2). After pulling this repo, run `west update` from the West workspace root (manifest pins mender-mcu fork @ `1b2d374`).

**Host build verified (2026-06-17 @ `1b2d374`):** sysbuild links `zephyr.elf` and produces `zephyr.mender`; hardware flash/Ethernet/Mender OTA **TBD**.

Prerequisites: `mender-mcu-integration/mender-local.conf`, SDK per [PROJECT-NOTES — Prerequisites](mender-mcu-integration/PROJECT-NOTES.md#prerequisites). Expected CMake warnings: [PROJECT-NOTES — Build warnings](mender-mcu-integration/PROJECT-NOTES.md#expected-build-warnings). Full EVK vs FRDM table: [PROJECT-NOTES](mender-mcu-integration/PROJECT-NOTES.md#frdm-imxrt1186-vs-mimxrt1180-evk).

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

**Device identity:** board overlays fix locally-administered MACs so Mender does not re-register every boot (EVK **02:11:80:00:00:01**, FRDM **02:11:86:00:00:01** on `swp0`). See [PROJECT-NOTES — Device identity](mender-mcu-integration/PROJECT-NOTES.md#device-identity-stable-mac). Production: EdgeLock per-unit identity (S2 roadmap), not shared lab MACs.

Manual build (equivalent to the script):

```bash
west build -p --sysbuild \
  -b frdm_imxrt1186/mimxrt1186/cm33 \
  -d build-frdm-rt1186 \
  mender-mcu-integration \
  -- \
  -DEXTRA_CONF_FILE=mender-local.conf \
  -DCONFIG_MENDER_ARTIFACT_NAME=dev-1
```




## MIMXRT1170-EVK (Cortex-M7)

Zephyr board: `mimxrt1170_evk/mimxrt1176/cm7` (default revision **B**). Mender **device type**: `mimxrt1170_evk`.

This path is the **standard MIMXRT1170-EVK** (MIMXRT1176 silicon) for Mender MCU OTA bring-up. It does **not** include NXP EdgeReady **RT117H/F** face/gesture runtime libraries — those SKUs need NXP’s EdgeReady SDK/licence flow outside this overlay.

**Host build verified** with Zephyr v4.4.0 + SDK 1.0.1 + mender-mcu `@1dbc35b`. Hardware flash/Ethernet/OTA is **TBD** until an EVK is on the bench.

| Step | Command |
|------|---------|
| Sysbuild (MCUboot + Mender app) | `./scripts/build-rt1170-evk.sh` |
| Incremental rebuild | `./scripts/build-rt1170-evk.sh --incremental` |
| Flash CM7 | `west flash -d build-rt1170-evk` |
| Serial console | On-board debug USB, **115200 8N1** (LPUART1) |
| Upload OTA artifact | `./scripts/create-rt1170-deployment.sh` |

Lab identity MAC: **02:11:70:00:00:01** on the 10/100 ENET iface. Lab group: **`rt1170-lab`**.

### Improv Wi-Fi provisioning (IW612)

An optional host-build-verified target adds Improv over BLE for an **Embedded
Artists 2EL M.2 module (NXP IW612)**:

```bash
west update
west blobs fetch hal_nxp
./scripts/build-rt1170-improv-iw612.sh
```

Advertises as **`eink-1170`** with an Active ESL claim redirect URL so the
Active ESL app can onboard it as `imx93-jaguar-eink` (lab spoof). The build pins `improv-zephyr` at `b65d2aaa39d48ec0fee11aa3eecd0609bbe30f3c`,
starts BLE provisioning before Mender waits for IPv4, persists Wi-Fi credentials
in Zephyr settings/NVS, and emits the normal signed image plus
`zephyr.mender`. The EVKB M.2 Wi-Fi path shares USDHC1 with the SD-card socket,
so do not use an SD card with this target.

**Status:** sysbuild and Mender artifact generation pass; EVKB + 2EL radio,
SDIO, BLE provisioning, reconnect, and OTA remain **hardware-unverified**.
Follow the NXP/Embedded Artists module wiring and EVKB hardware-rework guidance.
The current Improv BLE transport has no pairing or physical-presence gate, so
this configuration is for lab evaluation only—not production provisioning.

### Improv in the emulator (`native_sim`)

You can exercise the Improv provisioning flow **without hardware** on
`native_sim`. Zephyr has no emulated Wi-Fi radio, so a small management-only
simulated Wi-Fi interface (`CONFIG_APP_WIFI_SIM`, `src/net/wifi_sim.c`) stands in
for one: on "connect" it reports association success and starts DHCPv4 on the
native TAP Ethernet interface, which carries the Mender traffic. The result is a
genuine loop — **Improv provisioning → network up over TAP → Mender connects** —
using the real pinned `improv-zephyr` protocol/serial code.

```bash
west update                                   # fetch improv-zephyr
sudo ./scripts/setup-native-sim-tap.sh install  # once per machine (user-owned zeth)
./scripts/run-native-sim-network.sh start       # no sudo after install (terminal 1)
./scripts/build-native-sim-improv.sh          # build (terminal 2)
./build-native_sim-improv/zephyr/zephyr.exe   # run; prints "connected to pseudotty: /dev/pts/N"
# terminal 3 — drive provisioning over that PTY (Chrome Web Serial cannot see a PTY):
python3 scripts/improv-serial-provision.py /dev/pts/N --ssid <SSID> --psk <8+ char PSK>
```

The device transitions `AUTHORIZED → PROVISIONING → PROVISIONED`; DHCPv4 then
runs on `zeth0` and the Mender client authenticates against Hosted Mender. Only
the radio association itself is stubbed — SDIO enumeration, IW612 firmware load,
and real scan/associate remain hardware-only. `CONFIG_APP_WIFI_SIM` must **never**
be enabled on real hardware.

#### Over BLE instead of serial

The same emulated loop runs over **BLE** — provisioned from a phone Improv app
or Chrome Web Bluetooth ([improv-wifi.com/ble](https://www.improv-wifi.com/ble/))
instead of a serial PTY. `native_sim` has no radio of its own, so it borrows the
**host's** Bluetooth adapter through Zephyr's HCI User Channel driver. Two
consequences, both kernel-imposed:

* the sim **takes the adapter away from BlueZ** while it runs (your desktop
  Bluetooth — mice/headphones — drops until the sim exits), and
* opening the user-channel socket needs **`CAP_NET_ADMIN`** (sudo, or a one-off
  `setcap` on the built binary) — unlike the TAP owner trick, this cannot be
  avoided.

```bash
west update
sudo ./scripts/setup-native-sim-tap.sh install   # once per machine (user-owned zeth)
./scripts/run-native-sim-network.sh start        # data path (terminal 1)
./scripts/build-native-sim-improv-ble.sh         # build (terminal 2)
# optional: grant the capability once so the run needs no sudo (re-run after each build)
./scripts/run-native-sim-improv-ble.sh setcap
./scripts/run-native-sim-improv-ble.sh           # powers down hci0, advertises "eink-51F0"
```

The target **spoofs an Active ESL e-ink board**: it advertises as **`eink-51F0`**
(so the Active ESL Flutter app's `eink-XXXX` filter accepts it) and on success
returns a claim redirect URL
`https://active-esl-onboard.active-esl.workers.dev?ip_address={ip}&token={token}`
with a freshly minted token — same contract as the real e-ink
`onboarding-server.py`. Open the Active ESL app (or
[improv-wifi.com/ble](https://www.improv-wifi.com/ble/)), connect to
**eink-51F0**, submit Wi-Fi credentials, and complete claim as `imx93-jaguar-eink`
/ board id `51F0`. Wi-Fi association is still simulated; DHCPv4 runs on `zeth0`.
No pairing / physical-presence gate — lab only. Do **not** claim into a
production tenant you care about without cleaning up afterwards.


### E-ink display / scheduler (`native_sim`)

Simulator-first port of the Linux Spectra-6 / scheduler contracts (C). See
[docs/EINK-CONTRACT.md](mender-mcu-integration/docs/EINK-CONTRACT.md) and
[PROJECT-NOTES — E-ink](mender-mcu-integration/PROJECT-NOTES.md#e-ink-display-and-scheduler-zephyr).

**Headless (CI / default):** `dummy_dc` on 32-bit `native_sim` — no SDL.

```bash
./scripts/build-native-sim-eink.sh
./scripts/gen-eink-frame.py --lr red blue -o /tmp/eink-zephyr/images/lr.es6f
./build-native_sim-eink/zephyr/zephyr.exe
# shell: eink show /tmp/eink-zephyr/images/lr.es6f
```

**Visual SDL window:** use 64-bit `native_sim/native/64` so the host amd64 SDL2 links
(the default 32-bit NSI build cannot). Needs `DISPLAY` / X11.

```bash
./scripts/build-native-sim-eink-sdl.sh
./build-native_sim-eink-sdl/zephyr/zephyr.exe
# shell: eink show /tmp/eink-zephyr/images/lr.es6f
# window title: "Zephyr EL133UF1 sim" (1200×1600 @ 25% zoom)
```

**Live e-tabelone schedule:** the development service currently publishes
JPEG/PNG assets, while production firmware deliberately accepts packed ES6F
only. A simulator-only host bridge converts those assets and leaves the
production boundary unchanged:

```bash
./scripts/run-native-sim-etabelone.sh <device_id>        # headless proof
./scripts/run-native-sim-etabelone.sh <device_id> --sdl  # visual scheduled image
```

The one-command run fetches the live config, selects the current scheduled
job, converts/downloads only that due image, streams it into `dummy_dc`/SDL,
and posts telemetry back to e-tabelone.


## Hardware bringup (Phase 1+)

**Status: TBD on hardware.** EVK pending arrival; **MIMXRT1180-EVK** and **FRDM-IMXRT1186** host sysbuild verified @ mender-mcu `1b2d374` (Zephyr v4.4.0, SDK 1.0.1). Phase 0b (`native_sim`) is complete; **Phase 1+ on physical RT118x boards has not been completed on the bench.** Flash, Ethernet, Hosted Mender OTA, and CM7 phases will be run when the board is available. See **[PROJECT-NOTES — Phase 1](mender-mcu-integration/PROJECT-NOTES.md#phase-1--evk-flash-cm33-mender-image)** and **[Upstream contribution](mender-mcu-integration/PROJECT-NOTES.md#upstream-contribution)**.

When lab hardware is available, use **`scripts/create-rt1180-deployment.sh`** (EVK) or **`scripts/create-rt1186-frdm-deployment.sh`** (FRDM) (static group **`rt1180-lab`**) to upload `zephyr.mender` — see [PROJECT-NOTES — rt1180-lab](mender-mcu-integration/PROJECT-NOTES.md#device-groups--mimxrt1180_evk--rt1180-lab).

**Production security:** on-die **EdgeLock (ELE)** key storage is a project requirement for RT118x — lab uses NVS today; phased roadmap (S0–S4) in [PROJECT-NOTES — Security / EdgeLock](mender-mcu-integration/PROJECT-NOTES.md#security--edgelock).

**EU Cyber Resilience Act (CRA):** technical gap analysis — [CRA technical mapping](mender-mcu-integration/PROJECT-NOTES.md#cyber-resilience-act-cra--technical-mapping); unified delivery plan — **[CRA compliance programme](mender-mcu-integration/PROJECT-NOTES.md#cra-compliance-programme-firmware-technical-baseline)** (engineering plan, not legal sign-off). Open milestones: [GitHub issue tracker #1](https://github.com/DynamicDevices/zephyr-rt1170-eink/issues/1).

## What this repo contains

| Path | Purpose |
|------|---------|
| `mender-mcu-integration/PROJECT-NOTES.md` | RT118x workspace, build, flash, and OTA notes |
| `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | EVK board Kconfig fragment |
| `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.overlay` | EVK stable lab MAC (NETC) |
| `mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf` | FRDM board Kconfig fragment |
| `mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.overlay` | FRDM stable lab MAC (NETC) |
| `mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7.conf` | RT1170 EVK board Kconfig fragment |
| `mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7.overlay` | RT1170 EVK stable lab MAC (ENET) |
| `mender-mcu-integration/boards/*_improv_iw612.conf` / `.overlay` | RT1170-EVKB IW612 Wi-Fi/BLE Improv configuration and USDHC1 mapping |
| `mender-mcu-integration/improv-native-sim.conf` | `native_sim` Improv (serial) + simulated Wi-Fi Kconfig fragment |
| `mender-mcu-integration/improv-native-sim-ble.conf` | `native_sim` Improv (BLE) + simulated Wi-Fi Kconfig fragment (host HCI User Channel) |
| `mender-mcu-integration/patches/improv-zephyr-active-esl-claim.patch` | Local `improv-zephyr` patch: `{token}` claim URL, larger RPC buffer, RPC write-without-response |
| `mender-mcu-integration/src/net/wifi_sim.c` | Management-only simulated Wi-Fi driver for `native_sim` (`CONFIG_APP_WIFI_SIM`) |
| `mender-mcu-integration/west.yml` | West manifest (Zephyr v4.4.0 + mender-mcu fork @ `1b2d374`; FRDM board) |
| `mender-mcu-integration/README.md` | Pointer to PROJECT-NOTES for RT118x |
| `mender-mcu-integration/.gitignore` | Local secrets and build paths |
| `scripts/` | Host helpers — Phase 0b: `build-native-sim.sh`, `run-native-sim-network.sh`, `test-mender-native-sim.sh`, `create-native-sim-deployment.sh`; RT118x CM33: `build-rt1180-evk.sh`, `build-rt1186-frdm.sh`, `create-rt1180-deployment.sh`, `create-rt1186-frdm-deployment.sh`; RT1170: `build-rt1170-evk.sh`, `build-rt1170-improv-iw612.sh`, `create-rt1170-deployment.sh`; Improv emulator: `build-native-sim-improv.sh`, `improv-serial-provision.py`, `build-native-sim-improv-ble.sh`, `run-native-sim-improv-ble.sh`, `setup-native-sim-tap.sh`, `native-sim-tap/`; CRA WS3: `generate-sbom.sh`; vemu: `test-vemu.sh` — see [PROJECT-NOTES — Scripts inventory](mender-mcu-integration/PROJECT-NOTES.md#scripts-inventory) |

## Secrets

Create `mender-mcu-integration/mender-local.conf` locally (gitignored). Never commit tenant tokens or PATs. See PROJECT-NOTES.
