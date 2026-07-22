#!/usr/bin/env bash
# MIMXRT1170-EVK SPI EL133UF1 lab profile (no Rocktech MIPI shield).
# GPIO map in boards/mimxrt1170_evk_mimxrt1176_cm7_eink_el133.overlay is
# placeholder until schematic/loom is confirmed — enable the DT node to flash.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

export BUILD_DIR="${BUILD_DIR:-build-rt1170-evk-eink}"
export RT1170_EXTRA_CONF_FILE="${RT1170_EXTRA_CONF_FILE:-boards/mimxrt1170_evk_mimxrt1176_cm7_eink_el133.conf}"

EL133_MODULE="${ROOT}/mender-mcu-integration/modules/eink-el133"
EL133_OVERLAY="${ROOT}/mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_eink_el133.overlay"
# Same LittleFS carve as LCD lab (shared flash layout helper).
LCD_FS_OVERLAY="${ROOT}/mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_lcd.overlay"

exec "${ROOT}/scripts/build-rt1170-evk.sh" "$@" \
  -DZEPHYR_EXTRA_MODULES="${EL133_MODULE}" \
  -DEXTRA_DTC_OVERLAY_FILE="${LCD_FS_OVERLAY};${EL133_OVERLAY}"
