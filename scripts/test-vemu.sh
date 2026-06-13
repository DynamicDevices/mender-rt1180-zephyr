#!/usr/bin/env bash
# Build nRF5340 hello_world (if needed) and run headless vemu, or print browser load steps.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-nrf5340-vemu}"
ELF="${ROOT}/${BUILD_DIR}/zephyr/zephyr.elf"
VEMU_BOARD_ID="nordic,nrf5340-dk-cpuapp"
RUN_VEMU="${HOME}/run-vemu.mjs"
VEMU_PKG="${HOME}/node_modules/@swedishembedded/vemu"

rebuild=false
frames=200
browser=false

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

  --rebuild       Force a clean west build
  --frames N      Max vemu frames (default: 200)
  --browser       Print vemulator.com load steps only (no Node run)
  -h, --help      Show this help

Examples:
  $(basename "$0")
  $(basename "$0") --frames 50
  $(basename "$0") --rebuild --frames 500
  $(basename "$0") --browser
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rebuild)
      rebuild=true
      shift
      ;;
    --frames)
      if [[ $# -lt 2 ]] || ! [[ "${2}" =~ ^[0-9]+$ ]]; then
        echo "error: --frames requires a non-negative integer" >&2
        exit 1
      fi
      frames="$2"
      shift 2
      ;;
    --browser)
      browser=true
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${ROOT}/zephyr/zephyr-env.sh" ]]; then
  echo "error: missing Zephyr workspace: ${ROOT}/zephyr/zephyr-env.sh" >&2
  echo "Run 'west update' from mender-mcu-integration first." >&2
  exit 1
fi

if [[ "${rebuild}" == true ]] || [[ ! -f "${ELF}" ]]; then
  build_args=()
  [[ "${rebuild}" == true ]] && build_args=(--rebuild)
  "${ROOT}/scripts/run-vemu-demo.sh" "${build_args[@]}"
fi

if [[ ! -f "${ELF}" ]]; then
  echo "error: build failed, missing ${ELF}" >&2
  exit 1
fi

if [[ "${browser}" == true ]]; then
  echo "=== vemulator.com (browser) ==="
  echo "1. Open https://vemulator.com/docs/getting-started"
  echo "2. Board: ${VEMU_BOARD_ID}, image kind: elf"
  echo "3. Load: ${ELF}"
  echo '   Expect UART: "Hello World! nrf5340dk/nrf5340/cpuapp"'
  exit 0
fi

if [[ ! -f "${RUN_VEMU}" ]]; then
  echo "error: missing headless runner: ${RUN_VEMU}" >&2
  echo "Create run-vemu.mjs in your home directory (see scripts/run-vemu-demo.sh)." >&2
  exit 1
fi

if [[ ! -d "${VEMU_PKG}" ]]; then
  echo "error: missing vemu npm package: ${VEMU_PKG}" >&2
  echo "Install: cd ~ && npm install @swedishembedded/vemu" >&2
  exit 1
fi

exec node "${RUN_VEMU}" "${ELF}" "${frames}"
