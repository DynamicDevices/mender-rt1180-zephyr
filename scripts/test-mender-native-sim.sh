#!/usr/bin/env bash
# Build mender-mcu-integration for native_sim and run Hosted Mender check-in smoke test.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-native_sim}"
EXE="${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
MENDER_CONF="${ROOT}/mender-mcu-integration/mender-local.conf"
NET_SCRIPT="${ROOT}/scripts/run-native-sim-network.sh"

rebuild=false
incremental=false
run_only=false
setup_hint=false
timeout_sec=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

  --rebuild       Pristine west build (build-native-sim.sh)
  --incremental   Incremental rebuild
  --run-only      Skip build; run zephyr.exe only
  --setup-hint    Print two-terminal network setup and exit
  --timeout SEC   Stop zephyr.exe after SEC seconds (0 = run until Ctrl-C)
  -h, --help      Show this help

Hosted Mender check-in needs TAP + DHCP + NAT (root):

  Terminal 1: ./scripts/run-native-sim-network.sh start  # after one-time setup-native-sim-tap.sh install
  Terminal 2: ./scripts/test-mender-native-sim.sh --run-only

Expect: DHCP address on zeth0, then "Mender client activated and running!"
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rebuild)
      rebuild=true
      shift
      ;;
    --incremental)
      incremental=true
      shift
      ;;
    --run-only)
      run_only=true
      shift
      ;;
    --setup-hint)
      setup_hint=true
      shift
      ;;
    --timeout)
      if [[ $# -lt 2 ]] || ! [[ "${2}" =~ ^[0-9]+$ ]]; then
        echo "error: --timeout requires a non-negative integer" >&2
        exit 1
      fi
      timeout_sec="$2"
      shift 2
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

if [[ "${setup_hint}" == true ]]; then
  usage
  "${NET_SCRIPT}" status || true
  exit 0
fi

if [[ ! -f "${ROOT}/zephyr/zephyr-env.sh" ]]; then
  echo "error: missing Zephyr workspace: ${ROOT}/zephyr/zephyr-env.sh" >&2
  exit 1
fi

if [[ ! -f "${MENDER_CONF}" ]]; then
  echo "error: missing ${MENDER_CONF}" >&2
  echo "Create gitignored mender-local.conf with tenant token and CONFIG_MENDER_SERVER_HOST_US=y" >&2
  exit 1
fi

if [[ "${run_only}" == false ]]; then
  if [[ "${rebuild}" == true ]]; then
    "${ROOT}/scripts/build-native-sim.sh"
  elif [[ "${incremental}" == true ]]; then
    "${ROOT}/scripts/build-native-sim.sh" --incremental
  elif [[ ! -f "${EXE}" ]]; then
    "${ROOT}/scripts/build-native-sim.sh"
  else
    echo "Using existing ${EXE} (pass --rebuild or --incremental to rebuild)"
  fi
fi

if [[ ! -f "${EXE}" ]]; then
  echo "error: missing ${EXE}" >&2
  exit 1
fi

if ! ip link show zeth &>/dev/null || ! ip link show zeth | grep -q 'state UP'; then
  echo "warning: zeth is not UP — Mender will hang at 'Waiting for network up...'" >&2
  echo "Start network: ${NET_SCRIPT} start  (one-time: sudo ./scripts/setup-native-sim-tap.sh install)" >&2
  "${NET_SCRIPT}" status >&2 || true
fi

if ! pgrep -a dnsmasq >/dev/null 2>&1; then
  echo "warning: dnsmasq not running — DHCP will not work without nat.conf setup" >&2
fi

echo "=== Running native_sim Mender client ==="
echo "Binary: ${EXE}"
echo ""

if [[ "${timeout_sec}" -gt 0 ]]; then
  timeout "${timeout_sec}" "${EXE}" || true
else
  exec "${EXE}"
fi
