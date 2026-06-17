#!/usr/bin/env bash
# Sysbuild Mender MCU OTA for NXP MIMXRT1180-EVK (CM33).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ -d "${ROOT}/.tools/bin" ]]; then
  export PATH="${ROOT}/.tools/bin:${PATH}"
fi

usage() {
  cat <<EOF
Usage: build-rt1180-evk.sh [--incremental] [extra west cmake args...]

Sysbuild Mender MCU OTA for MIMXRT1180-EVK CM33 (mimxrt1180_evk/mimxrt1189/cm33).

Environment:
  BUILD_DIR              Default: build
  RT1180_BOARD           Default: mimxrt1180_evk/mimxrt1189/cm33
  CONFIG_MENDER_ARTIFACT_NAME  Default: dev-1

Examples (from West workspace root):
  ./scripts/build-rt1180-evk.sh
  ./scripts/build-rt1180-evk.sh --incremental

Board fragment: mender-mcu-integration/boards/mimxrt1180_evk_mimxrt1189_cm33.conf
Flash: west flash -d build
Deploy: ./scripts/create-rt1180-deployment.sh
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

BOARD="${RT1180_BOARD:-mimxrt1180_evk/mimxrt1189/cm33}"
BUILD_DIR="${BUILD_DIR:-build}"
ARTIFACT_NAME="${CONFIG_MENDER_ARTIFACT_NAME:-dev-1}"

WEST_BUILD=(-p)
if [[ "${1:-}" == "--incremental" ]]; then
  WEST_BUILD=()
  shift
fi

CMAKE_EXTRA=(-DCONFIG_MENDER_ARTIFACT_NAME="\"${ARTIFACT_NAME}\"")
if [[ -f mender-mcu-integration/mender-local.conf ]]; then
  CMAKE_EXTRA+=(-DEXTRA_CONF_FILE=mender-mcu-integration/mender-local.conf)
fi

west build "${WEST_BUILD[@]}" --sysbuild -d "${BUILD_DIR}" -b "${BOARD}" mender-mcu-integration -- \
  "${CMAKE_EXTRA[@]}" "$@"

echo "OK: board=${BOARD} build_dir=${BUILD_DIR}"
