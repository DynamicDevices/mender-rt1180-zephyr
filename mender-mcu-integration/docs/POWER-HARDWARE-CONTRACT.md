# RT1170 battery board — power / CM4 hardware contract

Firmware implements the software side of this contract. Schematic ownership is
Michael/Ollie.

**No companion PMU MCU** on this path (Alex 2026-08-19). Jaguar’s MCXC does
not exist on FRDM-IMXRT1186 or the RT1170 product board. Rails and SNVS are
the i.MX RT SoC’s problem (GPIOs + SRTC), not a second firmware image. Controlled questions live in
[`AESL-HW-RT1170-EINK-SPEC`](https://github.com/active-esl/specifications/tree/main/hw/AESL-HW-RT1170-EINK-SPEC)
(Rev ≥ 0.2, questions 12–16).

## Field sleep (required)

| Item | Requirement |
|------|-------------|
| Mode | **True SNVS / BOM** — only `VDD_SNVS_IN` (or RT118x `VDD_BBSM_IN`) + 32.768 kHz RTC alive |
| Board kill | **Main 3.3 V regulator off** (BOM). `DCDC_IN` / `VDD_LPSR_IN` (RT118x: `VDD_AON_IN`) are driven from that 3V3 — they collapse with it; **no separate FET on those nets required** |
| BBSM stay-alive | `VDD_BBSM_IN` / `VDD_SNVS_IN` remains powered (FRDM: **always-on LDO**; product: coin cell or dedicated always-on) |
| Also off | Panel, data NOR, IW612 (battery mode) |
| Board sleep | ≤50 µA @ 25 °C (stretch ≤20 µA) at battery input |
| Wake (BOM) | **BBSM** (RTC and/or ONOFF / `WAKEUP`) re-enables main 3V3 → **full power-on reset**, not a WFI resume. FRDM: **SW1** ONOFF; SoC `WAKEUP` pad high. Optional IW612 host-wake (WoWLAN) when that path is rated |
| Boot | Every BOM wake is a **full POR** / cold boot (RT1170 CM7 / RT118x CM33) |

Not acceptable as field sleep: Zephyr SP10 (~mA), SP15 with CM4 suspend
(~0.23 mA LPSR alone).

## CM4 policy (required)

| Item | Requirement |
|------|-------------|
| Production | CM4 **held in reset**; no second-core image |
| Memory | CM4 256 KiB remains reserved (not reclaimed for framebuffer) |
| Wake companion | **Do not** use CM4 retained/SP15 as RTC/wake helper |
| Firmware gate | `CONFIG_SECOND_CORE_MCUX` must stay disabled; `eink_power_assert_cm4_held()` |

## IW612

| Mode | Behaviour |
|------|-----------|
| Battery (default) | Hard-gated between scheduled cloud wakes |
| WoWLAN | Low-power rail + SNVS-capable host-wake; separately rated life |

## Rails software expects to control

- Panel / EL133 PMIC (PON/DRF/POF then hard-gate)
- IW612 (hard-gate + WoWLAN arm)
- Data NOR (deep-power-down then gate)
- Optional SDRAM (DNP preferred; if populated must hard-gate, never self-refresh sleep)

## RT1186 (FRDM / product) — option D

i.MX RT1186 has **BBNSM** (not classic SNVS LPCR_TOP). Field sleep = **BOM
(battery-only mode)**:

1. Program BBNSM RTC time-alarm (`eink snvs <sec>`).
2. Optional **TOSP** + dumb-PMIC (`eink snvs <sec> cut`) to drop `PMIC_ON_REQ`
   so the **main 3.3 V regulator turns off**. `DCDC_IN` and `VDD_AON_IN` are
   fed from that 3V3 — they do not need their own power gates. **BBSM stays
   up** on FRDM via the **always-on BBSM LDO** (no coin cell required for RTC).
3. GPC WAIT/STOP + WFI alone **does not** kill 3V3 on FRDM. Once truly in
   **BOM** (3V3 regulator off), wake is via the **BBSM block** (RTC and/or
   **ONOFF (SW1)** / **WAKEUP** pad — AN13847 §4.7): main 3V3 returns →
   **full POR**, not a WFI resume. If FRDM USB/jack keeps 3.3 V up, WFI
   returns on the RTC IRQ (`-EAGAIN`) instead. Park NVIC/SysTick first; never
   `k_busy_wait` after SysTick is off.

CM7 stays in reset. No MCXC PMU. Shell does not enable battery duty-cycle
on the Hosted-Mender lab image.

Lab DMM loop (opt-in): `BOM_POWER_LOOP=1 ./scripts/build-rt1186-frdm-eink.sh`
then flash. Settle → TOSP + RTC → POR forever. Rebuild **without** that env
to restore a normal shell. FRDM Gemba 2026-08-20 (LED removed): BOM hold
floor ~**0.26 mA** on the DC-jack Agilent; awake ~210 mA. Not a product µA
claim (FRDM tree + measurement point).

## Product BOM path (map from FRDM → schematic)

SoC nets that must be **down** in field sleep (`DCDC_IN`, RT1170 `VDD_LPSR_IN`
/ RT118x `VDD_AON_IN`) are **fed from the board main 3.3 V regulator**. Product
does **not** need discrete FETs on those SoC pins if the regulator enable is
tied to `PMIC_ON_REQ` (or equivalent). BBSM/SNVS stay-alive is a **separate
always-on** supply (coin cell or always-on LDO on `VDD_BBSM_IN` /
`VDD_SNVS_IN`) — same split as FRDM’s always-on BBSM LDO.

| Step | Firmware | Board |
|------|----------|--------|
| 1 | Program RTC alarm (BBNSM / SNVS) | Always-on domain keeps ticking |
| 2 | Assert **TOSP** / drop `PMIC_ON_REQ` | Main **3V3 regulator off** → SoC main feeds collapse |
| 3 | (optional) GPC WFI while rails fall | CPU dies with the rail; no resume |
| 4 | Wake | RTC and/or service **ONOFF**/`WAKEUP` (and rated IW612 host-wake) re-enables 3V3 |
| 5 | Boot | **Full POR** / cold boot CM7 (RT1170) or CM33 (RT118x) |

Open for Michael (spec Q12–16): confirm product regulator enable polarity /
`POR_B` threshold so TOSP cold-boots cleanly without back-powering gated
domains; pick SNVS-capable net for IW612 host-wake; status LEDs **DNP** on
production (FRDM LED was ~mA in the sleep budget). Target remains ≤50 µA @
25 °C at battery input once the product tree is instrumented.

## FRDM wake-window bench — LittleFS / LZ4 flash I/O (2026-08-22)

Measured on **FRDM-IMXRT1186** HTTP image (`feat/frdm-gpc-wait`), expand-on-display,
after shrinking A/B slots to **2 MiB** each so `/lfs1` is **~11.75 MiB** on the
16 MiB W25Q128 (only external NOR). Logs: `prof: flash_read|write=…`
and shell `eink flash_bench <path> [write]`.

Proof class: **FRDM** (not Renode). Useful for **awake-time / joules** before
BOM sleep — NOR + CPU stay up for these windows; Wi‑Fi can be gated after
download if expand-on-display is used.

| Step | Wall | Bytes | Approx rate | Notes |
|------|------|-------|-------------|--------|
| HTTP body → LittleFS write | **5567 ms** | 658 354 (LZ4) | **~115 KiB/s** | Dominated by FlexSPI program; radio still up in this sync |
| LZ4 expand — flash read | **99 ms** | 658 354 | **~6.3 MiB/s** | Full compressed object into RAM |
| LZ4 expand — flash write | **7972 ms** | 960 032 (ES6F) | **~117 KiB/s** | Scratch `.paint.es6f`; **radio can be off** |
| Display stream — flash read | **173 ms** | 960 000 | **~5.3 MiB/s** | Payload stream to EL133 |
| `flash_bench` read (LZ4 file) | **91 ms** | 658 354 | **~6.9 MiB/s** | Shell; no network |

Sync wall (same run): plan ~1.9 s + primary download/validate ~8.6 s + paint
~11.3 s ≈ **22 s** total (`prof: sync … (v2)`), then sleep/BOM candidate.

**Implications for sleep / power management**

1. **NOR program is the long pole after radio** (~5–8 s per ~0.6–1.0 MiB write
   at ~115 KiB/s). Budget awake time for **at least one LZ4 write + one ES6F
   expand write** on a content change (~13 s flash alone on this chip).
2. Prefer **`APP_EINK_LZ4_EXPAND_ON_DISPLAY`**: hard-gate IW612 as soon as the
   compressed object is accepted; pay the ~8 s expand + paint with Wi‑Fi down.
3. Reads are cheap vs writes (~50–70× faster here) — repaint-from-cache is a
   short NOR window if the ES6F scratch already exists.
4. Field BOM still kills main 3V3 (NOR off). These numbers are **awake-slot**
   costs between BOM cycles, not sleep current.
5. Partition headroom: keep enough free LittleFS for **LZ4 + expanded ES6F at
   once** (~1.6 MiB) during materialize; FRDM map now leaves ~11.75 MiB `/lfs1`
   after 2 MiB slots (see `boards/…_eink_flash_map.overlay`).

Hosted Mender 401 on this lab image is unrelated to the e-tabelone path.

## EVK lab only

- `APP_EINK_FULL_FRAMEBUFFER` + SDRAM for panel electrical bring-up
- Ethernet may be used; not a product interface
- SP10/SP15 retained-sleep profiles (if used) carry **no** battery-life claim
