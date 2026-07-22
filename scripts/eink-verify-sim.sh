#!/usr/bin/env bash
# Simulator verification gate for Zephyr e-ink.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Avoid inheriting duty-cycle / LZ4 A/B BUILD_DIR overrides from the shell.
unset BUILD_DIR NATIVE_SIM_EXTRA_CONF NATIVE_SIM_EXTRA_MODULES NATIVE_SIM_EXTRA_CMAKE_ARGS


mkdir -p /tmp/eink-zephyr/images
./scripts/gen-eink-frame.py --solid white -o /tmp/eink-zephyr/images/white.es6f
./scripts/gen-eink-frame.py --lr red blue -o /tmp/eink-zephyr/images/lr.es6f
./scripts/gen-eink-frame.py --bars -o /tmp/eink-zephyr/images/bars.es6f

python3 scripts/eink-check-fixtures.py

./scripts/build-native-sim-eink.sh

LOG=$(mktemp)
timeout 12 ./build-native_sim-eink/zephyr/zephyr.exe 2>&1 | tee "$LOG" || true
grep -q "eink selftest OK" "$LOG"
grep -q "eink display ready" "$LOG"
grep -q "eink store root=/lfs1/eink" "$LOG"
# Provisional custom-board map: 32 MiB NOR, e-ink LittleFS at 0xe40000 (4544 × 4 KiB)
grep -q "flash-controller@0:0xe40000 is 4544" "$LOG"
flash_bytes=$(stat -c%s flash.bin)
test "$flash_bytes" -eq 33554432
echo "OK: selftest + display init + 32 MiB LittleFS"

# SoC UID identity: hwinfo -device_id drives Etablone device_id + shell uid.
UID_LOG=$(mktemp)
rm -f flash.bin
timeout 8 ./build-native_sim-eink/zephyr/zephyr.exe -device_id=0xC0FFEE01 \
  >"$UID_LOG" 2>&1 <<'EOF' || true
eink uid
EOF
grep -q "device=C0FFEE01" "$UID_LOG"
grep -q "soc_uid=C0FFEE01" "$UID_LOG"
echo "OK: SoC UID identity (device=C0FFEE01 / eink uid)"

python3 scripts/eink-check-el133-driver.py

python3 scripts/eink-check-rt1170-profiles.py

python3 scripts/eink-contract-checks.py

# Runtime: mock-SPI ztest asserts the exact init + refresh opcode order
./scripts/build-el133-ztest.sh

# End-to-end: file:// config → import ES6F → schedule show (streaming, no FB)
rm -f flash.bin
python3 scripts/eink-sim-sync-probe.py
echo "OK: file:// sync + scheduled show"

# Same path with LZ4-framed white (expand-on-download default)
./scripts/eink-lz4-wrap.py /tmp/eink-zephyr/images/white.es6f \
  -o /tmp/eink-fixture/white.es6f.lz4
rm -f flash.bin
python3 scripts/eink-sim-sync-probe.py --image /tmp/eink-fixture/white.es6f.lz4
echo "OK: file:// LZ4 expand-on-download + scheduled show"

echo "OK: eink simulator verification gate"
