#!/usr/bin/env bash
# MIMXRT1170-EVK + Rocktech RK055HDMIPI4MA0 LCD preview (lab stand-in for SDL).
# Mutually exclusive with EL133/LPSPI1 e-ink images.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

export BUILD_DIR="${BUILD_DIR:-build-rt1170-evk-lcd}"
export RT1170_EXTRA_CONF_FILE="${RT1170_EXTRA_CONF_FILE:-boards/mimxrt1170_eink_shell.conf;boards/mimxrt1170_evk_mimxrt1176_cm7_lcd.conf}"

LCD_OVERLAY="${ROOT}/mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_lcd.overlay"

# Shield enables zephyr,display = LCDIF + HX8394 panel.
# EXTRA_DTC_OVERLAY_FILE keeps boards/mimxrt1170_evk_*.overlay (lab MAC).
exec "${ROOT}/scripts/build-rt1170-evk.sh" "$@" \
  -DSHIELD=rk055hdmipi4ma0 \
  -DEXTRA_DTC_OVERLAY_FILE="${LCD_OVERLAY}"
