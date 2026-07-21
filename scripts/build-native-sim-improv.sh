#!/usr/bin/env bash
# Build the native_sim Mender + Improv (serial) emulator target.
#
# Runs the improv-zephyr provisioning handshake over the native_sim console PTY
# and brings the native TAP Ethernet interface up (DHCPv4) on provisioning, so
# the Mender client connects end-to-end without a real radio. Host build only.
#
# Prereqs: gcc-11 + gcc-11-multilib (see build-native-sim.sh), 'west update' for
# the improv-zephyr module, and (to run) net-tools TAP/NAT via
# scripts/run-native-sim-network.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -d "${ROOT}/modules/improv-zephyr" ]]; then
  echo "Missing modules/improv-zephyr; run 'west update' first." >&2
  exit 1
fi

export BUILD_DIR="${BUILD_DIR:-build-native_sim-improv}"
export NATIVE_SIM_EXTRA_CONF="${NATIVE_SIM_EXTRA_CONF:-improv-native-sim.conf}"
export NATIVE_SIM_EXTRA_MODULES="${NATIVE_SIM_EXTRA_MODULES:-${ROOT}/modules/improv-zephyr}"

exec "${ROOT}/scripts/build-native-sim.sh" "$@"
