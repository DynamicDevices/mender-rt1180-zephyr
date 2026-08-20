#!/usr/bin/env bash
# Renode PC-path smoke for BOM_POWER_LOOP FRDM e-ink ELF.
# Proof class: renode. Does NOT prove LPUART banners, FRDM silicon, or µA.
#
# PASS: guest reaches BBNSM RTC_EN poke used by bbnsm_rtc_start() in
# eink_power.c (BOM settle → enter_snvs). UART Robot may still FAIL on this ELF.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

ELF="${ELF:-${ROOT}/build-frdm-rt1186-eink/mender-mcu-integration/zephyr/zephyr.elf}"
if [[ ! -f "${ELF}" ]]; then
  echo "missing ${ELF} — BOM_POWER_LOOP=1 ./scripts/build-rt1186-frdm-eink.sh" >&2
  exit 1
fi
if ! grep -q '^CONFIG_APP_EINK_BOM_POWER_LOOP=y$' \
    "${ROOT}/build-frdm-rt1186-eink/mender-mcu-integration/zephyr/.config" 2>/dev/null; then
  echo "ELF build is not BOM_POWER_LOOP=y — rebuild with BOM_POWER_LOOP=1" >&2
  exit 1
fi

export PATH="${HOME}/tmp/renode-venv/bin:/usr/bin:${HOME}/.local/opt/renode-portable:${PATH}"
OUT_DIR="$(mktemp -d /tmp/renode-bom-pc.XXXXXX)"
LOG_CAPTURE="${OUT_DIR}/suite.log"

# Bound wall time: UART Wait can hang once the guest hits WFI (virtual time stalls).
set +e
timeout 25 "${ROOT}/scripts/renode-frdm-eink-uart.sh" >"${LOG_CAPTURE}" 2>&1
RC=$?
set -e

RLOG="$(ls -t /tmp/renode-*/renode-robot.log 2>/dev/null | head -1 || true)"
SUMMARY="${OUT_DIR}/summary.txt"
{
  echo "elf=${ELF}"
  echo "timeout_rc=${RC}"
  echo "renode_robot_log=${RLOG:-none}"
} >"${SUMMARY}"

if [[ -z "${RLOG}" || ! -f "${RLOG}" ]]; then
  echo "FAIL: no renode-robot.log (Renode did not start)" | tee -a "${SUMMARY}"
  echo "summary=${SUMMARY}"
  exit 1
fi

# BBNSM CTRL @ 0x54440008, RTC_EN(0x2) write + sticky-read spin (10000) from bbnsm_rtc_start.
if rg -q '0x54440008, value 0x2' "${RLOG}" && rg -q '0x54440008\. \(9999\)' "${RLOG}"; then
  echo "PASS: renode PC-path reached bbnsm_rtc_start (BOM enter_snvs after settle)" | tee -a "${SUMMARY}"
  echo "note: LPUART banner Robot remains a separate, currently failing check on this ELF" | tee -a "${SUMMARY}"
  echo "summary=${SUMMARY}"
  exit 0
fi

echo "FAIL: BBNSM RTC_EN poke not seen in ${RLOG}" | tee -a "${SUMMARY}"
echo "summary=${SUMMARY}"
exit 1
