#!/usr/bin/env bash
# Thin wrapper: drift-gate against DynamicDevices/mender-mcu DD_PIN.
# Logic SoT: mender-mcu scripts/check-consumer-pin.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WEST=${1:-"$ROOT/mender-mcu-integration/west.yml"}
[[ "$WEST" != /* ]] && WEST="$ROOT/$WEST"
URL=${MENDER_MCU_CHECK_SCRIPT_URL:-https://raw.githubusercontent.com/DynamicDevices/mender-mcu/feature/zephyr-4.4-mbedtls4/scripts/check-consumer-pin.sh}
curl -fsSL "$URL" | bash -s -- "$WEST"
