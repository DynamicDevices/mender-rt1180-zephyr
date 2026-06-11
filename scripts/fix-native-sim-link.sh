#!/usr/bin/env bash
# Deprecated: prefer scripts/build-native-sim.sh (sets CC/CXX=gcc-11 before west build).
# Manual repair if nsi_config was already generated with host gcc-13.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NSI_CONFIG="${ROOT}/build-native_sim/zephyr/NSI/nsi_config"

if [[ ! -f "${NSI_CONFIG}" ]]; then
  echo "Missing ${NSI_CONFIG} — configure native_sim first (./scripts/build-native-sim.sh)." >&2
  exit 1
fi

if ! command -v gcc-11 >/dev/null 2>&1; then
  echo "gcc-11 not found. Install: sudo apt install gcc-11 g++-11 gcc-11-multilib" >&2
  exit 1
fi

sed -i 's|NSI_CC:=.*|NSI_CC:=ccache /usr/bin/gcc-11|' "${NSI_CONFIG}"
echo "Updated: $(grep '^NSI_CC' "${NSI_CONFIG}")"
