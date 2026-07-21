#!/usr/bin/env bash
# Build native_sim/native/64 with SDL e-ink overlay (amd64 SDL2).
# The default 32-bit native_sim build cannot link host SDL2; use this for visual checks.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${BUILD_DIR:-build-native_sim-eink-sdl}"
EXTRA_OVERLAY="${ROOT}/mender-mcu-integration/boards/native_sim_eink_sdl.overlay"
EL133_MODULE="${ROOT}/mender-mcu-integration/modules/eink-el133"

WEST_ARGS=(-d "${BUILD_DIR}" --board native_sim/native/64 mender-mcu-integration --
  -DEXTRA_CONF_FILE="boards/native_sim.conf;mender-local.conf;eink-native-sim-sdl.conf"
  -DZEPHYR_EXTRA_MODULES="${EL133_MODULE}"
  -DDTC_OVERLAY_FILE="${EXTRA_OVERLAY}")

if [[ "${1:-}" == "--incremental" ]]; then
  shift
  west build "${WEST_ARGS[@]}" "$@"
else
  west build -p "${WEST_ARGS[@]}" "$@"
fi
echo "OK: ${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
echo "Run (needs DISPLAY / X11): ${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
echo "  then: eink show /tmp/eink-zephyr/images/lr.es6f jobLR"
