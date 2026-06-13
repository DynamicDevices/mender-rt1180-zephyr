#!/usr/bin/env bash
# Build mender-mcu-integration for nRF5340 DK app core (vemu freeware board).
# Uses gitignored mender-local.conf; boards/nrf5340dk_nrf5340_cpuapp.conf (noop OTA + test RNG).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

BOARD="nrf5340dk/nrf5340/cpuapp"
BUILD_DIR="${BUILD_DIR:-build-nrf5340-mender-vemu}"
APP="${ROOT}/mender-mcu-integration"
MENDER_CONF="${APP}/mender-local.conf"
ELF="${ROOT}/${BUILD_DIR}/zephyr/zephyr.elf"

if [[ ! -f "${ROOT}/zephyr/zephyr-env.sh" ]]; then
  echo "error: missing Zephyr workspace: ${ROOT}/zephyr/zephyr-env.sh" >&2
  echo "Run 'west update' from the repo root first." >&2
  exit 1
fi
# shellcheck source=/dev/null
source "${ROOT}/zephyr/zephyr-env.sh"

if [[ ! -f "${MENDER_CONF}" ]]; then
  echo "error: missing ${MENDER_CONF}" >&2
  echo "Create gitignored mender-local.conf (tenant token + CONFIG_MENDER_SERVER_HOST_US=y)." >&2
  exit 1
fi

rebuild=false
if [[ "${1:-}" == "--rebuild" ]]; then
  rebuild=true
  shift
fi

WEST_BUILD=()
[[ "${rebuild}" == true ]] && WEST_BUILD=(-p)

echo "Building mender-mcu-integration for ${BOARD} ..."
west build "${WEST_BUILD[@]}" -d "${BUILD_DIR}" --board "${BOARD}" "${APP}" -- \
  -DEXTRA_CONF_FILE=mender-local.conf "$@"

if [[ ! -f "${ELF}" ]]; then
  echo "error: build failed, missing ${ELF}" >&2
  exit 1
fi
echo "OK: ${ELF}"
