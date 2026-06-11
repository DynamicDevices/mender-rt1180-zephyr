#!/usr/bin/env bash
# NSI regenerates nsi_config on pristine/reconfigure and defaults NSI_CC to system gcc (13),
# which breaks libgcc linking for native_sim. Pin host link to gcc-11 after configure.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NSI_CONFIG="${ROOT}/build-native_sim/zephyr/NSI/nsi_config"

if [[ ! -f "${NSI_CONFIG}" ]]; then
  echo "Missing ${NSI_CONFIG} — configure native_sim first (west build -d build-native_sim)." >&2
  exit 1
fi

if ! command -v gcc-11 >/dev/null 2>&1; then
  echo "gcc-11 not found. Install: sudo apt install gcc-11 g++-11 gcc-11-multilib" >&2
  exit 1
fi

sed -i 's|NSI_CC:=.*|NSI_CC:=ccache /usr/bin/gcc-11|' "${NSI_CONFIG}"
echo "Updated: $(grep '^NSI_CC' "${NSI_CONFIG}")"
