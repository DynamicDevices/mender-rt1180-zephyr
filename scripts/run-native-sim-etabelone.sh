#!/usr/bin/env bash
# One-command live e-tabelone -> ES6F -> Zephyr scheduled-display proof.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Avoid inheriting duty-cycle / other wrapper BUILD_DIR overrides.
unset BUILD_DIR NATIVE_SIM_EXTRA_CONF NATIVE_SIM_EXTRA_MODULES NATIVE_SIM_EXTRA_CMAKE_ARGS

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <e-tabelone-device-id> [--sdl]" >&2
    exit 2
fi

DEVICE_ID="$1"
MODE="${2:-}"
BRIDGE_LOG="${EINK_BRIDGE_LOG:-/tmp/eink-etabelone-bridge.log}"
# BooleanOptionalAction needs 3.9+; prefer 3.12 when `python3` is still 3.8.
PY="${EINK_BRIDGE_PYTHON:-}"
if [[ -z "$PY" ]]; then
    if command -v python3.12 >/dev/null 2>&1; then
        PY=python3.12
    elif command -v python3.11 >/dev/null 2>&1; then
        PY=python3.11
    else
        PY=python3
    fi
fi
UPSTREAM_ARGS=()
if [[ -n "${ETABLONE_BASE:-}" ]]; then
    UPSTREAM_ARGS+=(--upstream "$ETABLONE_BASE")
fi
# Prefer device token from frdm/cutover env files when ETABELONE_TOKEN unset.
if [[ -z "${ETABELONE_TOKEN:-}" && -n "${ETABLONE_DEVICE_TOKEN:-}" ]]; then
    export ETABELONE_TOKEN="$ETABLONE_DEVICE_TOKEN"
fi

if [[ -n "$(ss -H -ltn "( sport = :8765 )")" ]]; then
    echo "port 8765 is already in use; stop the existing bridge first" >&2
    exit 1
fi

if [[ "$MODE" == "--sdl" ]]; then
    ./scripts/build-native-sim-eink-sdl.sh
    BINARY="build-native_sim-eink-sdl/zephyr/zephyr.exe"
    HOLD="${EINK_SIM_HOLD_SECONDS:-30}"
else
    ./scripts/build-native-sim-eink.sh
    BINARY="build-native_sim-eink/zephyr/zephyr.exe"
    HOLD="${EINK_SIM_HOLD_SECONDS:-0}"
fi

"$PY" scripts/eink-etabelone-bridge.py \
    --device-id "$DEVICE_ID" \
    "${UPSTREAM_ARGS[@]}" \
    >"$BRIDGE_LOG" 2>&1 &
BRIDGE_PID=$!
trap 'kill "$BRIDGE_PID" 2>/dev/null || true; wait "$BRIDGE_PID" 2>/dev/null || true' EXIT

"$PY" - "$DEVICE_ID" <<'PY'
import sys
import time
import urllib.error
import urllib.request

device_id = sys.argv[1]
url = f"http://127.0.0.1:8765/node/v0/device/{device_id}/config"
deadline = time.time() + 30
last = None
while time.time() < deadline:
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            if response.status == 200:
                print("bridge ready")
                break
    except urllib.error.HTTPError as err:
        # Server is up; upstream may still be unhappy.
        print(f"bridge ready status={err.code}")
        break
    except Exception as err:
        last = err
        time.sleep(0.25)
else:
    raise SystemExit(f"bridge failed to become ready: {last}")
PY

"$PY" scripts/eink-sim-etabelone-probe.py \
    --device-id "$DEVICE_ID" \
    --binary "$BINARY" \
    --fresh \
    --token none \
    --hold "$HOLD"

echo "OK: live e-tabelone schedule displayed by Zephyr native_sim"
