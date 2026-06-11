# Mender MCU OTA — i.MX RT1180 (Zephyr)

Project notes for Mender over-the-air updates on the NXP **i.MX RT1180 EVK** (`mimxrt1180_evk/mimxrt1189/cm33`). This workspace is a West checkout of the upstream [mender-mcu-integration](mender-mcu-integration/) reference app with a local RT1180 board configuration.

Upstream getting-started and native_sim instructions remain in [mender-mcu-integration/README.md](mender-mcu-integration/README.md). Mender MCU module docs: [modules/mender-mcu/README.md](modules/mender-mcu/README.md).

## Purpose

Evaluate **Mender MCU OTA** on the RT1180 CM33 core: MCUboot + swap, Ethernet bring-up, Hosted Mender client, and automatic `zephyr-image` artifact generation at build time.

**Scope (CM33 vs CM7):** RT1180 is dual-core (Cortex-M33 + Cortex-M7). This port targets **CM33 only** — Mender MCU, MCUboot, and Zephyr networking. **CM7 is a separate phase** and out of scope for initial bring-up.

## Linux on RT1180

### Verdict

**No Linux on the RT1180 SoC itself.** The chip is dual Cortex-M (M33 + M7) — a real-time MCU, not an applications-processor MPU. Linux needs an MMU and memory model RT1180 does not provide; there is no upstream or NXP kernel port that boots on this silicon.

### What NXP does not ship for RT1180

- **No imx-linux BSP** — imx-linux covers i.MX application processors (Cortex-A class: i.MX 93, 8M Plus, etc.), not RT1180.
- **No LmP / Foundries machine** — Foundries Linux factories target MPUs; RT1180 is not a valid LmP target.

NXP firmware stacks for RT1180 are **Zephyr**, **FreeRTOS**, and **MCUboot** (see [RT1180 product page](https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/i-mx-rt-crossover-mcus/i-mx-rt1180-crossover-mcu-with-arm-cortex-m7-and-cortex-m33-cores:i.MX-RT1180)).

### What "Linux support" actually means

Two distinct cases — neither is "run Linux on RT1180":

