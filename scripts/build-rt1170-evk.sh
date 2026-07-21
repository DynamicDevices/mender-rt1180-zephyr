#!/usr/bin/env bash
# Sysbuild Mender MCU OTA for NXP MIMXRT1170-EVK (Cortex-M7).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ -d "${ROOT}/.tools/bin" ]]; then
  export PATH="${ROOT}/.tools/bin:${PATH}"
fi

usage() {
  cat <<EOF
Usage: build-rt1170-evk.sh [--incremental] [extra west cmake args...]

Sysbuild Mender MCU OTA for MIMXRT1170-EVK CM7 (mimxrt1170_evk/mimxrt1176/cm7).

Environment:
  BUILD_DIR              Default: build-rt1170-evk
  RT1170_BOARD           Default: mimxrt1170_evk/mimxrt1176/cm7
  CONFIG_MENDER_ARTIFACT_NAME  Default: dev-1

Examples (from West workspace root):
  ./scripts/build-rt1170-evk.sh
  ./scripts/build-rt1170-evk.sh --incremental

Board fragment: mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7.conf
Flash: west flash -d build-rt1170-evk
Deploy: ./scripts/create-rt1170-deployment.sh

Notes:
  - Targets the standard MIMXRT1170-EVK (MIMXRT1176), not RT117H/F EdgeReady SKUs.
  - Face/gesture runtime libraries need NXP EdgeReady silicon + SDK outside this overlay.
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

BOARD="${RT1170_BOARD:-mimxrt1170_evk/mimxrt1176/cm7}"
BUILD_DIR="${BUILD_DIR:-build-rt1170-evk}"
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
