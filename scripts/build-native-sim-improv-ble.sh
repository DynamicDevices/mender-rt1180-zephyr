#!/usr/bin/env bash
# Build the native_sim Mender + Improv (BLE) emulator target.
#
# Exposes the improv-zephyr BLE (GATT) provisioning service over the HOST
# Bluetooth adapter (native_sim HCI User Channel), and brings the native TAP
# Ethernet interface up (DHCPv4) on provisioning so the Mender client connects
# end-to-end without a real radio. Host build only.
#
# Prereqs: gcc-11 + gcc-11-multilib (see build-native-sim.sh), 'west update' for
# the improv-zephyr module. To run: a spare/powered-down host BLE adapter and
# CAP_NET_ADMIN, plus net-tools TAP/NAT -- see scripts/run-native-sim-improv-ble.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -d "${ROOT}/modules/improv-zephyr" ]]; then
  echo "Missing modules/improv-zephyr; run 'west update' first." >&2
  exit 1
fi

# Active ESL claim-token / write-without-response patch on pinned improv-zephyr.
# Idempotent: skip if already applied (reverse-check or marker in source).
PATCH="${ROOT}/mender-mcu-integration/patches/improv-zephyr-active-esl-claim.patch"
if [[ -f "${PATCH}" ]]; then
  if ! grep -q 'mint_claim_token' "${ROOT}/modules/improv-zephyr/src/improv_handler.c" 2>/dev/null; then
    echo "Applying ${PATCH##*/} to modules/improv-zephyr..."
    git -C "${ROOT}/modules/improv-zephyr" apply "${PATCH}"
  fi
fi

export BUILD_DIR="${BUILD_DIR:-build-native_sim-improv-ble}"
export NATIVE_SIM_EXTRA_CONF="${NATIVE_SIM_EXTRA_CONF:-improv-native-sim-ble.conf}"
export NATIVE_SIM_EXTRA_MODULES="${NATIVE_SIM_EXTRA_MODULES:-${ROOT}/modules/improv-zephyr}"

exec "${ROOT}/scripts/build-native-sim.sh" "$@"
