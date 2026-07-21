#!/usr/bin/env bash
# One-command live e-tabelone -> ES6F -> Zephyr scheduled-display proof.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <e-tabelone-device-id> [--sdl]" >&2
    exit 2
fi

DEVICE_ID="$1"
MODE="${2:-}"
BRIDGE_LOG="${EINK_BRIDGE_LOG:-/tmp/eink-etabelone-bridge.log}"

if [[ -n "$(ss -H -ltn "( sport = :8765 )")" ]]; then
    echo "port 8765 is already in use; stop the existing bridge first" >&2
    exit 1
fi

if [[ "$MODE" == "--sdl" ]]; then
    ./scripts/build-native-sim-eink-sdl.sh --incremental
    BINARY="build-native_sim-eink-sdl/zephyr/zephyr.exe"
    HOLD="${EINK_SIM_HOLD_SECONDS:-30}"
else
    ./scripts/build-native-sim-eink.sh --incremental
    BINARY="build-native_sim-eink/zephyr/zephyr.exe"
    HOLD="${EINK_SIM_HOLD_SECONDS:-0}"
fi

python3 scripts/eink-etabelone-bridge.py \
    --device-id "$DEVICE_ID" >"$BRIDGE_LOG" 2>&1 &
BRIDGE_PID=$!
trap 'kill "$BRIDGE_PID" 2>/dev/null || true; wait "$BRIDGE_PID" 2>/dev/null || true' EXIT

python3 - "$DEVICE_ID" <<'PY'
import sys
import time
import urllib.request

device_id = sys.argv[1]
url = f"http://127.0.0.1:8765/node/v0/device/{device_id}/config"
deadline = time.time() + 30
while time.time() < deadline:
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            if response.status == 200:
                print("bridge ready")
                break
    except Exception:
        time.sleep(0.25)
else:
    raise SystemExit("bridge failed to become ready")
PY

python3 scripts/eink-sim-etabelone-probe.py \
    --device-id "$DEVICE_ID" \
    --binary "$BINARY" \
    --fresh \
    --hold "$HOLD"

echo "OK: live e-tabelone schedule displayed by Zephyr native_sim"
