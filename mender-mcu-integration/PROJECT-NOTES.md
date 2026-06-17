# Mender MCU OTA — i.MX RT1180 (Zephyr)

Project notes for Mender over-the-air updates on NXP **i.MX RT118x** boards: **MIMXRT1180-EVK** (`mimxrt1180_evk/mimxrt1189/cm33`) and **FRDM-IMXRT1186** (`frdm_imxrt1186/mimxrt1186/cm33`, CM33 only). This workspace is a West checkout of the upstream [mender-mcu-integration](mender-mcu-integration/) reference app with a local RT1180 board configuration.

Upstream getting-started and native_sim instructions remain in [mender-mcu-integration/README.md](mender-mcu-integration/README.md). Mender MCU module docs: [modules/mender-mcu/README.md](modules/mender-mcu/README.md).

## Purpose

Public repo: https://github.com/DynamicDevices/mender-rt1180-zephyr

Evaluate **Mender MCU OTA** on the RT1180 CM33 core: MCUboot + swap, Ethernet bring-up, Hosted Mender client, and automatic `zephyr-image` artifact generation at build time.

**Scope (CM33 vs CM7):** RT1180 is dual-core (Cortex-M33 + Cortex-M7). This port targets **CM33 only** — Mender MCU, MCUboot, and Zephyr networking. **CM7 is a separate phase** and out of scope for initial bring-up.

