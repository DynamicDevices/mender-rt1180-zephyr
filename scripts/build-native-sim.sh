#!/usr/bin/env bash
# native_sim NSI final link uses -m32. Ubuntu 24.04 default GCC 13 has no -m32 libgcc
# unless gcc-multilib is installed. Zephyr sets NSI_CC from host CMAKE_C_COMPILER (/usr/bin/gcc).
# Configure first, pin NSI_CC to gcc-11 (gcc-11-multilib), then build — one script, no failed link.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ -f zephyr/zephyr-env.sh ]]; then
  # shellcheck source=/dev/null
  source zephyr/zephyr-env.sh
fi

usage() {
  cat <<EOF
Usage: build-native-sim.sh [--incremental] [extra west cmake args...]

Phase 0b Mender smoke: Zephyr native_sim (requires gcc-11 + gcc-11-multilib).

Environment:
  BUILD_DIR              Default: build-native_sim

Examples (from West workspace root):
  ./scripts/build-native-sim.sh
  ./scripts/build-native-sim.sh --incremental

Run: ./build-native_sim/zephyr/zephyr.exe (or test-mender-native-sim.sh)
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if ! command -v gcc-11 >/dev/null 2>&1; then
  echo "gcc-11 required (with gcc-11-multilib for -m32 NSI link)." >&2
  echo "Install: sudo apt install gcc-11 g++-11 gcc-11-multilib" >&2
  echo "Or install gcc-multilib g++-multilib for default GCC 13 and use west build directly." >&2
  exit 1
fi

BUILD_DIR="${BUILD_DIR:-build-native_sim}"
WEST_CMAKE_ONLY=(--cmake-only)
WEST_BUILD=(-p)
if [[ "${1:-}" == "--incremental" ]]; then
  WEST_CMAKE_ONLY=()
  WEST_BUILD=()
  shift
fi

# Optional extra Kconfig fragments (semicolon-separated) appended after
# mender-local.conf, and extra Zephyr modules (e.g. improv-zephyr). Used by
# wrapper scripts such as build-native-sim-improv.sh.
EXTRA_CONF="mender-local.conf"
if [[ -n "${NATIVE_SIM_EXTRA_CONF:-}" ]]; then
  EXTRA_CONF="${EXTRA_CONF};${NATIVE_SIM_EXTRA_CONF}"
fi
EXTRA_MODULE_ARGS=()
if [[ -n "${NATIVE_SIM_EXTRA_MODULES:-}" ]]; then
  EXTRA_MODULE_ARGS=(-DZEPHYR_EXTRA_MODULES="${NATIVE_SIM_EXTRA_MODULES}")
fi

west build "${WEST_BUILD[@]}" -d "${BUILD_DIR}" --board native_sim mender-mcu-integration \
  "${WEST_CMAKE_ONLY[@]}" -- \
  -DEXTRA_CONF_FILE="${EXTRA_CONF}" "${EXTRA_MODULE_ARGS[@]}" "$@"

"${ROOT}/scripts/fix-native-sim-link.sh"

west build -d "${BUILD_DIR}"

EXE="${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
if [[ ! -f "${EXE}" ]]; then
  echo "Build failed: missing ${EXE}" >&2
  exit 1
fi
echo "OK: ${EXE}"
