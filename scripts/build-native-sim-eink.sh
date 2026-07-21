#!/usr/bin/env bash
# Build native_sim Mender app with e-ink (SDL) + scheduler.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export BUILD_DIR="${BUILD_DIR:-build-native_sim-eink}"
export NATIVE_SIM_EXTRA_CONF="${NATIVE_SIM_EXTRA_CONF:-eink-native-sim.conf}"
# Overlay via west cmake args
EXTRA_OVERLAY="${ROOT}/mender-mcu-integration/boards/native_sim_eink.overlay"
EL133_MODULE="${ROOT}/mender-mcu-integration/modules/eink-el133"
export NATIVE_SIM_EXTRA_MODULES="${NATIVE_SIM_EXTRA_MODULES:-${EL133_MODULE}}"
# Append overlay to west build through env consumed below
export NATIVE_SIM_EXTRA_CMAKE_ARGS="${NATIVE_SIM_EXTRA_CMAKE_ARGS:--DDTC_OVERLAY_FILE=${EXTRA_OVERLAY}}"
exec "${ROOT}/scripts/build-native-sim.sh" "$@"
