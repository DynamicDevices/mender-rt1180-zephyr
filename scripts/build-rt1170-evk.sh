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
  RT1170_EXTRA_CONF_FILE Additional app conf (semicolon-separated)
  CONFIG_MENDER_ARTIFACT_NAME  Default: dev-1
  MCUBOOT_SIGNATURE_KEY_FILE   Optional absolute/relative PEM (overrides demo key)
  SKIP_SBOM=1            Skip west spdx archive after a successful build
  GENERATE_SBOM=0        Same as SKIP_SBOM=1

Examples (from West workspace root):
  ./scripts/build-rt1170-evk.sh
  ./scripts/build-rt1170-evk.sh --incremental
  MCUBOOT_SIGNATURE_KEY_FILE=/secure/rt1170-mcuboot.pem ./scripts/build-rt1170-evk.sh

Board fragment: mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7.conf
Flash: west flash -d build-rt1170-evk
Deploy: ./scripts/create-rt1170-deployment.sh
SBOM: scripts/generate-sbom.sh --rt1170-only (auto after build unless SKIP_SBOM=1)

Notes:
  - Targets the standard MIMXRT1170-EVK (MIMXRT1176), not RT117H/F EdgeReady SKUs.
  - Face/gesture runtime libraries need NXP EdgeReady silicon + SDK outside this overlay.
  - west spdx --init is run before cmake so CRA WS3 SPDX archives can be produced.
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

if [[ -n "${RT1170_EXTRA_CONF_FILE:-}" ]]; then
  if [[ -n "${EXTRA_CONF}" ]]; then
    EXTRA_CONF="${EXTRA_CONF};${RT1170_EXTRA_CONF_FILE}"
  else
    EXTRA_CONF="${RT1170_EXTRA_CONF_FILE}"
  fi
fi

CMAKE_EXTRA=(-DCONFIG_MENDER_ARTIFACT_NAME="\"${ARTIFACT_NAME}\"")
if [[ -n "${EXTRA_CONF}" ]]; then
  CMAKE_EXTRA+=(-DEXTRA_CONF_FILE="${EXTRA_CONF}")
fi

# Optional production / lab signing key (see docs/MCUBOOT-KEY-CEREMONY.md).
if [[ -n "${MCUBOOT_SIGNATURE_KEY_FILE:-}" ]]; then
  if [[ ! -f "${MCUBOOT_SIGNATURE_KEY_FILE}" ]]; then
    echo "error: MCUBOOT_SIGNATURE_KEY_FILE not found: ${MCUBOOT_SIGNATURE_KEY_FILE}" >&2
    exit 1
  fi
  # App image signing (sysbuild propagates for zephyr.signed.bin).
  CMAKE_EXTRA+=(-DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE="\"${MCUBOOT_SIGNATURE_KEY_FILE}\"")
  # MCUboot verifies with the matching public material from the same PEM.
  CMAKE_EXTRA+=(-Dmcuboot_CONFIG_BOOT_SIGNATURE_KEY_FILE="\"${MCUBOOT_SIGNATURE_KEY_FILE}\"")
  echo "Using MCUBOOT_SIGNATURE_KEY_FILE=${MCUBOOT_SIGNATURE_KEY_FILE}"
fi

# Reject demo key when releasing.
if [[ "${RELEASE_BUILD:-}" == "1" ]]; then
  key_eff="${MCUBOOT_SIGNATURE_KEY_FILE:-bootloader/mcuboot/root-rsa-2048.pem}"
  if [[ "${key_eff}" == *root-rsa-2048.pem ]]; then
    echo "error: RELEASE_BUILD=1 forbids demo root-rsa-2048.pem — set MCUBOOT_SIGNATURE_KEY_FILE" >&2
    exit 1
  fi
fi

# Sysbuild first configure can race (snippets.py mkdir); ensure dirs exist and retry once.
mkdir -p "${BUILD_DIR}/mender-mcu-integration/zephyr" "${BUILD_DIR}/mender-mcu-integration/Kconfig" "${BUILD_DIR}/Kconfig"
mkdir -p "${BUILD_DIR}/mcuboot"

# CRA WS3: CMake file-based API query must exist *before* configure.
spdx_prepare() {
  local d
  for d in "${BUILD_DIR}" "${BUILD_DIR}/mender-mcu-integration" "${BUILD_DIR}/mcuboot"; do
    mkdir -p "${d}"
    west spdx --init -d "${d}" >/dev/null
  done
  echo "SPDX CMake API initialized under ${BUILD_DIR}/"
}
spdx_prepare

if ! west build "${WEST_BUILD[@]}" --sysbuild -d "${BUILD_DIR}" -b "${BOARD}" mender-mcu-integration -- \
  "${CMAKE_EXTRA[@]}" "$@"; then
  mkdir -p "${BUILD_DIR}/mender-mcu-integration/zephyr" "${BUILD_DIR}/mender-mcu-integration/Kconfig" "${BUILD_DIR}/Kconfig" "${BUILD_DIR}/mcuboot"
  spdx_prepare
  west build --sysbuild -d "${BUILD_DIR}" -b "${BOARD}" mender-mcu-integration -- \
    "${CMAKE_EXTRA[@]}" "$@"
fi

SIGNED="${ROOT}/${BUILD_DIR}/mender-mcu-integration/zephyr/zephyr.signed.bin"
MENDER="${ROOT}/${BUILD_DIR}/mender-mcu-integration/zephyr/zephyr.mender"
echo "OK: board=${BOARD} build_dir=${BUILD_DIR}"
[[ -f "${SIGNED}" ]] && echo "  signed app: ${SIGNED}"
[[ -f "${MENDER}" ]] && echo "  artifact:   ${MENDER}"

if [[ "${SKIP_SBOM:-}" == "1" || "${GENERATE_SBOM:-}" == "0" ]]; then
  echo "SBOM skipped (SKIP_SBOM/GENERATE_SBOM)"
else
  if ! python3 -c 'import reuse' 2>/dev/null; then
    echo "warning: Python package reuse missing — SBOM skipped (pip install --user reuse)" >&2
  else
    RT1170_BUILD_DIR="${BUILD_DIR}" CONFIG_MENDER_ARTIFACT_NAME="${ARTIFACT_NAME}" \
      "${ROOT}/scripts/generate-sbom.sh" --rt1170-only || \
      echo "warning: SBOM generation failed — see scripts/generate-sbom.sh" >&2
  fi
fi
