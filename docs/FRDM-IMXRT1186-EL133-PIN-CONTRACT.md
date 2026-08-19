# FRDM-IMXRT1186 ↔ EL133UF1 pin contract (lab)

**Status:** proposed loom — **not** FRDM silicon proof.  
**Proof class after flash:** `FRDM` only for GPIO/SPI/BUSY; product SoC remains RT1170.  
**Lane:** `spectra6-frdm` · `/data_drive/dd/zephyr-rt1170-eink-spectra6-frdm`

Do **not** unplug the live Jaguar demo. Use a spare 13.3″ Spectra 6 + 60-pin FFC
breakout. FRDM supplies **3.3 V I/O only**. HV / VCOM / AVDD stay on an E Ink
kit or a parked Jaguar PMIC. Common GND is mandatory.

## Bus (match Jaguar Linux, not the E Ink EVK quad default)

| Item | Lab value |
|------|-----------|
| Controller | LPSPI2 (already `okay` on FRDM) |
| Width | **1-bit** (`SPIM = 0x00`) |
| Mode | SPI mode 0, MSB first |
| Clock | 5 MHz |
| CS | **GPIO** CS0 + CS1 (not LPSPI2 PCS0) |
| Extra | DC, RESET#, BUSY (ready = high) |

Strap panel **BS[1:0] = 01** (4-line SPI). EVK switches at `11` select **quad**
and will look dead on this 1-bit path.

## FRDM headers (NXP QSG + Zephyr pinctrl)

LPSPI2 pads are on **J3** (motor / FRDM header), not Arduino D11–D13.

| Panel | FFC (E Ink 60-pin notes) | FRDM silk | SoC pad | Zephyr DT |
|-------|--------------------------|-----------|---------|-----------|
| SCLK | 28 SCL | **J3-18** | `GPIO_AON_19` | `&lpspi2` SCK |
| MOSI | 29 SI0 | **J3-16** | `GPIO_AON_18` | `&lpspi2` SOUT |
| MISO | — (unused, 1-bit) | J3-14 | `GPIO_AON_17` | SIN, leave open |
| *(do not use)* | — | J3-12 | `GPIO_AON_16` | LPSPI2 PCS0 |
| CS0 (master) | 27 CSB_M | **J1-6 / D2** | `GPIO_EMC_B1_12` | `&gpio2 12` ACTIVE_LOW |
| CS1 (slave) | 58 CSB_S | **J1-8 / D3** | `GPIO_EMC_B1_13` | `&gpio2 13` ACTIVE_LOW |
| D/C | 26 D_CX | **J1-10 / D4** | `GPIO_EMC_B1_14` | `&gpio2 14` ACTIVE_HIGH |
| RESET# | 24 RES# | **J1-12 / D5** | `GPIO_EMC_B1_15` | `&gpio2 15` ACTIVE_LOW |
| BUSY | 25 BUSY_N | **J2-2 / D8** | `GPIO_EMC_B1_17` | `&gpio2 17` ACTIVE_HIGH |
| GND | several | J2-14 / J1-7 | — | common with PMIC |
| VDDIO 3.3 V | 19 VDDIO | J2-16 (3V3) **or** PMIC 3V3 — not both | — | |

FFC pin numbers come from `eink-spectra6/reference/*SCHEMATIC*` (E Ink EVK
60-pin). Confirm on the **spare** FFC / Jaguar connector before soldering.

Zephyr `spi_loopback` comments say J3-14 = SOUT and J3-16 = SIN. **QSG +
pinctrl disagree** (14 = SIN / `GPIO_AON_17`, 16 = SOUT / `GPIO_AON_18`).
Trust QSG+pinctrl; a loopback jumper 14↔16 still works either way.

Avoid **D0/D1** (`GPIO_AON_08/09`, CM33 console) and **D6** (`GPIO_AD_13`, CM7 UART).

## Overlay / build

| File | Role |
|------|------|
| `mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_eink_el133.overlay` | LPSPI2 + GPIO map; EL133 `status = okay` |
| `mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_eink_el133.conf` | SPI/GPIO/e-ink shell; **no** full framebuffer |
| `scripts/build-rt1186-frdm-eink.sh` | Sysbuild wrapper |

First bench: RESET pulse, BUSY idle-high, then init registers. No 960 kB DTM
until that passes.

## Product loop (Jaguar-like behaviour)

Same **e-tabelone** contract as Jaguar `eink-scheduler-rust`, already in this
tree (`eink_http.c` / `eink_scheduler.c`):

wake → net → `GET /node/v0/device/{id}/config` → fetch due then gallery
images → `POST …/telemetry` → run cron → paint Spectra 6 → sleep.

| Piece | Jaguar (i.MX93 Linux) | This Zephyr tree |
|-------|----------------------|------------------|
| Config / jobs | `GET …/config` | same path |
| Images | JPEG/PNG + `el133uf1_demo --process-image` | **ES6F** (or ES6F.lz4); JPEG/PNG rejected on device |
| Telemetry | `POST …/telemetry` | same |
| Offline cache | `/var/lib/eink-scheduler/` | LittleFS store |
| Sleep / rails | **MCXC PMU** UART (`DISP_EN`, Wi‑Fi rails, `deep_sleep_all_off`) | **No PMU MCU.** Product: RT SoC GPIOs + SNVS/SRTC (`eink_power.*`). FRDM lab: external panel HV; no MCXC |

**No PMU controller chip (Alex 2026-08-19):** Jaguar’s MCXC is **not** on the
Zephyr i.MX RT path (FRDM-1186 lab or RT1170 product). Do not speak UART
`power disp` / `power wifi` or assume a companion MCU. Panel/Wi‑Fi/NOR gates
are RT GPIOs when the overlay provides them; without those DT properties the
software contract still logs/status-only (`eink_power.h`). Lab HV stays on the
E Ink kit / parked Jaguar PMIC — FRDM does not generate ±16 V.

**Alex 2026-08-19 (amended same day):** FRDM-IMXRT1186 is the **one lab board**
for Spectra 6 SPI **and** Hosted **Mender MCU** over **NETC Ethernet** (not
Foundries; not MIMXRT1180-EVK unless we add an EVK loom). Portal + schedule
stay on **native_sim** until e-tabelone HTTP is turned back on. Renode does
**not** prove Mender (NETC/PHY unmodelled). Dual-slot swap on silicon is still
**FRDM** proof, not `renode`.

**Renode (same day):** UART smoke of this FRDM e-ink ELF is in-tree
(`./scripts/renode-frdm-eink-uart.sh`). Proof class **`renode`**. It does
**not** model Spectra 6, NETC/PHY, or live e-tabelone. Portal TAP stays
`native_sim`.

## Explicitly out of scope

- Yocto / Foundries on FRDM (no MMU)
- FlexSPI / quad SPI
- Product RT1170 pinmux (this map does **not** transfer)
- Live Jaguar as a jumper donor