**Security (project requirement):** RT118x targets (**EVK** and **FRDM**) must support on-die **EdgeLock Secure Enclave (ELE)** for cryptographic device identity on the production path — not NVS-in-flash alone. Lab bring-up may use NVS auth keys temporarily; see [Security / EdgeLock](#security--edgelock).

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
├── zephyr/                 # Zephyr v4.4.0 (west import; `frdm_imxrt1186` requires ≥ v4.4)
├── bootloader/mcuboot/     # MCUboot (west import)
├── modules/mender-mcu/     # Mender MCU Zephyr module
├── mender-mcu-integration/ # Reference app + west manifest (git repo)
├── scripts/                # Host helpers — see [Scripts inventory](#scripts-inventory)
├── tools/net-tools/          # Zephyr net-tools (manual `git clone`; gitignored)
├── .tools/bin/             # Local mender-artifact + mender-cli (gitignored)
└── build/                  # Sysbuild output (gitignored)
```

Fresh checkout:

```bash
west init -l mender-mcu-integration
west update
```

## Scripts inventory

Host helpers at the West workspace root (`scripts/`). All paths below are from that root.

| Script | Role |
|--------|------|
| `scripts/build-native-sim.sh` | Pristine/incremental `native_sim` build; pins NSI to **GCC 11** via `fix-native-sim-link.sh` |
| `scripts/fix-native-sim-link.sh` | Repair NSI link after a failed pristine configure (sets `NSI_CC` to `gcc-11`) |
| `scripts/run-native-sim-network.sh` | Start/stop/status TAP + DHCP + NAT using `tools/net-tools/nat.conf` (`cd` into net-tools; `stop` passes `--config nat.conf`) |
| `scripts/test-mender-native-sim.sh` | Build (optional) + run `zephyr.exe` Mender smoke test; expects TAP from `run-native-sim-network.sh` |
| `scripts/create-native-sim-deployment.sh` | Build noop-update artifact (`device_type` `native_sim`) and create **one** Hosted Mender deployment (`MENDER_DEPLOY_TARGET=device` \| `device_type` \| `group`; default group **`simulator`**) |
| `scripts/build-rt1180-evk.sh` | Sysbuild Mender for EVK CM33 (default `build/`) |
| `scripts/build-rt1186-frdm.sh` | Sysbuild Mender for FRDM-IMXRT1186 CM33 (default `build-frdm-rt1186/`) |
| `scripts/create-rt1180-deployment.sh` | Upload `zephyr.mender` (default `device_type` `mimxrt1180_evk`, `build/`) — **one** deployment (default group **`rt1180-lab`**) |
| `scripts/create-rt1186-frdm-deployment.sh` | Same as above for FRDM (`device_type` `frdm_imxrt1186`, `build-frdm-rt1186/`) |
| `scripts/test-vemu.sh` | Build `hello_world` for nRF5340 and run headless **vemu** (or print browser load steps) |
| `scripts/run-vemu-demo.sh` | Build `hello_world` for nRF5340 and print vemulator.com load instructions |
| `scripts/build-mender-vemu.sh` | Sysbuild Mender app for `nrf5340dk/nrf5340/cpuapp` (noop module; uses `boards/nrf5340dk_nrf5340_cpuapp.conf`) |
| `scripts/test-mender-vemu.sh` | Build + run Mender image in headless vemu (slow — increase `--frames`) |

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| **Zephyr SDK 1.0.x** | Required for Zephyr **v4.4.0** (`FindHostTools.cmake`); download **`zephyr-sdk-1.0.1_*_gnu.tar.xz`** from [SDK 1.0.1](https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v1.0.1), run `setup.sh -c -t arm-zephyr-eabi` (or `-t all`), set `ZEPHYR_SDK_INSTALL_DIR` to the install dir. Plain (non-`_gnu`) tarballs are not the supported ng layout. SDK 0.17.4 was used with v4.2.0 EVK-only bring-up. |
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

For **mender-cli** on your workstation, create `mender-mcu-integration/mender-pat-local.conf` (also matched by `*-local.conf` in `.gitignore`). Put **one line** in the file: the raw Hosted Mender PAT JWT only (not Kconfig `KEY=value` syntax).

## Build (canonical)

From the West workspace root:

```bash
west build -p --sysbuild \
  -b mimxrt1180_evk/mimxrt1189/cm33 \
  mender-mcu-integration \
  -- \
  -DEXTRA_CONF_FILE=mender-mcu-integration/mender-local.conf \
  -DCONFIG_MENDER_ARTIFACT_NAME="dev-1"
```

- **Sysbuild** builds MCUboot (`SB_CONFIG_BOOTLOADER_MCUBOOT=y` in `mender-mcu-integration/sysbuild-mcuboot.conf`) and the application.
- **Board fragment** `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` is applied automatically for this board target.
- **`mender-local.conf`** supplies Hosted Mender tenant token and server selection at build time.

## FRDM-IMXRT1186 vs MIMXRT1180-EVK

| Topic | MIMXRT1180-EVK | FRDM-IMXRT1186 |
|-------|-----------------|----------------|
| Zephyr board string | `mimxrt1180_evk/mimxrt1189/cm33` | `frdm_imxrt1186/mimxrt1186/cm33` |
| SoC | MIMXRT1189 | MIMXRT1186 |
| Mender device type | `mimxrt1180_evk` | `frdm_imxrt1186` |
| Board conf (auto) | `boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | `boards/frdm_imxrt1186_mimxrt1186_cm33.conf` |
| Default build dir | `build/` | `build-frdm-rt1186/` |
| LinkServer probe | `MIMXRT1189xxxxx:MIMXRT1180-EVK` | `MIMXRT1186xxxxx:FRDM-IMXRT1186` |
| Console UART | LPUART1 (MCU-Link J53) | LPUART1 (on-board MCU-Link) |
| Main RAM (CM33) | EVK HyperRAM layout | 8 MiB HyperRAM @ `0x38000000` (`zephyr,sram`) |
| External NOR | W25Q128 on FlexSPI (EVK `mimxrt1180_evk.dtsi`) | W25Q128JV on FlexSPI2 @ `0x04000000` |
| MCUboot partitions | 128 KiB boot, 7 MiB ×2 slots, ~2 MiB storage | Same layout in `frdm_imxrt1186.dtsi` |
| NETC Ethernet | Multiple switch ports (see EVK doc) | `switch_port0` + `switch_port2` (1 Gbps TSN ports); same NETC Kconfig as EVK |
| Zephyr upstream focus | NXP superset / full platform support | Supported; some features may lag EVK ([board doc](https://docs.zephyrproject.org/latest/boards/nxp/frdm_imxrt1186/doc/index.html)) |
| Lab Mender group | **`rt1180-lab`** (shared name; **do not** mix device types in one deployment target) | Same group name OK if deployments target **`device_type`** or per-device UUID |

**Zephyr version:** upstream `frdm_imxrt1186` landed in Zephyr **v4.4.0**. This manifest pins **`revision: v4.4.0`** in `west.yml` (was v4.2.0 for EVK-only bring-up). Run `west update` after pulling manifest changes.

### Build (FRDM CM33)

```bash
./scripts/build-rt1186-frdm.sh
# or manually from West workspace root:
west build -p --sysbuild \
  -b frdm_imxrt1186/mimxrt1186/cm33 \
  -d build-frdm-rt1186 \
  mender-mcu-integration \
  -- \
  -DEXTRA_CONF_FILE=mender-mcu-integration/mender-local.conf \
  -DCONFIG_MENDER_ARTIFACT_NAME="dev-1"
```

Artifact device type: **`frdm_imxrt1186`**. Deploy with `./scripts/create-rt1186-frdm-deployment.sh` (or `MENDER_DEVICE_TYPE=frdm_imxrt1186 MENDER_BUILD_DIR=build-frdm-rt1186 ./scripts/create-rt1180-deployment.sh`).

### Flash (FRDM CM33)

After sysbuild, from workspace root:

```bash
west flash -d build-frdm-rt1186
```

Use LinkServer (default) or J-Link per `zephyr/boards/nxp/frdm_imxrt1186/board.cmake`. Reset after flash; serial **115200 8N1** on MCU-Link. CM33: jumper **J60** = **1:OFF 2:OFF 3:ON**. Ethernet: cable to **`swp0` or `swp2`** (not `eth0`/`swp4`). No devicetree overlay is required for Mender storage — upstream `storage_partition` matches the EVK sizing model.

## Security / EdgeLock

Both **MIMXRT1180-EVK** and **FRDM-IMXRT1186** share the same on-die **EdgeLock Secure Enclave (ELE)** subsystem (RT1180 family) — not the separate **HSE** block on some other NXP MCUs. **Board choice does not change the security roadmap.**

### Project requirement

RT118x targets (**EVK** + **FRDM**) must support on-die **ELE** for **cryptographic device identity** on the production path — not NVS-in-flash alone. Lab bring-up may use NVS auth keys temporarily (current Mender default); shipping configuration must move device-auth private keys into ELE opaque storage with hardware entropy — not plaintext DER in external NOR.

### Current state (lab)

| Area | Today |
|------|--------|
| **ELE (Zephyr SoC)** | ELE **ping** and **TRDC** access control handled by upstream RT118x SoC driver — no application keystore, no `nxp,ele-trng` on CM33 DTS yet |
| **Mender device-auth** | Auth keys generated in RAM; stored as DER in **NVS** on `storage_partition` (external flash, after MCUboot slots) |
| **Entropy** | **Timer RNG workaround** in board conf (`CONFIG_TEST_RANDOM_GENERATOR` + `CONFIG_TIMER_RANDOM_GENERATOR`) — CM33 DTS has no `zephyr,entropy` node; see [RT1180 board configuration notes](#rt1180-board-configuration-notes) |
| **MCUboot image key** | Host PEM at build time — **separate** from Mender device identity |
| **Tenant token** | Kconfig at build time (`mender-local.conf`) — **separate** from device identity key |

### Target architecture (ELE-backed device auth)

| Layer | Target |
|-------|--------|
| **Key storage** | **PSA opaque keys** in ELE keystore — non-exportable private key; sign devauth/TLS payloads in-enclave |
| **Entropy** | **ELE TRNG** for key generation when Zephyr wires `nxp,ele-trng` on RT118x CM33 DTS |
| **Manufacturing** | **SPSDK** / [AN14861](https://docs.nxp.com/bundle/AN14861/page/topics/introduction.html) OEM provisioning; **EdgeLock 2GO** for fleet key management where applicable |
| **Mender MCU** | Platform layer: storage + TLS signing via **PSA/ELE**, not plaintext DER in NVS — extension points `MENDER_PLATFORM_STORAGE_TYPE_WEAK`, `MENDER_PLATFORM_TLS_TYPE_WEAK` in [`modules/mender-mcu/`](modules/mender-mcu/) |
| **Out of scope for identity key** | **MCUboot image signing key** (firmware authenticity) and **Hosted Mender tenant token** (server auth) remain separate credentials |

Zephyr `SECURE_STORAGE` (encrypted flash ITS) is a weaker intermediate — still software-visible after decrypt; ELE opaque keys are the production target.

### Phased security plan

| Phase | Goal | Status |
|-------|------|--------|
| **S0 — Lab** | NVS auth keys + timer RNG; ELE ping via Zephyr SoC; Hosted Mender auth + OTA (`native_sim` Phase 0b, then EVK/FRDM Phase 1) | Phase 0b **done**; hardware **TBD** |
| **S1 — ELE TRNG** | Enable hardware entropy when upstream adds `nxp,ele-trng` DTS binding and driver for RT118x CM33 (`CONFIG_ENTROPY_NXP_ELE_TRNG`); drop timer-RNG workaround | Blocked on upstream NXP/Zephyr |
| **S2 — PSA crypto driver** | Zephyr PSA Crypto integration with NXP [`psa_crypto_driver`](https://github.com/NXP/psa_crypto_driver) (**ELE_S4XX** backend) | Not started |
| **S3 — Mender platform** | Opaque ELE key storage + PSA sign for devauth in mender-mcu platform layer | Not started |
| **S4 — Manufacturing** | AN14861 provisioning workflow, lifecycle policy, EdgeLock 2GO integration; remove timer RNG | Not started |

S1–S4 are independent of the [Zephyr testing plan](#zephyr-testing-plan) OTA phases (0–4) but should complete before any production fleet rollout.

### Warnings

- **Lifecycle transitions are largely irreversible** — do not advance ELE provisioning, OEM key import, or secure-boot enablement on **sole lab boards** without explicit NXP guidance; a mis-step can brick secure-boot or key-storage paths on that unit.
- **Do not experiment on your only EVK/FRDM** — keep spare units or documented rollback before provisioning trials.
- **Secure boot vs Mender identity** — MCUboot **image signing key** and Mender **device identity key** (target: ELE) serve different roles; do not conflate or reuse.
- **Tenant token** is server-side auth at build time — not a substitute for per-device ELE identity.

### References

| Topic | Link |
|-------|------|
| OEM key provisioning (RT1180) | [AN14861 — Secure OEM Key Provisioning](https://docs.nxp.com/bundle/AN14861/page/topics/introduction.html) |
| NXP EdgeLock program | [EdgeLock Secure Enclave](https://www.nxp.com/products/nxp-product-information/nxp-product-programs/edgelock-secure-enclave:EDGELOCK-SECURE-ENCLAVE) |
| NXP PSA crypto driver | [NXP/psa_crypto_driver](https://github.com/NXP/psa_crypto_driver) |
| Zephyr EVK board | [mimxrt1180_evk](https://docs.zephyrproject.org/latest/boards/nxp/mimxrt1180_evk/doc/index.html) |
| Zephyr FRDM board | [frdm_imxrt1186](https://docs.zephyrproject.org/latest/boards/nxp/frdm_imxrt1186/doc/index.html) |
| Mender MCU module | [`modules/mender-mcu/README.md`](modules/mender-mcu/README.md) — platform auth, TLS, and storage extension points |

## Cyber Resilience Act (CRA) — technical mapping

**Disclaimer:** Engineering preparedness notes only — **not legal advice** and **not a conformity assessment or CE marking sign-off**. Product classification, support-period justification, technical documentation (Annex VII), and Article 14 incident reporting require qualified legal/regulatory review for your specific product and go-to-market.

**Scope:** RT118x CM33 firmware built from this workspace (Zephyr + MCUboot + Mender MCU → Hosted Mender). Companion MPU Linux (e.g. i.MX 93 in HMS designs) is a separate software stack and OTA path.

### Official references

| Topic | Source |
|-------|--------|
| CRA regulation | [Regulation (EU) 2024/2847](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX%3A32024R2847) |
| Essential requirements | [Annex I](https://www.craact.eu/regulation/cra/annex/0) (Part I product properties; Part II vulnerability handling) |
| Manufacturer obligations | [Article 13](https://www.digitalacts.eu/regulation/cra/article/13/obligations-of-manufacturers) |
| Vulnerability / incident reporting | [Article 14](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX%3A32024R2847) (see also [Zephyr CRA page — reporting timelines](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html#what-are-the-vulnerability-reporting-obligations)) |
| Zephyr integrator guidance | [EU Cyber Resilience Act (CRA) — Zephyr Project](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html) |
| Mender security model | [Security overview](https://docs.mender.io/overview/security) |
| Mender OTA / deployments | [Deployment](https://docs.mender.io/overview/deployment), [Inventory](https://docs.mender.io/overview/inventory) |
| Mender MCU (Zephyr) | [Mender MCU troubleshooting](https://docs.mender.io/troubleshoot/mender-mcu) |
| Zephyr SBOM | [`west spdx`](https://docs.zephyrproject.org/latest/security/sbom.html) |
| Zephyr CVE / PSIRT | [Security vulnerability reporting](https://docs.zephyrproject.org/latest/security/reporting.html), [Vulnerabilities](https://docs.zephyrproject.org/latest/security/vulnerabilities.html) |

**Key CRA dates (manufacturers):** vulnerability/incident reporting obligations from **11 September 2026**; full essential-requirements application for products placed on the market from **11 December 2027** ([Zephyr CRA summary](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html#overview)).

### CRA obligations — technical summary (not legal interpretation)

Annex I Part I expects products with digital elements to ship **secure by default** (sensible defaults, reset-to-secure-state where applicable), minimise attack surface, protect data in transit/at rest where relevant, and deliver **security updates** for a declared **support period** (typically **≥ 5 years** unless a shorter period is justified for the intended lifetime — [Recital / Art. 13(8)](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX%3A32024R2847)).

Annex I Part II expects a **vulnerability handling process**: identify/document components (including **SBOM** in a commonly used machine-readable format), receive and remediate reports, distribute **security updates** without undue delay (free to users unless tailor-made B2B contract says otherwise), and separate security fixes from feature updates where technically feasible ([Annex I Part II](https://www.craact.eu/regulation/cra/annex/0)).

**Article 14** adds **notification timelines** when you become aware of an **actively exploited vulnerability** or **severe incident** affecting your product (early warning **24 h**, follow-up **72 h**, final report per CRA — [Zephyr CRA table](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html#what-are-the-vulnerability-reporting-obligations)). **Article 13(6)** also expects upstream reporting when you find issues in integrated components (e.g. Zephyr) and sharing fixes where you develop them.

Classification of the **final product** (default vs important vs critical) drives conformity assessment depth; using networking/crypto in Zephyr does not by itself reclassify a product ([Zephyr CRA — core functionality](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html#which-category-does-my-product-belong-to)). An industrial Ethernet edge device built on RT118x is likely **default** unless its *core* function matches an Annex III/IV category — confirm per product.

### Current technical posture (this project)

| Area | State | CRA relevance |
|------|--------|---------------|
| **OTA channel** | Hosted Mender US; TLS 1.2; device auth via generated keypair | Secure update distribution (Annex I Part II) |
| **Phase 0b (`native_sim`)** | **Done** — client auth, inventory polling, noop deployment | Validates Mender integration only; **no** MCUboot / `zephyr-image` path |
| **RT118x hardware OTA** | **TBD** (Phases 1–2) | MCUboot swap + signed `zephyr-image` not yet bench-proven on EVK/FRDM |
| **Boot / image integrity** | MCUboot + RSA-2048 image signing; swap-with-move rollback | Addresses tampered firmware at boot; key is **demo** `root-rsa-2048.pem` |
| **Mender artifact signing** | **Not supported** on Mender MCU client ([docs](https://docs.mender.io/troubleshoot/mender-mcu)) | Payload integrity relies on **MCUboot** signature, not Mender `ArtifactVerifyKey` |
| **Device identity** | Lab: DER private key in **NVS** flash | Fails secure-by-default / key-protection expectations for production |
| **Entropy** | `CONFIG_TEST_RANDOM_GENERATOR` + timer RNG workaround | Unacceptable for production TLS/keygen; blocks “state of the art” crypto claims |
| **Inventory** | Mender client reports build/network attributes (`CONFIG_MENDER_CLIENT_INVENTORY_*`) | Fleet visibility; **not** a substitute for SBOM |
| **SBOM** | Not generated in build scripts | Annex I Part II gap |
| **Zephyr baseline** | Manifest pins **v4.4.0** ([`west.yml`](west.yml)) | Need LTS/backport policy aligned to declared support period |
| **Support period / risk assessment / Art. 14 process** | Not documented for a shipping product | Organisational gaps |
| **EdgeLock roadmap** | S0 lab → S4 manufacturing ([above](#security--edgelock)) | Production path for identity + TRNG |

### Gap analysis matrix

| CRA theme | What we have | Gap | Recommended action |
|-----------|--------------|-----|-------------------|
| **Secure by default** | MCUboot enforced boot; TLS to Mender; no open debug in `prj.conf` | Demo signing key; weak RNG; tenant token baked in Kconfig; NVS-exportable devauth key | Production MCUboot key in HSM/offline signer; **S1–S3** ELE TRNG + opaque devauth key; per-device or secure provisioning for tenant credentials |
| **Security updates (OTA)** | Mender MCU + `zephyr-image` artifact pipeline; rollback via MCUboot | Hardware path unproven; Micro tier (no phased rollout) | Complete **Phase 2** on EVK/FRDM; document update SLO; upgrade Hosted tier or use API if staged rollout needed |
| **Vulnerability handling** | Zephyr PSIRT/CVE exist; Mender security disclosure channel | No product process: triage, VEX, fix SLAs, customer comms | Register [Zephyr Vulnerability Alert Registry](https://docs.zephyrproject.org/latest/security/security-overview.html); define watch on Zephyr/NXP/Mender advisories; tie to Mender deployments |
| **SBOM** | `west spdx` available upstream | No CI SBOM per release; MCUboot SBOM separate; raw SPDX lacks CPE/PURL | Add release job: `west spdx` on app **and** MCUboot builds; enrich IDs; archive per `CONFIG_MENDER_ARTIFACT_NAME` |
| **Component / fleet inventory** | Mender inventory + device groups (`simulator`, `rt1180-lab`) | Inventory ≠ SBOM; limited on Micro tier | Publish `artifact_name`, Zephyr `KERNEL_VERSION`, git SHA via inventory; map to SBOM version |
| **Incident / exploit reporting (Art. 14)** | — | No runbook or CSIRT contact | Legal + engineering runbook before **Sep 2026**; define “awareness” triggers |
| **Support period** | — | Not stated | Document ≥5 y (or justified shorter) maintenance window; pin Zephyr LTS or explicit fork/backport policy |
| **Upstream reporting (Art. 13(6))** | — | No formal Zephyr/NXP reporting path | Use [vulnerabilities@zephyrproject.org](https://docs.zephyrproject.org/latest/security/reporting.html) / NXP PSIRT as appropriate |
| **Hardening / config review** | `CONFIG_ASSERT`, warnings-as-errors, Mbed TLS user config | No `west build -t hardenconfig`; no threat model | Run [hardenconfig](https://docs.zephyrproject.org/latest/security/hardening.html) on RT118x defconfig; record accepted risks |
| **Wireless / RED** | RT118x port is **Ethernet (NETC)** only | RED radio cybersecurity is N/A for this MCU image; wireless on another SoC is out of scope here | Track radio module compliance separately if product includes Wi-Fi/BT |

### How Mender helps (update & vulnerability **process** — not compliance by itself)

Mender addresses CRA themes around **secure distribution** and **operational vulnerability response** when used as part of a broader ISMS — it does not satisfy SBOM, support-period declaration, or Article 14 reporting on its own.

1. **Security updates without physical access** — Devices poll Hosted Mender (`CONFIG_MENDER_CLIENT_UPDATE_POLL_INTERVAL`); accepted devices receive `zephyr-image` deployments. Meets the *mechanism* side of Annex I Part II (“securely distribute updates”) once hardware OTA is validated.
2. **Controlled rollout** — Device groups (`simulator`, `rt1180-lab`) and deploy targets (`device`, `device_type`, `group`) limit blast radius. **Phased rollout** needs Professional/Enterprise ([deployment docs](https://docs.mender.io/overview/deployment)); lab uses Micro + manual discipline (one deployment at a time).
3. **Fleet inventory** — Periodic inventory ([docs](https://docs.mender.io/overview/inventory)) reports artifact name and build metadata — input to “which firmware is exposed?” during CVE triage. Extend with explicit `zephyr_version` / `sbom_id` attributes when SBOM pipeline exists.
4. **Authenticated channel** — Per-device keypair + TLS to server ([security overview](https://docs.mender.io/overview/security)); pending-device acceptance reduces drive-by registration. Production must move keys to **ELE (S3)**.
5. **Deployment evidence** — ISO8601 logs (`CONFIG_LOG_OUTPUT_FORMAT_ISO8601_TIMESTAMP`) support post-incident audit of update success/failure ([Mender deployment logs](https://docs.mender.io/overview/deployment)).
6. **Signed firmware payload** — Mender MCU does **not** verify Mender artifact signatures; **MCUboot** verifies `zephyr.signed.bin`. Production requires non-demo `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE` and offline signing.
7. **CVE workflow (manufacturer-owned)** — Mender is not an SBOM/CVE scanner. Typical loop: SBOM + NVD/Zephyr advisory → impact analysis → build fixed Zephyr → new artifact → Mender deployment to affected `device_type`/group. Mender accelerates **remediation delivery**, not discovery.

### Roadmap alignment (EdgeLock + Mender + Zephyr)

| Track | Milestone | CRA-oriented outcome |
|-------|-----------|----------------------|
| **S0 (lab)** | `native_sim` OTA **done**; RT118x **Phase 2** OTA | Prove update path; not production security |
| **S1** | ELE TRNG in Zephyr DTS | Remove timer RNG; credible key generation / TLS |
| **S2** | PSA crypto driver on ELE | Hardware-backed crypto primitives |
| **S3** | Mender platform: opaque ELE devauth signing | Meets device-identity protection expectations |
| **S4** | AN14861 provisioning; lifecycle policy | Manufacturing-grade trust anchor |
| **Mender** | Hardware `zephyr-image` deploy + swap test | Demonstrate security update delivery |
| **Mender** | Inventory attributes ↔ release SBOM ID | Faster vulnerability impact assessment |
| **Mender** | Optional: signed artifacts at CI + future MCU client support | Defence in depth on `.mender` wrapper (today: MCUboot only) |
| **Zephyr** | Choose **LTS** or document backport policy for support period | Sustainable security fixes across 5+ years |
| **Zephyr** | `west spdx` in release CI; MCUboot SBOM merged | Annex I Part II SBOM |
| **Zephyr** | `hardenconfig` + PSIRT registry | Secure-by-default configuration baseline |
| **Organisation** | Support period statement, risk assessment, Art. 14 runbook | CRA documentation and reporting readiness |

### UK market — PSTI and RED (Ethernet-only note)

- **EU RED (2014/53/EU)** applies to **radio** equipment. The RT118x CM33 images in this project use **wired Ethernet (NETC)** only — no on-chip Wi-Fi/BT in this firmware scope. RED cybersecurity articles matter when the *product* includes radio hardware (module or companion SoC), not for Ethernet-only MCU firmware alone.
- **UK PSTI** ([Act 2022](https://www.legislation.gov.uk/ukpga/2022/46/contents)) applies to many **internet-connectable** consumer products — **including Ethernet** — with baseline duties (unique passwords, vulnerability contact, **minimum security update period**). PSTI is narrower than CRA but overlaps on updates and disclosure. A **B2B industrial** RT118x gateway may fall outside PSTI consumer scope — confirm per product; CRA/PSTI alignment is discussed in [Zephyr CRA](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html) and UK [PSTI regulations](https://www.legislation.gov.uk/uksi/2023/1007/contents/made).
- **Practical split:** treat this repo as the **MCU firmware CRA/PSTI technical baseline** (OTA + boot integrity + future ELE); treat **UK Statement of Compliance / CE marking** as product-line deliverables above this integration layer.

## Build outputs

| File | Role |
|------|------|
| `build/mcuboot/zephyr/zephyr.bin` | MCUboot bootloader image (flash at `boot_partition`) |
| `build/mender-mcu-integration/zephyr/zephyr.bin` | Unsigned application (internal; slot payload before signing) |
| `build/mender-mcu-integration/zephyr/zephyr.signed.bin` | MCUboot-signed app image (slot update payload) |
| `build/mender-mcu-integration/zephyr/zephyr.mender` | Mender artifact (`zephyr-image`, name `dev-1`) for Hosted Mender upload |

For FRDM builds, substitute `build-frdm-rt1186/` for `build/` in the paths above.

Artifact metadata from a successful host build: device type `mimxrt1180_evk` (EVK) or `frdm_imxrt1186` (FRDM), artifact type `zephyr-image`.

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

## Device groups — `native_sim` / `simulator`

**Static group (inventory):** name **`simulator`** (Mender has no separate group UUID — the name is the identifier). The accepted `native_sim` device (`03fac8bb-567a-4008-b6d5-57efb522d1c3`, MAC **02:00:5e:00:53:31**) is assigned to this group. Groups are created implicitly when the first device is added (`PUT /api/management/v1/inventory/devices/{id}/group` or `PATCH .../groups/{name}/devices`).

**Deploy to the group** (after artifact upload):

```bash
MENDER_DEPLOY_TARGET=group ./scripts/create-native-sim-deployment.sh
# optional override (default is simulator):
MENDER_DEPLOY_TARGET=group MENDER_DEVICE_GROUP=simulator ./scripts/create-native-sim-deployment.sh
```

REST equivalent: `POST /api/management/v1/deployments/deployments/group/simulator` with JSON `{ "name", "artifact_name", "force_installation" }`.

**Other targets** (same script): `MENDER_DEPLOY_TARGET=device` (single device ID) or `device_type` (ad-hoc filter on `device_type=native_sim` + accepted).

**Saved filter (dynamic search):** filter name **`simulator-native_sim`**, id **`6a2d518a7f9c80463ad089a0`** — `device_type` `$eq` `native_sim` and status accepted. Useful in the UI device list; deployments still use static **group** or inline **filter** on the deployments API, not the saved filter id.

**API / CLI limits:** `mender-cli` 2.0.0 has no `groups` subcommand — use **curl + PAT** for group membership. Each device belongs to **at most one** static group. OpenAPI marks saved filters as Enterprise; this tenant can create them, but do not rely on that on all Hosted tiers.


## Device groups — `mimxrt1180_evk` / `rt1180-lab`

**Static group (inventory):** name **`rt1180-lab`**. Mender does not list empty groups — **`rt1180-lab` appears in the UI when the first accepted RT118x lab device is assigned** (`PUT /api/management/v1/inventory/devices/{id}/group` with body `{"group": "rt1180-lab"}` or `PATCH .../groups/rt1180-lab/devices`). Until hardware arrives, only the **`simulator`** group is in use for Phase 0b (`native_sim`).

**Deploy to the lab group** (after EVK build produces `build/mender-mcu-integration/zephyr/zephyr.mender`):

```bash
./scripts/create-rt1180-deployment.sh
# optional overrides:
MENDER_DEPLOY_TARGET=group MENDER_DEVICE_GROUP=rt1180-lab ./scripts/create-rt1180-deployment.sh
MENDER_DEPLOY_TARGET=device MENDER_DEVICE_ID=<uuid> ./scripts/create-rt1180-deployment.sh
```

**When to use which group:** **`simulator`** — Zephyr `native_sim` noop-update smoke tests (no hardware). **`rt1180-lab`** — physical **RT118x CM33** images (`mimxrt1180_evk` or `frdm_imxrt1186` device types). Prefer **`MENDER_DEPLOY_TARGET=device_type`** or per-device UUID so EVK and FRDM artifacts are not conflated.

REST equivalent: `POST /api/management/v1/deployments/deployments/group/rt1180-lab` with JSON `{ "name", "artifact_name", "force_installation" }`.

## RT1180 board configuration notes

Files: `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf`, `mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf` (same NETC/Mender/RNG settings; FRDM storage partition in `zephyr/boards/nxp/frdm_imxrt1186/frdm_imxrt1186.dtsi`)

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
| Hosted Mender client (no hardware) | Phase 0b | **Done** (2026-06-13) — build, DHCP, accept, noop OTA; see [Phase 0b](#phase-0b--native_sim-smoke-test-no-evk) |
| CM7 boot verify | Phase 3 | Separate sysbuild image; **do not** flash over Mender CM33 build until slot1 repartition |
| CM7 OTA via CM33 | Phase 4 | Future — needs `cm7_partition` + custom update module |

## Zephyr testing plan

Actionable validation checklist for RT1180 CM33 Mender bring-up and (separately) CM7 dual-core boot. Run from the **West workspace root** (parent of `mender-mcu-integration/`). Cross-references: [Build (canonical)](#build-canonical), [Flash](#flash), [Hosted Mender](#hosted-mender), [CM7 boot and OTA](#cm7-boot-and-ota), [RT1180 board configuration notes](#rt1180-board-configuration-notes).

### Prerequisites (all phases)

| Item | Requirement | Reference |
|------|-------------|-----------|
| West workspace | `west init -l mender-mcu-integration && west update` | [Workspace layout](#workspace-layout) |
| Zephyr SDK | 1.0.x for v4.4.0, `ZEPHYR_SDK_INSTALL_DIR` set | [Prerequisites](#prerequisites) |
| Python env | Zephyr venv active; `pyelftools`, `intelhex`, `cbor2` | MCUboot signing / artifacts |
| Local secrets | `mender-mcu-integration/mender-local.conf` (gitignored) with tenant token | [Secrets](#secrets-do-not-commit) |
| Board Kconfig fragment | Auto-applied: `mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf` | NETC, RNG workaround, Mender storage |
| Hosted Mender account | Micro tier; device type `mimxrt1180_evk`; update module `zephyr-image` | [Hosted Mender](#hosted-mender) |
| Workstation PAT | Hosted Mender **personal access token** (not device tenant token) | [Hosted Mender workstation tools](#hosted-mender-workstation-tools) |

**Tools on PATH** (from workspace root):

```bash
export PATH="$(pwd)/.tools/bin:$PATH"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/zephyr-sdk-1.0.1}"
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
  -DCONFIG_MENDER_ARTIFACT_NAME="dev-1"
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

### Phase 0b — `native_sim` smoke test (no EVK)

**Status: COMPLETE (2026-06-13).** Verified on this workspace: build, TAP/DHCP/NAT, Hosted Mender auth, and noop-update deployment.

Goal: exercise the Mender MCU client, TLS, and Hosted Mender polling on the host **before** RT1180 hardware is available. Uses Zephyr `native_sim` with the **noop-update** module (no MCUboot, no `zephyr-image` OTA). Upstream notes: [mender-mcu-integration/README.md](README.md#native-simulator).

**Verified outcomes (2026-06-13)**

| Step | Result |
|------|--------|
| Build | `./scripts/build-native-sim.sh` — GCC 11 NSI fix; outputs `build-native_sim/zephyr/zephyr.exe` |
| Network | `./scripts/run-native-sim-network.sh start` — `nat.conf`; stop uses `--config nat.conf` |
| Run | `./scripts/test-mender-native-sim.sh` — DHCP **192.0.2.24** on `zeth0`; Mender client init; device type **`native_sim`** |
| First check-in | HTTP **401** until device **accepted** in Hosted Mender UI (pending device, MAC **02:00:5e:00:53:31**) |
| Auth OK | After accept: log line **"No deployment available"** = tenant token auth working |
| Noop OTA | `./scripts/create-native-sim-deployment.sh` — serial: download → install → `deployment_status_cb: success` |

**Why not plain `west build -p`?** `native_sim` links the NSI runner with `-m32`. NSI sets `NSI_CC` from the host `CMAKE_C_COMPILER`. Ubuntu 24.04 default **GCC 13** has no `-m32` `libgcc`, so a pristine configure fails at the final link with `cannot find -lgcc`. Pin the host compiler to **GCC 11** (with `gcc-11-multilib`) for configure and link.

**Host prerequisites (in addition to [Prerequisites](#prerequisites-all-phases))**

| Item | Requirement |
|------|-------------|
| Host GCC 11 + multilib | `sudo apt install gcc-11 g++-11 gcc-11-multilib` (alternative: `gcc-multilib` / `g++-multilib` for default GCC 13 if you prefer not to use GCC 11) |
| `net-tools` | Clone [zephyrproject-rtos/net-tools](https://github.com/zephyrproject-rtos/net-tools) to `tools/net-tools` at the workspace root (not in this West manifest). |
| TAP / net setup | `./scripts/run-native-sim-network.sh start` (recommended), or manual `tools/net-tools/net-setup.sh --config nat.conf` from `tools/net-tools/` — see [Scripts inventory](#scripts-inventory). Without DHCP on `zeth`, the client stays on “Waiting for network up…”. |
| Secrets | Same gitignored `mender-mcu-integration/mender-local.conf` (tenant token + `CONFIG_MENDER_SERVER_HOST_US=y`). |

All paths below are from the **West workspace root** (directory that contains `mender-mcu-integration/` and `.west/`).

- [x] **0b.0** Clone `net-tools` once (not in the West manifest; not committed):

```bash
cd /path/to/your/west/workspace   # parent of mender-mcu-integration/
mkdir -p tools
git clone https://github.com/zephyrproject-rtos/net-tools.git tools/net-tools
test -x tools/net-tools/net-setup.sh || chmod +x tools/net-tools/net-setup.sh
```

In a **separate terminal**, start TAP (needs root; leave running while `zephyr.exe` runs):

```bash
cd /path/to/your/west/workspace
sudo ./scripts/run-native-sim-network.sh start
# stop when done: sudo ./scripts/run-native-sim-network.sh stop
```

**Recommended wrapper** — `run-native-sim-network.sh` `cd`s into `tools/net-tools` and passes `--config nat.conf` on both start and stop (cleans `dnsmasq` + NAT rules). Manual equivalent:

```bash
cd tools/net-tools
sudo ./net-setup.sh --config nat.conf
```

**This blocks — that is normal.** With no `start`/`stop` argument, the script creates `zeth`, sources the config, then sleeps until you press **Ctrl-C**. Run `./scripts/test-mender-native-sim.sh --run-only` (or `./build-native_sim/zephyr/zephyr.exe`) in **another** terminal.

If a previous run left `zeth` behind: `sudo ./scripts/run-native-sim-network.sh stop`, or `sudo ip link delete zeth`.

Configure host NAT so the sim can reach Hosted Mender: [Setting up Zephyr and NAT/masquerading on host to access internet](https://docs.zephyrproject.org/latest/connectivity/networking/qemu_setup.html#setting-up-zephyr-and-nat-masquerading-on-host-to-access-internet).

- [x] **0b.1** Pristine build (separate directory from EVK `build/`). **Recommended — one command, no failed link step:**

```bash
source zephyr/zephyr-env.sh
./scripts/build-native-sim.sh
```

The script runs `west build --cmake-only`, pins `NSI_CC` to `gcc-11` via `fix-native-sim-link.sh`, then completes the build (no failed NSI link step). Incremental rebuild: `./scripts/build-native-sim.sh --incremental`.

Equivalent manual west command (no script):

```bash
source zephyr/zephyr-env.sh
CC=/usr/bin/gcc-11 CXX=/usr/bin/g++-11 west build -p -d build-native_sim --board native_sim mender-mcu-integration -- \
  -DEXTRA_CONF_FILE=mender-local.conf
```

`EXTRA_CONF_FILE` is relative to the application directory (`mender-mcu-integration/`).

- [x] **0b.2** Verify NSI used GCC 11 (optional):

```bash
grep '^NSI_CC' build-native_sim/zephyr/NSI/nsi_config
# NSI_CC:=ccache /usr/bin/gcc-11
```

If you already ran a failing pristine build without GCC 11, either remove `build-native_sim` and re-run **0b.1**, or run `./scripts/fix-native-sim-link.sh` then `west build -d build-native_sim`.

- [x] **0b.3** Outputs:

```bash
test -f build-native_sim/zephyr/zephyr.elf
test -f build-native_sim/zephyr/zephyr.exe
```

- [x] **0b.4** Run (after `run-native-sim-network.sh start` in a separate terminal):

```bash
./scripts/test-mender-native-sim.sh --run-only
# or
./build-native_sim/zephyr/zephyr.exe
# or
west build -d build-native_sim -t run
```

- [x] **0b.5** Accept pending device in Hosted Mender UI (**Devices → Pending**). Pending MAC **02:00:5e:00:53:31**; first poll may log **401** until accepted. After accept: **"No deployment available"** confirms auth.

- [x] **0b.6** Noop deployment:

```bash
./scripts/create-native-sim-deployment.sh
# deploy to static group simulator (all devices in that group):
MENDER_DEPLOY_TARGET=group ./scripts/create-native-sim-deployment.sh
# if a prior deployment is still in progress:
./scripts/create-native-sim-deployment.sh --abort-inprogress
```

See [Device groups — `native_sim` / `simulator`](#device-groups--native_sim--simulator).

**Pass (sim):** Binary starts; DHCP on `zeth0` (e.g. **192.0.2.24**); device accepted; noop deploy logs download → install → `deployment_status_cb: success`.

**Troubleshooting — HTTP 401 on first check-in**

| Symptom | Cause | Action |
|---------|-------|--------|
| Repeated **401** in serial/API | Device not yet **accepted** | Hosted Mender UI → **Devices → Pending** → Accept (MAC **02:00:5e:00:53:31** for default sim identity) |
| **401** after accept | Wrong credential in image | Rebuild with tenant token in gitignored `mender-local.conf` — not workstation PAT |

**Troubleshooting — deployment 409 (Deployment aborted)**

| Symptom | Cause | Action |
|---------|-------|--------|
| **409** / deployment aborted | **Overlapping deployments** to the same device — not a bad artifact | Wait for in-progress deploy to finish, or `./scripts/create-native-sim-deployment.sh --abort-inprogress`; run **one deployment at a time** |

**Troubleshooting — stuck on “Waiting for network up…”**

| Check | Command / expectation |
|-------|------------------------|
| Build is Mender app (not a stray hello_world tree) | `grep configuration-dir build-native_sim/build_info.yml` → `mender-mcu-integration`. `Hello World! native_sim/native` is printed by `main.c` in this app — normal. |
| `zeth` exists and is UP | `ip link show zeth` → `state UP`; host side is usually `192.0.2.2/24`. |
| DHCP server on `zeth` | `pgrep -a dnsmasq` — must be running. Default `zeth.conf` via `net-setup.sh` does **not** start DHCP. |
| NAT (Hosted Mender / internet) | `sysctl net.ipv4.ip_forward` → `1`; `iptables -t nat -L POSTROUTING -n` includes MASQUERADE for `192.0.2.0/24` (needs root to list). |

**What “network up” means:** `netup_wait_for_network()` (`src/utils/netup.c`) calls `net_dhcpv4_start()` on the default interface and waits on a semaphore until `NET_EVENT_IPV4_ADDR_ADD` with address type `NET_ADDR_DHCP`. No DHCP offer → hang at “Waiting for network up…”.

**Fix A (recommended):** `./scripts/run-native-sim-network.sh start` (wraps `nat.conf` — TAP + NAT + `dnsmasq`). Manual equivalent:

```bash
cd tools/net-tools
sudo ./net-setup.sh stop --config nat.conf    # if zeth already exists
sudo ./net-setup.sh --config nat.conf
```

Leave that terminal blocked (or use `start` / `stop`). In another terminal: `./scripts/test-mender-native-sim.sh --run-only`.

**Fix B (plain `net-setup.sh` already running in terminal 1):** in terminal 2:

```bash
cd tools/net-tools
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -C POSTROUTING -s 192.0.2.0/24 -j MASQUERADE 2>/dev/null ||   sudo iptables -t nat -A POSTROUTING -s 192.0.2.0/24 -j MASQUERADE
sudo iptables -P FORWARD ACCEPT
sudo dnsmasq -C dnsmasq_nat.conf -x /var/run/dnsmasq_zeth.pid -d
```

Restart `zephyr.exe` after `dnsmasq` is up. Success logs include `Address[…]` / lease time, then Mender client activity.

**What sim validates vs EVK (Phases 0–2)**

| Area | `native_sim` | RT1180 EVK |
|------|----------------|------------|
| Mender client / TLS / tenant token | Yes | Yes |
| Hosted Mender inventory & polling | Yes (with net-setup) | Yes (Ethernet DHCP) |
| MCUboot + signed image | No (noop update module) | Yes |
| `zephyr-image` artifact OTA | No | Yes |
| NXP NETC Ethernet, SW5, flash | No | Yes |
| CM7 / dual-core | No | Phase 3+ |

---

### vemu (nRF5340) — limited scope

**Status:** build/run verified; **not** a substitute for Phase 0b Hosted Mender testing.

Swedish Embedded **vemu** freeware runs nRF5340 firmware in the browser or headless Node — **no network stack**, so the Mender client cannot reach Hosted Mender. Use **`native_sim`** for OTA/auth validation; use vemu for quick nRF5340 build sanity and Mender app boot without hardware.

Board fragment: `mender-mcu-integration/boards/nrf5340dk_nrf5340_cpuapp.conf` — noop update module, no artifact generation, test RNG (no nRF5 entropy in vemu).

| Script | Role |
|--------|------|
| `./scripts/test-vemu.sh` | `hello_world` on `nrf5340dk/nrf5340/cpuapp` + headless vemu |
| `./scripts/run-vemu-demo.sh` | Build hello_world + print vemulator.com load steps |
| `./scripts/build-mender-vemu.sh` | Mender integration app for vemu board target |
| `./scripts/test-mender-vemu.sh` | Build + run Mender image in vemu (increase `--frames` — image is slow) |

**Pass (vemu):** ELF builds; vemu runs without immediate fault. **Does not pass:** Hosted Mender check-in or deployment (no network in freeware vemu).

---

### Phase 1 — EVK flash (CM33 Mender image)

**Status: TBD — pending MIMXRT1180-EVK arrival.** Phase 0b (`native_sim`) is done; hardware bring-up (Phases 1–4) has not started.

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


### Phase 1 — FRDM flash (CM33 Mender image)

**Status: TBD — bench validation on FRDM-IMXRT1186.** Build scripts and board Kconfig fragment are in-tree; flash/Ethernet/Mender OTA on hardware not yet signed off.

Goal: same as EVK Phase 1 — MCUboot + signed app, serial console, NETC DHCP before Hosted Mender.

**Preconditions:** `west update` (Zephyr **v4.4.0**); Phase 0 sysbuild pattern; `mender-local.conf`; jumper **J60 = 1:OFF 2:OFF 3:ON** for CM33 ([programming section](https://docs.zephyrproject.org/latest/boards/nxp/frdm_imxrt1186/doc/index.html#programming-and-debugging)).

- [ ] **1F.1** Build:

```bash
./scripts/build-rt1186-frdm.sh
```

- [ ] **1F.2** Flash and reset:

```bash
west flash -d build-frdm-rt1186
```

- [ ] **1F.3** Serial on **MCU-Link** @ **115200 8N1** — MCUboot then Zephyr/Mender logs.
- [ ] **1F.4** Ethernet on **`swp0` or `swp2`** (1 Gbps TSN user ports); confirm DHCP (`net iface` / logs). Do not use DSA CPU/conduit ports (`swp4`, `eth0`).

**Pass:** LinkServer or J-Link flash OK; CM33 console alive; `swp0` or `swp2` obtains IPv4.

**Fail triage:** same NETC/RNG themes as EVK — see [RT1180 board configuration notes](#rt1180-board-configuration-notes); wrong port → try other TSN connector; CM7 debug → J60 must be CM33 position.

**Mender:** device type **`frdm_imxrt1186`**; assign accepted device to **`rt1180-lab`**; deploy with `./scripts/create-rt1186-frdm-deployment.sh` or `MENDER_DEPLOY_TARGET=device_type ./scripts/create-rt1186-frdm-deployment.sh`.


### Phase 2 — Hosted Mender (CM33 OTA)

Goal: device accepted, artifact deployed, inventory reported, MCUboot swap confirmed.

**Preconditions:** Phase 1 pass; artifact from Phase 0; Hosted Mender PAT in environment (never commit):

```bash
export MENDER_SERVER_URL="https://hosted.mender.io"
export MENDER_PAT="<your-hosted-mender-pat>"
export PATH="$(pwd)/.tools/bin:$PATH"
```

- [ ] **2.1** Boot device with network; within ~30–60 s device appears as **pending** (Micro tier; type `mimxrt1180_evk` or `frdm_imxrt1186` matching the flashed board).
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
| **native_sim 401** | Check-in rejected before accept | Accept pending device in UI; 401 is normal on first run |
| **native_sim 409** | Deployment aborted | Overlapping deployments — `--abort-inprogress`; one deploy at a time |

## Hosted Mender workstation tools

Store your Hosted Mender **personal access token** in `mender-mcu-integration/mender-pat-local.conf` (gitignored; same directory as `mender-local.conf`). The file is a **single line** with the raw PAT JWT (not tenant token, not Kconfig). Example login from the workspace root:

```bash
mender-cli login --password "$(cat mender-mcu-integration/mender-pat-local.conf)"
```


## Zephyr documentation (MCP)

Use the **user-zephyr-docs** MCP server in Cursor for Zephyr Kconfig, devicetree, and API lookups while iterating on this port.

## Status

Track progress with the **[Zephyr testing plan](#zephyr-testing-plan)** checkboxes.

| Step | Testing plan | State |
|------|--------------|-------|
| West workspace + dependencies | Prerequisites | Done |
| Host sysbuild + artifact | Phase 0 | Done |
| `native_sim` Mender smoke + noop OTA | Phase 0b | **Done** (2026-06-13) — build, DHCP, accept, noop deploy |
| vemu nRF5340 build/run (no network) | vemu | Done (limited — use Phase 0b for Hosted Mender) |
| EVK flash + serial | Phase 1 | **TBD** — pending MIMXRT1180-EVK arrival |
| FRDM flash + serial | Phase 1 | **TBD** — FRDM-IMXRT1186 (build scripts + board conf ready) |
| Ethernet DHCP | Phase 1 | **TBD** — pending EVK |
| Hosted Mender accept + OTA | Phase 2 | **TBD** — pending EVK |
| MCUboot swap on deploy | Phase 2.6 | **TBD** — pending EVK |
| CM7 `mbox_data` dual boot | Phase 3 | **TBD** — pending EVK |
| CM7 OTA via CM33 | Phase 4 | Future |

## Next steps

1. When **MIMXRT1180-EVK** arrives, run **Phase 1** (flash, serial, Ethernet/DHCP).
2. Complete **Phase 2** on RT1180 EVK (accept device, upload `zephyr.mender`, deploy, confirm swap). Phase 0b on `native_sim` already validated client auth and noop OTA on the host.
3. When needed, run **Phase 3** in `build-mbox` — then reflash Mender image before resuming Phase 2.
4. Push local commits when ready to share the RT1180 port upstream or to a fork remote.

## Commits

| Repo | Commit | Notes |
|------|--------|-------|
| `mender-mcu-integration/` | `76ebea3` | RT1180 board conf + `.gitignore` for local secrets/tools/build; **local, unpushed** (`main` ahead of `origin/main` by 1) |
