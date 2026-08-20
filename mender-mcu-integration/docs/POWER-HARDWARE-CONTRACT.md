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

## EVK lab only

- `APP_EINK_FULL_FRAMEBUFFER` + SDRAM for panel electrical bring-up
- Ethernet may be used; not a product interface
- SP10/SP15 retained-sleep profiles (if used) carry **no** battery-life claim
