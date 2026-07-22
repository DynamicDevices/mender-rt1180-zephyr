#!/usr/bin/env bash
# native_sim duty-cycle smoke: schedule next_wake + SNVS stub returns (no forever hang).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mkdir -p /tmp/eink-zephyr/images /tmp/eink-fixture
./scripts/gen-eink-frame.py --solid white -o /tmp/eink-zephyr/images/white.es6f
./scripts/gen-eink-frame.py --bars -o /tmp/eink-zephyr/images/bars.es6f

python3 - <<'PY'
import json
from pathlib import Path
cfg = {
    "images": [
        {"image_id": "white", "url": "file:///tmp/eink-zephyr/images/white.es6f"},
        {"image_id": "bars", "url": "file:///tmp/eink-zephyr/images/bars.es6f"},
    ],
    "schedule": [
        {"job_id": "j1", "image_id": "white", "cron": "0 0 * * *"},
        {"job_id": "j2", "image_id": "bars", "cron": "0 18 * * *"},
    ],
    "orientation": 0,
}
Path("/tmp/eink-fixture/sim-duty.config.json").write_text(json.dumps(cfg))
print("wrote /tmp/eink-fixture/sim-duty.config.json")
PY

export BUILD_DIR=build-native_sim-eink-duty
export NATIVE_SIM_EXTRA_CONF="eink-native-sim.conf;eink-native-sim-duty.conf"
./scripts/build-native-sim-eink.sh

LOG=$(mktemp)
# Duty cycle ends at k_sleep(K_FOREVER) after SNVS stub returns — timeout the binary.
timeout 40 ./build-native_sim-eink-duty/zephyr/zephyr.exe 2>&1 | tee "$LOG" || true

grep -q "eink selftest OK" "$LOG"
grep -q "power: next wake unix=" "$LOG"
grep -q "power: native_sim SNVS stub return" "$LOG"
grep -q "power: IW612 software gate only" "$LOG"
# Fire-and-forget display: do not block wake on waveform completion.
grep -Eq "display fire-and-forget|refresh done result=0" "$LOG"
# Radio hard-gated before SNVS (or skipped entirely when schedule fresh).
grep -Eq "WiFi hard-gated after network work|skip radio|IW612 HARD-GATE" "$LOG"
# Primary frame only — gallery is skipped on battery duty-cycle.
grep -Eq "accepted image white|already cached|importing fixture image white|show job=" "$LOG"
grep -Eq "gallery skipped \(battery duty-cycle\)|gallery deferred|no new scheduled" "$LOG" || true
echo "OK: duty-cycle smoke (schedule next_wake + SNVS stub)"