1. **Heterogeneous Multi-SoC (HMS) companion** — Linux on an adjacent MPU (**i.MX 93** or **i.MX 8M Plus**); RT1180 runs RTOS firmware (Zephyr/FreeRTOS) for the **NETC TSN switch** and real-time protocols. NXP [Real-time Edge](https://www.nxp.com/design/design-center/software/development-software/real-time-edge-software:REALTIME-EDGE-SOFTWARE) connects the pair via **DSA (Distributed Switch Architecture)**: MPU Linux gets a NETC DSA driver; RT1180 runs a service driver so `ethtool`, `tc`, and PTP/TSN config can manage switch ports from Linux. RT1180 stays an MCU.
2. **Linux host PC** — x86/ARM64 workstation for cross-build, flash, and Mender artifact tooling only.

### Mainline Linux and NETC

Mainline kernel work for RT1180 is the **NETC DSA switch driver on the MPU side** — Linux on i.MX 93/8MP manages RT1180 switch ports. It does **not** enable booting Linux on RT1180.

### Nearby Linux-capable parts (HMS pairing)

| Part | Role |
|------|------|
| i.MX 93 | Primary HMS MPU; LmP/Foundries-capable; NETC DSA on Linux |
| i.MX 8M Plus | Alternative HMS MPU in Real-time Edge reference designs |

### Why this project stays Zephyr + Mender MCU (CM33)

RT1180 OTA belongs on the MCU firmware stack: **MCUboot + Zephyr + Mender MCU** on CM33, with NETC Ethernet for Hosted Mender. That matches the hardware and NXP tooling. A companion i.MX 93 Linux image (if used in a product) would be a **separate** LmP/Mender full-system OTA path — out of scope here. Zephyr board reference: [mimxrt1180_evk](https://docs.zephyrproject.org/latest/boards/nxp/mimxrt1180_evk/doc/index.html).

## Workspace layout

West topdir is this directory (parent of `mender-mcu-integration/`). Manifest: `mender-mcu-integration/west.yml`.

```
./                          # West workspace root
├── .west/                  # West metadata
├── zephyr/                 # Zephyr v4.2.0 (west import)
├── bootloader/mcuboot/     # MCUboot (west import)
├── modules/mender-mcu/     # Mender MCU Zephyr module
├── mender-mcu-integration/ # Reference app + west manifest (git repo)
├── .tools/bin/             # Local mender-artifact + mender-cli (gitignored)
└── build/                  # Sysbuild output (gitignored)
```

Fresh checkout:

```bash
west init -l mender-mcu-integration
west update
```

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| **Zephyr SDK 0.17.4** | `arm-zephyr-eabi` toolchain; set `ZEPHYR_SDK_INSTALL_DIR` |
| **West + Zephyr Python env** | `pip install west`; activate Zephyr venv per [Zephyr getting started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) |
| **Python packages** | `pyelftools`, `intelhex`, `cbor2` (MCUboot signing / artifact steps) |
| **mender-artifact** | On `PATH` as `.tools/bin/mender-artifact` (local build or install into workspace `.tools/`) |
| **Flash probe** | NXP LinkServer and/or SEGGER J-Link for CM33 |

## Secrets (do not commit)

Create `mender-mcu-integration/mender-local.conf` locally (listed in `mender-mcu-integration/.gitignore`):

```kconfig
# Local Hosted Mender credentials — do not commit
CONFIG_MENDER_SERVER_TENANT_TOKEN="<your-tenant-token>"
CONFIG_MENDER_SERVER_HOST_US=y
```

Never commit tenant tokens or credentials. `prj.conf` uses `"..."` placeholders only.

## Build (canonical)

From the West workspace root:

```bash
west build -p --sysbuild \
  -b mimxrt1180_evk/mimxrt1189/cm33 \
  mender-mcu-integration \
  -- \
  -DEXTRA_CONF_FILE=mender-mcu-integration/mender-local.conf \
  -DCONFIG_MENDER_ARTIFACT_NAME=dev-1
```

- **Sysbuild** builds MCUboot (`SB_CONFIG_BOOTLOADER_MCUBOOT=y` in `mender-mcu-integration/sysbuild-mcuboot.conf`) and the application.
- **Board fragment** `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` is applied automatically for this board target.
- **`mender-local.conf`** supplies Hosted Mender tenant token and server selection at build time.

## Build outputs

| File | Role |
|------|------|
| `build/mcuboot/zephyr/zephyr.bin` | MCUboot bootloader image (flash at `boot_partition`) |
| `build/mender-mcu-integration/zephyr/zephyr.bin` | Unsigned application (internal; slot payload before signing) |
| `build/mender-mcu-integration/zephyr/zephyr.signed.bin` | MCUboot-signed app image (slot update payload) |
| `build/mender-mcu-integration/zephyr/zephyr.mender` | Mender artifact (`zephyr-image`, name `dev-1`) for Hosted Mender upload |

Artifact metadata from a successful host build: device type `mimxrt1180_evk`, artifact type `zephyr-image`.

## Flash

From workspace root after build:

**Hardware (CM33):** set SW5 to **0100** before power-on. For LinkServer (default), leave JP5 uninstalled.

```bash
west flash -d build
```

- **Runner:** LinkServer (default; `MIMXRT1189xxxxx:MIMXRT1180-EVK`). J-Link also supported — see `zephyr/boards/nxp/mimxrt1180_evk/board.cmake`.
- **Order:** MCUboot first, then signed application — separate partition images, **no merged/combined image**.
- **Images:**
  - `build/mcuboot/zephyr/zephyr.bin` → `boot_partition`
  - `build/mender-mcu-integration/zephyr/zephyr.signed.bin` → slot partition
- **pyOCD does not work** on this EVK (external flash programming unsupported); use LinkServer or J-Link.

Reset the board after flash (SW3) and attach serial on MCU-Link (115200 8N1, J53).

## Hosted Mender

| Setting | Value |
|---------|-------|
| Server | Hosted Mender US (`CONFIG_MENDER_SERVER_HOST_US`) |
| Plan tier | Micro |
| Device type | `mimxrt1180_evk` (Zephyr `BOARD` default) |
| Update module | `zephyr-image` |
| Artifact | Upload `build/mender-mcu-integration/zephyr/zephyr.mender` |

Register the device after first boot with network connectivity; deploy the matching artifact name (e.g. `dev-1`).


## Hosted Mender workstation tools

Add workspace-local binaries to `PATH` (from the West workspace root, one level above this repo):

```bash
export PATH="$(pwd)/.tools/bin:$PATH"
```

| Tool | Role |
|------|------|
| **mender-artifact** | Already in `.tools/bin` — package and inspect local `.mender` files (Zephyr sysbuild calls this when artifact generation is enabled). |
| **mender-cli** | **Release tag `2.0.0`** on [mendersoftware/mender-cli](https://github.com/mendersoftware/mender-cli/releases/tag/2.0.0) (GitHub tag is `2.0.0`, not `v2.0.0`). Pre-built binaries: [Mender downloads](https://docs.mender.io/downloads#mender-cli). **Do not** install `@v0.9.0` or an old `@latest` pseudo-version — that build only exposes `login` and `completion`. Install into `.tools/bin` (needs Go + **libssl-dev** for CGO): `GOBIN="$(pwd)/.tools/bin" go install github.com/mendersoftware/mender-cli@2.0.0`. Verify: `mender-cli --help` lists `artifacts`, `devices`, `cp`, `terminal`, etc. **Auth:** Hosted Mender **PAT** (not the device tenant token in `mender-local.conf`) — `mender-cli login --username you@example.com --password '<PAT>' --server https://hosted.mender.io` or `--token-value '<PAT>'` on each command. |
| **curl + jq** | Hosted Mender REST API for **accept pending device** and **create deployment** when you need full control or the CLI lacks a subcommand. |

Store PATs and tenant tokens only in gitignored local config or your environment — never in git or these notes.

## RT1180 board configuration notes

File: `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf`

- **NETC Ethernet** — `CONFIG_ETH_NXP_IMX_NETC`, `CONFIG_NET_L2_ETHERNET`; `CONFIG_NET_IF_MAX_IPV4_COUNT=2` (EVK exposes multiple NETC interfaces; two suffices for host-port DHCP).
- **MCUboot signing** — `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE="bootloader/mcuboot/root-rsa-2048.pem"` (demo key from upstream tree).
- **Mender storage** — `CONFIG_MENDER_STORAGE_PARTITION_STORAGE_PARTITION=y` maps to `storage_partition` in `zephyr/boards/nxp/mimxrt1180_evk/mimxrt1180_evk.dtsi` (after slot0/slot1 in external flash).
- **RNG workaround** — RT1180 CM33 DTS has no `zephyr,entropy` yet; `CONFIG_TEST_RANDOM_GENERATOR` + `CONFIG_TIMER_RANDOM_GENERATOR` satisfy `sys_rand_get` for networking/TLS until hardware entropy is wired up.

## CM7 boot and OTA

**Verdict: PARTIAL.** CM7 boot test **yes** — upstream `mbox_data` sysbuild (CM33 main, CM7 remote) loads CM7 firmware from `slot1`. Mender on CM7 **no** (no network stack on CM7 in this design). CM7 OTA **via CM33** only — needs a dedicated flash partition and a custom Mender update module on CM33.

### slot1 conflict

External flash `slot1_partition` (`image-1`, 7 MiB @ 0x720000) serves two roles today:

| Role | Consumer |
|------|----------|
| MCUboot CM33 swap secondary | Mender CM33 OTA (`slot0`/`slot1` swap) |
| CM7 firmware store | `nxp,m7-partition = &slot1_partition` in CM7 DTS |

Enabling `CONFIG_SECOND_CORE_MCUX=y` on the Mender CM33 build **before repartitioning** collides with MCUboot swap — one or both paths break.

Executable bring-up steps live in **[Zephyr testing plan](#zephyr-testing-plan)** below (Phases 0–4). This subsection is the **roadmap context** only.

| Roadmap track | Testing plan phase | Notes |
|---------------|-------------------|-------|
| CM33 Mender OTA | Phase 2 | Current track; no `SECOND_CORE_MCUX` yet |
| CM7 boot verify | Phase 3 | Separate sysbuild image; **do not** flash over Mender CM33 build until slot1 repartition |
| CM7 OTA via CM33 | Phase 4 | Future — needs `cm7_partition` + custom update module |

## Zephyr testing plan

Actionable validation checklist for RT1180 CM33 Mender bring-up and (separately) CM7 dual-core boot. Run from the **West workspace root** (parent of `mender-mcu-integration/`). Cross-references: [Build (canonical)](#build-canonical), [Flash](#flash), [Hosted Mender](#hosted-mender), [CM7 boot and OTA](#cm7-boot-and-ota), [RT1180 board configuration notes](#rt1180-board-configuration-notes).

### Prerequisites (all phases)

| Item | Requirement | Reference |
|------|-------------|-----------|
| West workspace | `west init -l mender-mcu-integration && west update` | [Workspace layout](#workspace-layout) |
| Zephyr SDK | 0.17.4, `ZEPHYR_SDK_INSTALL_DIR` set | [Prerequisites](#prerequisites) |
| Python env | Zephyr venv active; `pyelftools`, `intelhex`, `cbor2` | MCUboot signing / artifacts |
| Local secrets | `mender-mcu-integration/mender-local.conf` (gitignored) with tenant token | [Secrets](#secrets-do-not-commit) |
| Board Kconfig fragment | Auto-applied: `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | NETC, RNG workaround, Mender storage |
| Hosted Mender account | Micro tier; device type `mimxrt1180_evk`; update module `zephyr-image` | [Hosted Mender](#hosted-mender) |
| Workstation PAT | Hosted Mender **personal access token** (not device tenant token) | [Hosted Mender workstation tools](#hosted-mender-workstation-tools) |

**Tools on PATH** (from workspace root):

```bash
export PATH="$(pwd)/.tools/bin:$PATH"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/zephyr-sdk-0.17.4}"
```

| Check | Command | Pass |
|-------|---------|------|
| mender-artifact | `mender-artifact --version` | Version string, exit 0 |
| mender-cli | `mender-cli --help` | Lists `artifacts`, `devices`, `deployments`, … (needs **@2.0.0**, not old `@v0.9.0`) |
| LinkServer / J-Link | `west flash --runner list` | LinkServer default; pyOCD **unsupported** on this EVK |

**Config files touched by testing**

| File | Role |
|------|------|
| `mender-mcu-integration/mender-local.conf` | Tenant token + server (local only) |
| `mender-mcu-integration/prj.conf` | Mender client, TLS, polling intervals |
| `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | NETC, RNG workaround, signing key path |
| `mender-mcu-integration/sysbuild-mcuboot.conf` | MCUboot swap-using-move |
| `bootloader/mcuboot/root-rsa-2048.pem` | Demo signing key (upstream) |

**Zephyr documentation (MCP):** use the **user-zephyr-docs** MCP server in Cursor for Kconfig, devicetree, and API lookups (e.g. `mimxrt1180_evk`, NETC Ethernet, `mbox_data` sysbuild). Upstream board doc: [mimxrt1180_evk](https://docs.zephyrproject.org/latest/boards/nxp/mimxrt1180_evk/doc/index.html).

### Hardware setup (EVK)

| Setting | Value | If wrong |
|---------|-------|----------|
| **SW5** (core select) | **0100** before power-on for CM33 / dual-core samples | CM7-only debug needs **0001**; Mender CM33 tests fail on wrong core |
| **JP5** | Uninstalled (LinkServer default) | Install only for external J-Link on J37 |
| **JP3, JP5** | On (factory) — UART routed to MCU-Link | No serial if off |
| **Serial CM33** | J53 USB, **LPUART1**, 115200 8N1 | See [Flash](#flash) |
| **Serial CM7** (Phase 3) | J60 default, or JP7 open + J53 second UART (**LPUART12**) | Dual-UART verify needs both consoles |
| **Ethernet** | Host PC cable to EVK **host network port** (1G end-point / ENETC0 side) | DHCP must reach your LAN; see NETC triage below |
| **Reset** | SW3 after flash | Bootloader / app won't run updated image until reset |

---

### Phase 0 — Host / build sanity

Goal: reproducible sysbuild and valid Mender artifact before touching hardware.

- [ ] **0.1** Activate Zephyr environment and confirm SDK path.
- [ ] **0.2** Pristine sysbuild of Mender integration app:

```bash
west build -p --sysbuild \
  -b mimxrt1180_evk/mimxrt1189/cm33 \
  mender-mcu-integration \
  -- \
  -DEXTRA_CONF_FILE=mender-mcu-integration/mender-local.conf \
  -DCONFIG_MENDER_ARTIFACT_NAME=dev-1
```

- [ ] **0.3** Build completes with MCUboot + app (no signing / artifact errors).
- [ ] **0.4** Outputs present:

```bash
test -f build/mcuboot/zephyr/zephyr.bin
test -f build/mender-mcu-integration/zephyr/zephyr.signed.bin
test -f build/mender-mcu-integration/zephyr/zephyr.mender
```

- [ ] **0.5** Validate artifact:

```bash
mender-artifact validate build/mender-mcu-integration/zephyr/zephyr.mender
mender-artifact read build/mender-mcu-integration/zephyr/zephyr.mender
```

**Pass:** `validate` exit 0; `read` shows device type `mimxrt1180_evk`, artifact name matches `CONFIG_MENDER_ARTIFACT_NAME` (e.g. `dev-1`), type `zephyr-image`.

**Fail triage:** missing `mender-artifact` on PATH → [Hosted Mender workstation tools](#hosted-mender-workstation-tools); signing errors → check `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE` and Python deps; sysbuild MCUboot errors → `mender-mcu-integration/sysbuild-mcuboot.conf`.

---

### Phase 1 — EVK flash (CM33 Mender image)

Goal: MCUboot + signed app on EVK; serial console and Ethernet link before Hosted Mender.

**Preconditions:** Phase 0 pass; **SW5 = 0100**; `mender-local.conf` flashed via build (tenant token in image).

- [ ] **1.1** Flash sysbuild output:

```bash
west flash -d build
```

- [ ] **1.2** Reset (SW3); attach serial on **J53** @ 115200 — expect MCUboot banner then Zephyr/Mender boot logs (no hard fault loop).
- [ ] **1.3** Connect Ethernet to host port; wait for DHCP (may take up to one update-poll interval after link up).
- [ ] **1.4** On serial, confirm network interface has IPv4 (shell `net iface` / log lines for address assignment).

**Pass:** Flash succeeds (LinkServer or J-Link); CM33 console alive; at least one NETC interface obtains DHCP IPv4 on your LAN.

**Fail triage**

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| Flash fails / wrong core | SW5 not **0100** | Power off, set SW5, reflash |
| pyOCD errors | Unsupported runner | Use LinkServer (default) or J-Link |
| Boot hang / TLS errors early | RNG / entropy | Board conf enables `CONFIG_TEST_RANDOM_GENERATOR` + `CONFIG_TIMER_RANDOM_GENERATOR` — see [RT1180 board configuration notes](#rt1180-board-configuration-notes) |
| Link up, no DHCP | Wrong NETC port or multi-interface confusion | EVK exposes multiple NETC interfaces; `CONFIG_NET_IF_MAX_IPV4_COUNT=2` — try host port; query Zephyr MCP for `CONFIG_ETH_NXP_IMX_NETC` / interface naming |
| No serial | JP3/JP5 | Factory jumpers on; correct USB port J53 |

---

### Phase 2 — Hosted Mender (CM33 OTA)

Goal: device accepted, artifact deployed, inventory reported, MCUboot swap confirmed.

**Preconditions:** Phase 1 pass; artifact from Phase 0; Hosted Mender PAT in environment (never commit):

```bash
export MENDER_SERVER_URL="https://hosted.mender.io"
export MENDER_PAT="<your-hosted-mender-pat>"
export PATH="$(pwd)/.tools/bin:$PATH"
```

- [ ] **2.1** Boot device with network; within ~30–60 s device appears as **pending** (Micro tier, type `mimxrt1180_evk`).
- [ ] **2.2** Accept pending device (REST — replace `DEVICE_ID` after listing):

```bash
# List pending
curl -s -H "Authorization: Bearer ${MENDER_PAT}" \
  "${MENDER_SERVER_URL}/api/management/v2/devauth/devices?status=pending" | jq .

# Accept (set DEVICE_ID from response)
curl -s -X PATCH -H "Authorization: Bearer ${MENDER_PAT}" \
  -H "Content-Type: application/json" \
  -d '{"device_status":"accepted"}' \
  "${MENDER_SERVER_URL}/api/management/v2/devauth/devices/${DEVICE_ID}/auth/auth-set/${AUTH_SET_ID}/status"
```

Alternatively use UI **Devices → Pending → Accept**, or `mender-cli` after `mender-cli login` with PAT.

- [ ] **2.3** Upload artifact (name must match deployed revision, e.g. `dev-1`):

```bash
mender-cli artifacts upload \
  --server "${MENDER_SERVER_URL}" \
  --token-value "${MENDER_PAT}" \
  build/mender-mcu-integration/zephyr/zephyr.mender
```

- [ ] **2.4** Create deployment to device group / single device; wait until **finished** / **successful** in UI or API.
- [ ] **2.5** Inventory: device shows attributes (artifact name, kernel/Zephyr version as reported by client).
- [ ] **2.6** **MCUboot swap test:** build a second artifact with different `CONFIG_MENDER_ARTIFACT_NAME` (e.g. `dev-2`), upload, deploy; on serial confirm download, reboot, new image running; if swap-with-move applies, confirm **test → confirm** behaviour per [sysbuild-mcuboot.conf](sysbuild-mcuboot.conf) (`SB_CONFIG_MCUBOOT_MODE_SWAP_USING_MOVE=y`).

**Pass:** Device **accepted** and **online**; deployment **success**; post-reboot serial shows new artifact/revision; no permanent boot loop after failed image (rollback path intact).

**Fail triage:** pending forever → DHCP/firewall; 401 → PAT not tenant token; deployment failed → artifact device type / name mismatch; repeated panic after OTA → slot corruption — reflash Phase 1; do **not** enable `CONFIG_SECOND_CORE_MCUX` on this image ([slot1 conflict](#slot1-conflict)).

---

### Phase 3 — CM7 boot test (no Mender)

Goal: verify dual-core `mbox_data` sysbuild — CM33 loads CM7 from `slot1` — **without** Mender client or Hosted Mender.

**Preconditions:** Phase 0–2 **not** required simultaneously. Use a **separate build directory**. **Do not** combine with Phase 2 Mender image on the same flash layout until [slot1 repartition](#slot1-conflict) — flashing `mbox_data` over a Mender CM33 image is OK for a **dedicated CM7 boot test session** but invalidates Phase 2 OTA state; reflash Mender image before returning to Phase 2.

- [ ] **3.1** **SW5 = 0100** (dual-core samples).
- [ ] **3.2** Build upstream sample (separate dir):

```bash
west build -p --sysbuild \
  -b mimxrt1180_evk/mimxrt1189/cm33 \
  zephyr/tests/drivers/mbox/mbox_data \
  -d build-mbox
```

- [ ] **3.3** Flash and reset:

```bash
west flash -d build-mbox
```

- [ ] **3.4** **Dual UART:** CM33 console **LPUART1** on J53; CM7 console **LPUART12** on J60 (or JP7 + J53). Both @ 115200.
- [ ] **3.5** CM33 log shows mailbox handshake; CM7 prints remote sample output; CM7 started from `slot1` (`nxp,m7-partition`).

**Pass:** Both cores boot; IPC/mbox test messages on both consoles; no permanent fault on either core.

**Fail triage:** CM7 silent → reset required after flash (board doc); attach debugger to CM7 if needed; SW5 must stay **0100** for dual-core. Slot1 conflict with prior Mender swap → mass reflash or erase external flash before retry.

---

### Phase 4 — CM7 OTA via CM33 (future)

Brief criteria only — full design in [CM7 boot and OTA](#cm7-boot-and-ota).

- [ ] **4.1** Devicetree: dedicated `cm7_partition`; MCUboot `slot0`/`slot1` reserved for CM33 only.
- [ ] **4.2** Custom Mender update module on CM33: download CM7 artifact, write `cm7_partition`, reset/reload CM7 via mailbox driver.
- [ ] **4.3** Enable `CONFIG_SECOND_CORE_MCUX=y` on Mender CM33 build **only after** 4.1.
- [ ] **4.4** End-to-end: Hosted Mender deploy pushes CM7 firmware; CM33 applies update; CM7 version visible in inventory or shell.

**Pass:** CM33 Mender OTA and CM7 firmware update coexist without corrupting MCUboot swap slots.

---

### Failure triage (quick reference)

| Area | Symptom | Check |
|------|---------|-------|
| **RNG** | TLS/connect fail, `sys_rand_get` errors | `CONFIG_TEST_RANDOM_GENERATOR`, `CONFIG_TIMER_RANDOM_GENERATOR` in board conf; no hardware entropy on CM33 DTS yet |
| **NETC** | Multiple interfaces, wrong DHCP port | `CONFIG_NET_IF_MAX_IPV4_COUNT=2`; cable on host/end-point port; Zephyr MCP + [board Ethernet section](https://docs.zephyrproject.org/latest/boards/nxp/mimxrt1180_evk/doc/index.html#ethernet) |
| **SW5** | Wrong core flashed / no boot | **0100** = CM33 / dual / Mender; **0001** = CM7-only debug |
| **Flash tool** | pyOCD failure | Use LinkServer or J-Link only |
| **slot1** | CM7 boot breaks Mender swap or vice versa | Do not merge Phase 2 + Phase 3 images until Phase 4 repartition |
| **Secrets** | Auth failures | Device uses tenant token in `mender-local.conf`; workstation uses **PAT** — never commit either |

## Zephyr documentation (MCP)

Use the **user-zephyr-docs** MCP server in Cursor for Zephyr Kconfig, devicetree, and API lookups while iterating on this port.

## Status

Track progress with the **[Zephyr testing plan](#zephyr-testing-plan)** checkboxes.

| Step | Testing plan | State |
|------|--------------|-------|
| West workspace + dependencies | Prerequisites | Done |
| Host sysbuild + artifact | Phase 0 | Done |
| EVK flash + serial | Phase 1 | Done |
| Ethernet DHCP | Phase 1 | **Not verified** |
| Hosted Mender accept + OTA | Phase 2 | **Not verified** |
| MCUboot swap on deploy | Phase 2.6 | **Not verified** |
| CM7 `mbox_data` dual boot | Phase 3 | Repeatable (separate image) |
| CM7 OTA via CM33 | Phase 4 | Future |

## Next steps

1. Run **Phase 1** Ethernet/DHCP checks if not already green.
2. Complete **Phase 2** (accept device, upload `zephyr.mender`, deploy, confirm swap).
3. When needed, run **Phase 3** in `build-mbox` — then reflash Mender image before resuming Phase 2.
4. Push local commits when ready to share the RT1180 port upstream or to a fork remote.

## Commits

| Repo | Commit | Notes |
|------|--------|-------|
| `mender-mcu-integration/` | `76ebea3` | RT1180 board conf + `.gitignore` for local secrets/tools/build; **local, unpushed** (`main` ahead of `origin/main` by 1) |
