#!/usr/bin/env bash
# Sysbuild Mender MCU OTA for NXP FRDM-IMXRT1186 (CM33).
# Requires Zephyr >= v4.4.0 (frdm_imxrt1186 board). Board fragment is auto-applied:
#   mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"
export PATH="${HOME}/bin:${PATH}"

if [[ -d "${ROOT}/.tools/bin" ]]; then
  export PATH="${ROOT}/.tools/bin:${PATH}"
fi

usage() {
  cat <<EOF
Usage: build-rt1186-frdm.sh [--incremental] [extra west cmake args...]

Sysbuild Mender MCU OTA for FRDM-IMXRT1186 CM33 (Zephyr frdm_imxrt1186/mimxrt1186/cm33).

Environment:
  BUILD_DIR              Default: build-frdm-rt1186
  RT1186_BOARD           Default: frdm_imxrt1186/mimxrt1186/cm33
  CONFIG_MENDER_ARTIFACT_NAME  Default: dev-1

Examples (from West workspace root):
  ./scripts/build-rt1186-frdm.sh
  ./scripts/build-rt1186-frdm.sh --incremental
  BUILD_DIR=build-frdm ./scripts/build-rt1186-frdm.sh

Requires Zephyr >= v4.4.0 (west update). Board fragment auto-applied:
  mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf

Flash: west flash -d build-frdm-rt1186
Deploy: ./scripts/create-rt1186-frdm-deployment.sh
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi


if [[ -f zephyr/zephyr-env.sh ]]; then
  # shellcheck source=/dev/null
  source zephyr/zephyr-env.sh
fi

BOARD="${RT1186_BOARD:-frdm_imxrt1186/mimxrt1186/cm33}"
BUILD_DIR="${BUILD_DIR:-build-frdm-rt1186}"
ARTIFACT_NAME="${CONFIG_MENDER_ARTIFACT_NAME:-dev-1}"

WEST_BUILD=(-p)
if [[ "${1:-}" == "--incremental" ]]; then
  WEST_BUILD=()
  shift
fi

EXTRA_CONF="mender-local.conf"
if [[ -f mender-mcu-integration/mender-local.conf ]]; then
  :
elif [[ -f mender-mcu-integration/mender-local.conf.example ]]; then
  echo "warning: mender-local.conf missing; build may fail at compile if token required" >&2
  EXTRA_CONF=""
fi

CMAKE_EXTRA=(-DCONFIG_MENDER_ARTIFACT_NAME="\"${ARTIFACT_NAME}\"")
if [[ -n "${EXTRA_CONF}" ]]; then
  CMAKE_EXTRA+=(-DEXTRA_CONF_FILE="${EXTRA_CONF}")
fi

# Sysbuild first configure can race (snippets.py mkdir); ensure dirs exist and retry once.
mkdir -p "${BUILD_DIR}/mender-mcu-integration/zephyr" "${BUILD_DIR}/mender-mcu-integration/Kconfig" "${BUILD_DIR}/Kconfig"

if ! west build "${WEST_BUILD[@]}" --sysbuild -d "${BUILD_DIR}" -b "${BOARD}" mender-mcu-integration -- \
  "${CMAKE_EXTRA[@]}" "$@"; then
  mkdir -p "${BUILD_DIR}/mender-mcu-integration/zephyr" "${BUILD_DIR}/mender-mcu-integration/Kconfig" "${BUILD_DIR}/Kconfig"
  west build --sysbuild -d "${BUILD_DIR}" -b "${BOARD}" mender-mcu-integration -- \
    "${CMAKE_EXTRA[@]}" "$@"
fi

SIGNED="${ROOT}/${BUILD_DIR}/mender-mcu-integration/zephyr/zephyr.signed.bin"
MENDER="${ROOT}/${BUILD_DIR}/mender-mcu-integration/zephyr/zephyr.mender"
echo "OK: board=${BOARD} build_dir=${BUILD_DIR}"
[[ -f "${SIGNED}" ]] && echo "  signed app: ${SIGNED}"
[[ -f "${MENDER}" ]] && echo "  artifact:   ${MENDER}"
