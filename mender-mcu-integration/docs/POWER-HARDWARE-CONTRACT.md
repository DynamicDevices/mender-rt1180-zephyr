# RT1170 battery board — power / CM4 hardware contract

Firmware implements the software side of this contract. Schematic ownership is
Michael/Ollie. Controlled questions live in
[`AESL-HW-RT1170-EINK-SPEC`](https://github.com/active-esl/specifications/tree/main/hw/AESL-HW-RT1170-EINK-SPEC)
(Rev ≥ 0.2, questions 12–16).

## Field sleep (required)

| Item | Requirement |
|------|-------------|
| Mode | **True SNVS** — only `VDD_SNVS_IN` + 32.768 kHz RTC alive |
| Gated | `DCDC_IN`, `VDD_LPSR_IN`, panel, data NOR, IW612 (battery mode) |
| Board sleep | ≤50 µA @ 25 °C (stretch ≤20 µA) at battery input |
| Wake | SRTC alarm, service button, optional IW612 host-wake (WoWLAN) |
| Boot | Every wake is a **CM7 cold boot** |

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

## EVK lab only

- `APP_EINK_FULL_FRAMEBUFFER` + SDRAM for panel electrical bring-up
- Ethernet may be used; not a product interface
- SP10/SP15 retained-sleep profiles (if used) carry **no** battery-life claim
