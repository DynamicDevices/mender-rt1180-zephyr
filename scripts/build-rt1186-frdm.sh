#!/usr/bin/env bash
# Sysbuild Mender MCU OTA for NXP FRDM-IMXRT1186 (CM33).
# Requires Zephyr >= v4.4.0 (frdm_imxrt1186 board). Board fragment is auto-applied:
#   mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf
#
# Opt-in OCRAM enroll (HyperRAM UsageFault after MCUboot):
#   FRDM_ENROLL_OCRAM=1 ./scripts/build-rt1186-frdm.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"
export PATH="${HOME}/bin:${PATH}"

if [[ -d "${ROOT}/.tools/bin" ]]; then
  export PATH="${ROOT}/.tools/bin:${PATH}"
fi

usage() {
  cat <<EOF_USAGE
Usage: build-rt1186-frdm.sh [--incremental] [extra west cmake args...]

Sysbuild Mender MCU OTA for FRDM-IMXRT1186 CM33 (Zephyr frdm_imxrt1186/mimxrt1186/cm33).

Environment:
  BUILD_DIR              Default: build-frdm-rt1186
                         (OCRAM enroll default: build-frdm-rt1186-ocram)
  RT1186_BOARD           Default: frdm_imxrt1186/mimxrt1186/cm33
  CONFIG_MENDER_ARTIFACT_NAME  Default: dev-1
  FRDM_ENROLL_OCRAM=1    Opt-in: OCRAM as zephyr,sram + tighter net/TLS bufs +
                         MCUboot DTCM conf (see PROJECT-NOTES FRDM OCRAM enroll)

Examples (from West workspace root):
  ./scripts/build-rt1186-frdm.sh
  ./scripts/build-rt1186-frdm.sh --incremental
  FRDM_ENROLL_OCRAM=1 ./scripts/build-rt1186-frdm.sh
  BUILD_DIR=build-frdm ./scripts/build-rt1186-frdm.sh

Requires Zephyr >= v4.4.0 (west update). Board fragment auto-applied:
  mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33.conf

Flash: west flash -d build-frdm-rt1186
       (OCRAM: west flash -d build-frdm-rt1186-ocram)
Deploy: ./scripts/create-rt1186-frdm-deployment.sh
EOF_USAGE
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
ENROLL_OCRAM=0
case "${FRDM_ENROLL_OCRAM:-}" in
  1|true|TRUE|yes|YES|on|ON) ENROLL_OCRAM=1 ;;
esac

if [[ "${ENROLL_OCRAM}" -eq 1 ]]; then
  BUILD_DIR="${BUILD_DIR:-build-frdm-rt1186-ocram}"
else
  BUILD_DIR="${BUILD_DIR:-build-frdm-rt1186}"
fi
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

OVERLAY_ARGS=()
if [[ "${ENROLL_OCRAM}" -eq 1 ]]; then
  if [[ ! -f mender-mcu-integration/mender-frdm-ocram.conf ]]; then
    echo "error: missing mender-mcu-integration/mender-frdm-ocram.conf" >&2
    exit 1
  fi
  if [[ ! -f mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_ocram.overlay ]]; then
    echo "error: missing boards/frdm_imxrt1186_mimxrt1186_cm33_ocram.overlay" >&2
    exit 1
  fi
  if [[ -n "${EXTRA_CONF}" ]]; then
    EXTRA_CONF="${EXTRA_CONF};mender-frdm-ocram.conf"
  else
    EXTRA_CONF="mender-frdm-ocram.conf"
  fi
  OVERLAY_ARGS+=(
    -DEXTRA_DTC_OVERLAY_FILE=boards/frdm_imxrt1186_mimxrt1186_cm33_ocram.overlay
  )
  if [[ -f mender-mcu-integration/sysbuild/mcuboot-frdm-dtcm.conf ]]; then
    OVERLAY_ARGS+=(
      -Dmcuboot_EXTRA_CONF_FILE="${ROOT}/mender-mcu-integration/sysbuild/mcuboot-frdm-dtcm.conf"
    )
  fi
  echo "FRDM_ENROLL_OCRAM=1: OCRAM sram + mender-frdm-ocram.conf (build_dir=${BUILD_DIR})"
fi

CMAKE_EXTRA=(-DCONFIG_MENDER_ARTIFACT_NAME="\"${ARTIFACT_NAME}\"")
if [[ -n "${EXTRA_CONF}" ]]; then
  CMAKE_EXTRA+=(-DEXTRA_CONF_FILE="${EXTRA_CONF}")
fi
CMAKE_EXTRA+=("${OVERLAY_ARGS[@]}")

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
