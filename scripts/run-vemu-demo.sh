#!/usr/bin/env bash
# Build Zephyr hello_world for nRF5340 DK (app core) and print vemu load instructions.
# Build dir is gitignored (build-nrf5340-vemu/). Do not commit zephyr.elf.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

BOARD="nrf5340dk/nrf5340/cpuapp"
BUILD_DIR="${BUILD_DIR:-build-nrf5340-vemu}"
SAMPLE="${ROOT}/zephyr/samples/hello_world"
ELF="${ROOT}/${BUILD_DIR}/zephyr/zephyr.elf"
VEMU_BOARD_ID="nordic,nrf5340-dk-cpuapp"

if [[ ! -f "${ROOT}/zephyr/zephyr-env.sh" ]]; then
  echo "Missing Zephyr workspace. Run 'west update' from mender-mcu-integration first." >&2
  exit 1
fi
# shellcheck source=/dev/null
source "${ROOT}/zephyr/zephyr-env.sh"

rebuild=false
if [[ "${1:-}" == "--rebuild" ]]; then
  rebuild=true
  shift
fi

if [[ "${rebuild}" == true ]] || [[ ! -f "${ELF}" ]]; then
  echo "Building hello_world for ${BOARD} ..."
  west build -p -d "${BUILD_DIR}" --board "${BOARD}" "${SAMPLE}" "$@"
else
  echo "Using existing ${ELF} (pass --rebuild to force a clean build)"
fi

if [[ ! -f "${ELF}" ]]; then
  echo "Build failed: missing ${ELF}" >&2
  exit 1
fi

echo ""
echo "=== vemu demo firmware ==="
echo "Board (Zephyr):  ${BOARD}"
echo "Board (vemu id): ${VEMU_BOARD_ID}"
echo "ELF:             ${ELF}"
echo ""
echo "Build command (equivalent):"
echo "  source zephyr/zephyr-env.sh"
echo "  west build -p -d ${BUILD_DIR} --board ${BOARD} zephyr/samples/hello_world"
echo ""
echo "--- vemulator.com (browser) ---"
echo "1. Open https://vemulator.com/docs/getting-started"
echo "2. Use the site emulator UI: board ${VEMU_BOARD_ID}, image kind \"elf\""
echo "3. Load this file: ${ELF}"
echo "   Expect UART: \"Hello World! ${BOARD}\""
echo ""
echo "--- npm (@swedishembedded/vemu) ---"
echo "  npm install @swedishembedded/vemu"
echo ""
cat << 'NODE'
  import { readFileSync } from "node:fs";
  import { initVemu, Emulator } from "@swedishembedded/vemu";

  await initVemu();
  const elf = new Uint8Array(readFileSync("build-nrf5340-vemu/zephyr/zephyr.elf"));
  const emu = new Emulator("nordic,nrf5340-dk-cpuapp", elf, "elf");
  emu.onEvent((events) => {
    for (const ev of events) {
      if (ev.kind === "uart.tx") process.stdout.write(Buffer.from(ev.payload.bytes));
    }
  });
  for (let i = 0; i < 500000; i++) if (!emu.step()) break;
NODE
echo ""
echo "--- headless (Node, vemu in ~/) ---"
echo "  node ~/run-vemu.mjs ${ELF} 200"
echo "  ./scripts/test-vemu.sh              # build (if needed) + run headless"
echo "  ./scripts/test-vemu.sh --browser    # browser load steps only"
echo ""
echo "Note: mender-mcu-integration CI targets nrf52840dk; vemu freeware uses nRF5340 (${VEMU_BOARD_ID})."
