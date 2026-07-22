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
Usage: build-native-sim.sh [--incremental|--reconfigure|--pristine] [extra west cmake args...]

Phase 0b Mender smoke: Zephyr native_sim (requires gcc-11 + gcc-11-multilib).

  (default)       ninja-only if configured, else west build (no -p)
  --incremental   same as default fast path
  --reconfigure   west build without pristine
  --pristine      west build -p (full clean)

Environment:
  BUILD_DIR              Default: build-native_sim
  CMAKE_BUILD_PARALLEL_LEVEL  Default: nproc

Examples (from West workspace root):
  ./scripts/build-native-sim.sh
  ./scripts/build-native-sim.sh --pristine
  ./scripts/build-native-sim-eink-sdl.sh

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
# Fastest path is default: ninja-only when configured; else west without -p.
MODE=auto
if [[ "${1:-}" == "--incremental" || "${1:-}" == "-i" ]]; then
  MODE=fast
  shift
elif [[ "${1:-}" == "--reconfigure" || "${1:-}" == "-r" ]]; then
  MODE=west
  shift
elif [[ "${1:-}" == "--pristine" || "${1:-}" == "-p" ]]; then
  MODE=pristine
  shift
fi

export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"
if command -v ccache >/dev/null 2>&1; then
  export CMAKE_C_COMPILER_LAUNCHER="${CMAKE_C_COMPILER_LAUNCHER:-ccache}"
  export CMAKE_CXX_COMPILER_LAUNCHER="${CMAKE_CXX_COMPILER_LAUNCHER:-ccache}"
fi

# Fast iterative path: skip west/CMake when the tree is already configured.
if [[ "${MODE}" == "fast" || "${MODE}" == "auto" ]] && [[ -f "${BUILD_DIR}/build.ninja" ]]; then
  echo "fast rebuild: ninja -C ${BUILD_DIR} (-j${CMAKE_BUILD_PARALLEL_LEVEL})"
  ninja -C "${BUILD_DIR}"
  EXE="${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
  if [[ ! -f "${EXE}" ]]; then
    echo "Build failed: missing ${EXE}" >&2
    exit 1
  fi
  echo "OK: ${EXE}"
  exit 0
fi

WEST_CMAKE_ONLY=(--cmake-only)
WEST_BUILD=()
if [[ "${MODE}" == "pristine" ]]; then
  WEST_BUILD=(-p)
fi
# First-time / auto without build.ninja: configure then link-fix then build (no -p).
if [[ "${MODE}" == "auto" || "${MODE}" == "fast" || "${MODE}" == "west" ]]; then
  WEST_CMAKE_ONLY=()
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

EXTRA_CMAKE=()
if [[ -n "${NATIVE_SIM_EXTRA_CMAKE_ARGS:-}" ]]; then
  # shellcheck disable=SC2206
  EXTRA_CMAKE=(${NATIVE_SIM_EXTRA_CMAKE_ARGS})
fi

CCACHE_CMAKE=()
if [[ -n "${CMAKE_C_COMPILER_LAUNCHER:-}" ]]; then
  CCACHE_CMAKE=(-DCMAKE_C_COMPILER_LAUNCHER="${CMAKE_C_COMPILER_LAUNCHER}"
    -DCMAKE_CXX_COMPILER_LAUNCHER="${CMAKE_CXX_COMPILER_LAUNCHER}")
fi

west build "${WEST_BUILD[@]}" -d "${BUILD_DIR}" --board native_sim mender-mcu-integration \
  "${WEST_CMAKE_ONLY[@]}" -- \
  -DEXTRA_CONF_FILE="${EXTRA_CONF}" "${EXTRA_MODULE_ARGS[@]}" "${EXTRA_CMAKE[@]}" \
  "${CCACHE_CMAKE[@]}" "$@"

"${ROOT}/scripts/fix-native-sim-link.sh"

west build -d "${BUILD_DIR}"

EXE="${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
if [[ ! -f "${EXE}" ]]; then
  echo "Build failed: missing ${EXE}" >&2
  exit 1
fi
echo "OK: ${EXE}"
