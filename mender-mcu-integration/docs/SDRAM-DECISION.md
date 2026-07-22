# SDRAM decision log

Status: **DNP preferred — pending hardware proof gates**

| Gate | Status |
|------|--------|
| Streaming ES6F validation (no `validate_buf`) | Implemented |
| Streaming display (no production `eink_fb`) | Implemented |
| EVK full-FB profile isolated (`APP_EINK_FULL_FRAMEBUFFER`) | Implemented |
| OCRAM `zephyr,sram` profile overlay | Scaffold (`*_eink_sram.*`) |
| Dual-FlexSPI map | Overlay scaffold (64 MiB FlexSPI2 layout); pinmux TBD |
| Active ESL product conf | Scaffold `mimxrt1170_aesl_eink.conf` (DNP SDRAM, no LCD preview) |
| EVK LCD lab preview | `APP_EINK_DISPLAY_LCD_PREVIEW` + `build-rt1170-evk-lcd.sh` (not product evidence) |
| EVK lab LittleFS carve | Overlay `*_lcd.overlay` (rev B `w25q512nw`); shared by LCD + EL133 lab builds |
| Flash-staged OTA copy to slot1 | Scaffold (`eink_ota_stage_*`); install returns `-ENOTSUP` until FlexSPI2 live |
| Measured IW612/TLS peak in 1 MiB OCRAM | Pending silicon |
| Main-rail-off ≤50 µA | Pending custom board |

**Decision rule:** populate SDRAM only if OCRAM margins or flash-staged OTA
cannot close after buffer removal. EVK panel-debug FB is not evidence.
