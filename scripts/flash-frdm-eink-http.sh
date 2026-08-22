#!/usr/bin/env bash
# Flash FRDM e-ink HTTP image via LinkServer on DPX (vix-usb-host).
#
# Default: **no chip erase** — preserves NVS (Mender device keys) and LittleFS
# (portal `eink creds`). Use ERASE=1 only when you intentionally wipe lab state;
# then run ./scripts/restore-frdm-lab-creds.sh
#
# Usage:
#   ./scripts/flash-frdm-eink-http.sh
#   ERASE=1 ./scripts/flash-frdm-eink-http.sh
#   BUILD_DIR=build-frdm-rt1186-eink-http ./scripts/flash-frdm-eink-http.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-frdm-rt1186-eink-http}"
LAB_HOST="${LAB_HOST:-vix-usb-host}"
SLOT0_ADDR="${SLOT0_ADDR:-0x14020000}"
ERASE="${ERASE:-0}"

MCUBOOT_ELF="${ROOT}/${BUILD_DIR}/mcuboot/zephyr/zephyr.elf"
SIGNED_BIN="${ROOT}/${BUILD_DIR}/mender-mcu-integration/zephyr/zephyr.signed.bin"

if [[ ! -f "$MCUBOOT_ELF" || ! -f "$SIGNED_BIN" ]]; then
  echo "missing images — build with:" >&2
  echo "  FRDM_EINK_HTTP=1 BUILD_DIR=${BUILD_DIR} ./scripts/build-rt1186-frdm-eink.sh" >&2
  exit 1
fi

ssh -o BatchMode=yes "$LAB_HOST" 'mkdir -p /tmp/eink-frdm'
scp -o BatchMode=yes -q "$MCUBOOT_ELF" "${LAB_HOST}:/tmp/eink-frdm/mcuboot.elf"
scp -o BatchMode=yes -q "$SIGNED_BIN" "${LAB_HOST}:/tmp/eink-frdm/zephyr.signed.bin"

ssh -o BatchMode=yes "$LAB_HOST" 'cat > /tmp/eink-flash.sh << "EOS"
#!/bin/bash
set -euo pipefail
export PATH=/usr/local/LinkServer:$PATH
SLOT0_ADDR="${1:-0x14020000}"
ERASE="${2:-0}"
fuser -k 4331/tcp 11111/tcp >/dev/null 2>&1 || true
sleep 1
for p in /proc/[0-9]*; do
  exe=$(readlink "$p/exe" 2>/dev/null || true)
  case "$exe" in
    *LinkServer|*redlinkserv|*crt_emu_cm_redlink) kill -9 "${p#/proc/}" 2>/dev/null || true ;;
  esac
done
sleep 1
if [[ "$ERASE" == "1" ]]; then
  echo "ERASE=1: full chip erase (wipes Mender NVS + portal LittleFS)"
  LinkServer flash -u none MIMXRT1186:FRDM-IMXRT1186 erase || true
fi
LinkServer flash -u none MIMXRT1186:FRDM-IMXRT1186 load \
  /tmp/eink-frdm/zephyr.signed.bin:${SLOT0_ADDR}
LinkServer flash -u none MIMXRT1186:FRDM-IMXRT1186 load -R \
  /tmp/eink-frdm/mcuboot.elf
echo OK_FLASH
EOS
chmod +x /tmp/eink-flash.sh
bash /tmp/eink-flash.sh '"$SLOT0_ADDR"' '"$ERASE"

echo "Flashed ${BUILD_DIR} (ERASE=${ERASE}) slot0@${SLOT0_ADDR}"
echo "Console: nc ${CONSOLE_HOST:-192.168.2.10} 2325"
if [[ "$ERASE" == "1" ]]; then
  echo "Next: ./scripts/restore-frdm-lab-creds.sh"
fi
