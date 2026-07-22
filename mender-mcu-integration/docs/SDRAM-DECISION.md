# SDRAM decision log

Status: **DNP preferred — pending hardware proof gates**

| Gate | Status |
|------|--------|
| Streaming ES6F validation (no `validate_buf`) | Implemented |
| Streaming display (no production `eink_fb`) | Implemented |
| EVK full-FB profile isolated (`APP_EINK_FULL_FRAMEBUFFER`) | Implemented |
| OCRAM `zephyr,sram` profile overlay | Scaffold (`*_eink_sram.*`) |
| Dual-FlexSPI map | Overlay scaffold; pinmux TBD |
| Flash-staged OTA copy to slot1 | Scaffold (`eink_ota_stage_*`); install returns `-ENOTSUP` until FlexSPI2 live |
| Measured IW612/TLS peak in 1 MiB OCRAM | Pending silicon |
| Main-rail-off ≤50 µA | Pending custom board |

**Decision rule:** populate SDRAM only if OCRAM margins or flash-staged OTA
cannot close after buffer removal. EVK panel-debug FB is not evidence.
