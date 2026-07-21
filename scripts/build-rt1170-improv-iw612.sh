#!/usr/bin/env bash
# Build Mender + Improv BLE provisioning for MIMXRT1170-EVKB + NXP 2EL/IW612.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export BUILD_DIR="${BUILD_DIR:-build-rt1170-improv-iw612}"
export RT1170_EXTRA_CONF_FILE="${RT1170_EXTRA_CONF_FILE:-boards/mimxrt1170_evk_mimxrt1176_cm7_improv_iw612.conf}"

if [[ ! -d "${ROOT}/modules/improv-zephyr" ]]; then
  echo "Missing modules/improv-zephyr; run 'west update' first." >&2
  exit 1
fi

PATCH="${ROOT}/mender-mcu-integration/patches/improv-zephyr-active-esl-claim.patch"
if [[ -f "${PATCH}" ]] && ! grep -q 'mint_claim_token' "${ROOT}/modules/improv-zephyr/src/improv_handler.c" 2>/dev/null; then
  echo "Applying ${PATCH##*/} to modules/improv-zephyr..."
  git -C "${ROOT}/modules/improv-zephyr" apply "${PATCH}"
fi

exec "${ROOT}/scripts/build-rt1170-evk.sh" \
  "$@" \
  -DZEPHYR_EXTRA_MODULES="${ROOT}/modules/improv-zephyr" \
  -Dmender-mcu-integration_DTC_OVERLAY_FILE="${ROOT}/mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_improv_iw612.overlay"
