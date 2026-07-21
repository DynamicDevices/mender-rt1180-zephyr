#!/usr/bin/env bash
# Run the native_sim Mender + Improv (BLE) emulator against the HOST Bluetooth
# adapter, so a real BLE central (phone Improv app, or Chrome Web Bluetooth at
# https://www.improv-wifi.com/ble/) can provision it over the air.
#
# native_sim's HCI User Channel driver binds the chosen host adapter directly,
# which:
#   * takes the adapter away from BlueZ while the sim runs (your desktop
#     Bluetooth -- mice/headphones -- drops until the sim exits), and
#   * requires CAP_NET_ADMIN to open the user-channel socket (kernel rule).
#
# The Wi-Fi association is simulated (CONFIG_APP_WIFI_SIM); actual data flows
# over the native TAP interface (zeth), so start the TAP network first with
#   ./scripts/run-native-sim-network.sh start
#
# Usage:
#   ./scripts/run-native-sim-improv-ble.sh [--dev hciN] [-- <extra zephyr.exe args>]
#   ./scripts/run-native-sim-improv-ble.sh setcap   # grant CAP_NET_ADMIN to the exe (sudo, once per build)
#
# Env:
#   BUILD_DIR  (default build-native_sim-improv-ble)
#   BT_DEV     (default hci0)  host adapter to hand to the sim
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-native_sim-improv-ble}"
BT_DEV="${BT_DEV:-hci0}"
EXE="${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"

usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; }

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
  setcap)
    [[ -f "${EXE}" ]] || { echo "Build first: ${EXE} missing." >&2; exit 1; }
    echo "Granting CAP_NET_ADMIN to ${EXE} (sudo)..."
    sudo setcap cap_net_admin+ep "${EXE}"
    getcap "${EXE}"
    exit 0 ;;
esac

if [[ "${1:-}" == "--dev" ]]; then
  BT_DEV="${2:?--dev needs an adapter, e.g. hci0}"
  shift 2
fi
if [[ "${1:-}" == "--" ]]; then
  shift
fi

[[ -f "${EXE}" ]] || { echo "Build first: ${EXE} missing (./scripts/build-native-sim-improv-ble.sh)." >&2; exit 1; }

if [[ ! -e "/sys/class/bluetooth/${BT_DEV}" ]]; then
  echo "Host adapter ${BT_DEV} not found. Available:" >&2
  ls /sys/class/bluetooth/ 2>/dev/null | sed 's/^/  /' >&2 || true
  exit 1
fi

# Free the adapter from BlueZ so the kernel hands it to the user channel.
# bluetoothctl uses D-Bus/polkit (no sudo needed for the active desktop user).
POWERED_OFF=0
if command -v bluetoothctl >/dev/null 2>&1; then
  echo "Powering down ${BT_DEV} (releasing it from BlueZ)..."
  if bluetoothctl power off >/dev/null 2>&1; then
    POWERED_OFF=1
  else
    echo "  warning: could not power off via bluetoothctl; the sim may fail to grab ${BT_DEV}." >&2
  fi
fi

restore_bt() {
  if [[ "${POWERED_OFF}" == "1" ]] && command -v bluetoothctl >/dev/null 2>&1; then
    echo "Restoring ${BT_DEV} to BlueZ..."
    bluetoothctl power on >/dev/null 2>&1 || true
  fi
}
trap restore_bt EXIT

# Decide how to get CAP_NET_ADMIN for the user-channel socket.
RUN=()
if [[ "$(id -u)" != "0" ]]; then
  if getcap "${EXE}" 2>/dev/null | grep -q cap_net_admin; then
    :  # binary already has the capability
  else
    echo "Note: ${BUILD_DIR}/zephyr/zephyr.exe lacks CAP_NET_ADMIN; using sudo."
    echo "      (Run '$0 setcap' once per build to avoid sudo here.)"
    RUN=(sudo)
  fi
fi

echo "Starting Improv BLE emulator on ${BT_DEV} (advertising as 'eink-51F0')."
echo "Provision from a phone Improv app or Chrome Web Bluetooth: https://www.improv-wifi.com/ble/"
exec "${RUN[@]}" "${EXE}" --bt-dev="${BT_DEV}" "$@"
