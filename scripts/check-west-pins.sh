#!/usr/bin/env bash
# Gate west.yml pins: Zephyr + mender-mcu (+ optional psa_crypto_driver).
# Product-local expected values live in pins/ next to this repo root.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WEST=${1:-}
PRODUCT_HINT=${DD_ZEPHYR_PRODUCT:-}

if [[ -z "$WEST" ]]; then
  if [[ -f "$ROOT/f1-controller/west.yml" ]]; then
    WEST="$ROOT/f1-controller/west.yml"
    PRODUCT_HINT=${PRODUCT_HINT:-f1}
  elif [[ -f "$ROOT/room-display/west.yml" ]]; then
    WEST="$ROOT/room-display/west.yml"
    PRODUCT_HINT=${PRODUCT_HINT:-room-display}
  elif [[ -f "$ROOT/mender-mcu-integration/west.yml" ]]; then
    WEST="$ROOT/mender-mcu-integration/west.yml"
    PRODUCT_HINT=${PRODUCT_HINT:-eink}
  else
    echo "error: cannot find west.yml under $ROOT" >&2
    exit 1
  fi
fi
[[ "$WEST" != /* ]] && WEST="$ROOT/$WEST"
[[ -f "$WEST" ]] || { echo "error: west.yml not found: $WEST" >&2; exit 1; }

rev_of() {
  local name=$1
  python3 - "$WEST" "$name" <<'PY'
import re, sys
path, name = sys.argv[1], sys.argv[2]
text = open(path, encoding="utf-8").read()
m = re.search(
    rf"(?ms)^[ \t]*-[ \t]*name:[ \t]*{re.escape(name)}\b.*?^[ \t]*revision:[ \t]*['\"]?(\S+?)['\"]?\s*$",
    text,
)
if not m:
    sys.exit(2)
print(m.group(1).strip())
PY
}

expect_file() {
  local f=$1
  [[ -f "$f" ]] || { echo "error: missing expected pin file: $f" >&2; exit 1; }
  tr -d '[:space:]' < "$f"
}

# --- Zephyr (shared cadence across F1 / room-display / e-ink) ---
ZEPHYR_ACTUAL=$(rev_of zephyr) || { echo "error: no zephyr project in $WEST" >&2; exit 2; }
ZEPHYR_EXPECT=$(expect_file "$ROOT/pins/ZEPHYR_PIN")
if [[ "$ZEPHYR_ACTUAL" != "$ZEPHYR_EXPECT" ]]; then
  echo "error: zephyr pin drift" >&2
  echo "  west.yml: $ZEPHYR_ACTUAL" >&2
  echo "  pins/ZEPHYR_PIN: $ZEPHYR_EXPECT" >&2
  echo "  bump all three products together (zephyr-imx-rt-project skill)" >&2
  exit 5
fi
echo "ok: zephyr @$ZEPHYR_ACTUAL"

# --- mender-mcu (shared DD_PIN) ---
if [[ -x "$ROOT/scripts/check-mender-mcu-pin.sh" || -f "$ROOT/scripts/check-mender-mcu-pin.sh" ]]; then
  chmod +x "$ROOT/scripts/check-mender-mcu-pin.sh"
  "$ROOT/scripts/check-mender-mcu-pin.sh" "$WEST"
else
  echo "error: missing scripts/check-mender-mcu-pin.sh" >&2
  exit 1
fi

# --- psa_crypto_driver (ELE products only) ---
if [[ -f "$ROOT/pins/PSA_CRYPTO_DRIVER_PIN" ]]; then
  PSA_ACTUAL=$(rev_of psa_crypto_driver) || {
    echo "error: pins/PSA_CRYPTO_DRIVER_PIN present but no psa_crypto_driver in west.yml" >&2
    exit 2
  }
  PSA_EXPECT=$(expect_file "$ROOT/pins/PSA_CRYPTO_DRIVER_PIN")
  if [[ ${#PSA_ACTUAL} -ne 40 ]]; then
    echo "error: psa_crypto_driver revision must be full 40-char SHA (got $PSA_ACTUAL)" >&2
    exit 3
  fi
  if [[ "$PSA_ACTUAL" != "$PSA_EXPECT" ]]; then
    echo "error: psa_crypto_driver pin drift" >&2
    echo "  west.yml: $PSA_ACTUAL" >&2
    echo "  pins/PSA_CRYPTO_DRIVER_PIN: $PSA_EXPECT" >&2
    echo "  SoT: F1 docs/ELE.md + DynamicDevices/psa_crypto_driver" >&2
    exit 5
  fi
  echo "ok: psa_crypto_driver @$PSA_ACTUAL"
else
  if rev_of psa_crypto_driver >/dev/null 2>&1; then
    echo "error: west.yml has psa_crypto_driver but pins/PSA_CRYPTO_DRIVER_PIN is missing" >&2
    exit 4
  fi
fi

echo "ok: west pins ($PRODUCT_HINT)"
