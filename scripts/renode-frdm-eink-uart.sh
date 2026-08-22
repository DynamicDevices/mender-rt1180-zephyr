#!/usr/bin/env bash
# Renode UART smoke of the FRDM-IMXRT1186 e-ink ELF (same image as flash).
# Proof class: renode. Does not prove FRDM silicon, e-tabelone, or Spectra 6.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

ELF="${ELF:-${ROOT}/build-frdm-rt1186-eink/mender-mcu-integration/zephyr/zephyr.elf}"
REPL="${REPL:-${ROOT}/tests/renode/frdm_imxrt1186.repl}"
ROBOT="${ROBOT:-${ROOT}/tests/renode/frdm_eink_uart.robot}"

if [[ ! -f "${ELF}" ]]; then
  echo "missing ${ELF} — build first: ./scripts/build-rt1186-frdm-eink.sh" >&2
  exit 1
fi

# Renode's runner needs Python 3.12+ with robotframework (conda 3.8 is too old).
export PATH="${HOME}/tmp/renode-venv/bin:/usr/bin:${HOME}/.local/opt/renode-portable:${PATH}"
if ! command -v renode-test >/dev/null; then
  echo "renode-test not on PATH (need ~/.local/opt/renode-portable)" >&2
  exit 1
fi

# First RAM-backed VTOR line from find-vtor.py
mapfile -t VTOR_LINES < <(python3 "${ROOT}/scripts/find-vtor.py" "${ELF}" | awk '/RAM VTOR=/{print; exit}')
if [[ ${#VTOR_LINES[@]} -eq 0 ]]; then
  echo "find-vtor.py found no RAM vector table in ${ELF}" >&2
  exit 1
fi
#  RAM VTOR=0x14020400 SP=0x3877d828 PC=0x1402dab0
eval "$(echo "${VTOR_LINES[0]}" | awk '{
  for (i=1;i<=NF;i++) {
    split($i,a,"=");
    if (a[1]=="VTOR") printf "VTOR=%s\n", a[2];
    if (a[1]=="SP") printf "SP=%s\n", a[2];
    if (a[1]=="PC") printf "PC=%s\n", a[2];
  }
}')"

echo "ELF=${ELF}"
echo "VTOR=${VTOR} SP=${SP} PC=${PC}"

EXPECT_BOM_LOOP="${EXPECT_BOM_LOOP:-}"
if [[ -z "${EXPECT_BOM_LOOP}" ]] && [[ -f "${ROOT}/build-frdm-rt1186-eink/mender-mcu-integration/zephyr/.config" ]]; then
  if grep -q '^CONFIG_APP_EINK_BOM_POWER_LOOP=y$' \
      "${ROOT}/build-frdm-rt1186-eink/mender-mcu-integration/zephyr/.config"; then
    EXPECT_BOM_LOOP=1
  fi
fi
echo "EXPECT_BOM_LOOP=${EXPECT_BOM_LOOP:-0}"

exec renode-test \
  --variable "ROOT:${ROOT}" \
  --variable "REPL:@${REPL}" \
  --variable "ELF:@${ELF}" \
  --variable "VTOR:${VTOR}" \
  --variable "SP:${SP}" \
  --variable "PC:${PC}" \
  --variable "EXPECT_BOM_LOOP:${EXPECT_BOM_LOOP:-}" \
  "${ROBOT}"
