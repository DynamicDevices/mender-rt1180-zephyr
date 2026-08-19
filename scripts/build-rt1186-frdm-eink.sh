#!/usr/bin/env bash
# FRDM-IMXRT1186 SPI EL133UF1 lab profile (1-bit LPSPI2 + GPIO CS).
# Pin contract: docs/FRDM-IMXRT1186-EL133-PIN-CONTRACT.md
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

export BUILD_DIR="${BUILD_DIR:-build-frdm-rt1186-eink}"

EL133_MODULE="${ROOT}/mender-mcu-integration/modules/eink-el133"
EL133_OVERLAY="${ROOT}/mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_eink_el133.overlay"
EL133_CONF="boards/frdm_imxrt1186_mimxrt1186_cm33_eink_el133.conf"
SHELL_CONF="boards/mimxrt1170_eink_shell.conf"

EXTRA_CONF="mender-local.conf"
if [[ ! -f mender-mcu-integration/mender-local.conf ]]; then
  echo "warning: mender-local.conf missing; eink confs only" >&2
  EXTRA_CONF=""
fi
if [[ -n "${EXTRA_CONF}" ]]; then
  EXTRA_CONF="${EXTRA_CONF};${SHELL_CONF};${EL133_CONF}"
else
  EXTRA_CONF="${SHELL_CONF};${EL133_CONF}"
fi

MCUBOOT_DTCM_OVERLAY="${ROOT}/mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_mcuboot_dtcm.overlay"

exec "${ROOT}/scripts/build-rt1186-frdm.sh" "$@" \
  -DZEPHYR_EXTRA_MODULES="${EL133_MODULE}" \
  -DEXTRA_CONF_FILE="${EXTRA_CONF}" \
  -DEXTRA_DTC_OVERLAY_FILE="${EL133_OVERLAY}" \
  -Dmcuboot_EXTRA_DTC_OVERLAY_FILE="${MCUBOOT_DTCM_OVERLAY}"
