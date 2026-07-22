#!/usr/bin/env bash
# Build + run the EL133UF1 native_sim mock-SPI ztest.
# Asserts the recreated driver emits the reference init and refresh opcodes.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export ZEPHYR_BASE="${ZEPHYR_BASE:-$ROOT/zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-/home/ajlennon/zephyr-sdk-1.0.1}"

# Do not inherit BUILD_DIR from eink / duty wrappers (wrong app dir).
BUILD_DIR="${EL133_BUILD_DIR:-build-el133-ztest}"
EXTRA=(-- -DZEPHYR_EXTRA_MODULES="$ROOT/mender-mcu-integration/modules/eink-el133")

if [[ "${1:-}" == "--pristine" ]]; then
  west build -b native_sim -d "$BUILD_DIR" \
    mender-mcu-integration/modules/eink-el133/tests/el133_seq \
    --pristine "${EXTRA[@]}"
else
  west build -b native_sim -d "$BUILD_DIR" \
    mender-mcu-integration/modules/eink-el133/tests/el133_seq \
    "${EXTRA[@]}"
fi

echo "OK: $ROOT/$BUILD_DIR/zephyr/zephyr.exe"

LOG=$(mktemp)
./"$BUILD_DIR/zephyr/zephyr.exe" 2>&1 | tee "$LOG"
grep -q "PROJECT EXECUTION SUCCESSFUL" "$LOG"
grep -q "PASS - test_sequences" "$LOG"
echo "OK: EL133 mock-SPI sequence ztest"
