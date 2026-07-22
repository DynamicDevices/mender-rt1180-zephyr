#!/usr/bin/env bash
# Build native_sim/native/64 with SDL e-ink overlay (amd64 SDL2).
# The default 32-bit native_sim build cannot link host SDL2; use this for visual checks.
#
# Iterative testing defaults to a fast ninja-only rebuild when the build dir
# already exists. Use --reconfigure after Kconfig/overlay/CMake changes, or
# --pristine for a clean tree.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${BUILD_DIR:-build-native_sim-eink-sdl}"
EXTRA_OVERLAY="${ROOT}/mender-mcu-integration/boards/native_sim_eink_sdl.overlay"
EL133_MODULE="${ROOT}/mender-mcu-integration/modules/eink-el133"
CONF_FRAG="${ROOT}/mender-mcu-integration/eink-native-sim-sdl.conf"

export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"
if command -v ccache >/dev/null 2>&1; then
  export CMAKE_C_COMPILER_LAUNCHER="${CMAKE_C_COMPILER_LAUNCHER:-ccache}"
  export CMAKE_CXX_COMPILER_LAUNCHER="${CMAKE_CXX_COMPILER_LAUNCHER:-ccache}"
fi

MODE=auto
WEST_EXTRA=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --incremental|-i)
      MODE=fast
      shift
      ;;
    --reconfigure|--west|-r)
      MODE=west
      shift
      ;;
    --pristine|-p)
      MODE=pristine
      shift
      ;;
    -h|--help)
      cat <<'HELP'
Usage: build-native-sim-eink-sdl.sh [--incremental|--reconfigure|--pristine] [west args...]

  (default)       fastest: ninja-only if configured, else west (no -p)
  --incremental   same as default fast path
  --reconfigure   west build (re-run CMake/Kconfig, no pristine)
  --pristine      west build -p (full clean configure)
HELP
      exit 0
      ;;
    *)
      WEST_EXTRA+=("$1")
      shift
      ;;
  esac
done

WEST_CMAKE=(
  -DEXTRA_CONF_FILE="boards/native_sim.conf;mender-local.conf;eink-native-sim-sdl.conf"
  -DZEPHYR_EXTRA_MODULES="${EL133_MODULE}"
  -DDTC_OVERLAY_FILE="${EXTRA_OVERLAY}"
)
if [[ -n "${CMAKE_C_COMPILER_LAUNCHER:-}" ]]; then
  WEST_CMAKE+=(-DCMAKE_C_COMPILER_LAUNCHER="${CMAKE_C_COMPILER_LAUNCHER}")
  WEST_CMAKE+=(-DCMAKE_CXX_COMPILER_LAUNCHER="${CMAKE_CXX_COMPILER_LAUNCHER}")
fi

configured() {
  [[ -f "${BUILD_DIR}/build.ninja" && -x "${BUILD_DIR}/zephyr/zephyr.exe" ]] || \
    [[ -f "${BUILD_DIR}/build.ninja" ]]
}

inputs_newer_than_build() {
  local stamp="${BUILD_DIR}/build.ninja"
  [[ -f "$stamp" ]] || return 0
  local f
  for f in "$CONF_FRAG" "$EXTRA_OVERLAY" \
    "${ROOT}/mender-mcu-integration/boards/native_sim.conf" \
    "${ROOT}/mender-mcu-integration/mender-local.conf"; do
    [[ -f "$f" ]] || continue
    if [[ "$f" -nt "$stamp" ]]; then
      return 0
    fi
  done
  return 1
}

do_west() {
  local pristine_flag=()
  if [[ "$1" == pristine ]]; then
    pristine_flag=(-p)
  fi
  west build "${pristine_flag[@]}" -d "${BUILD_DIR}" --board native_sim/native/64 \
    mender-mcu-integration -- "${WEST_CMAKE[@]}" "${WEST_EXTRA[@]}"
}

do_ninja() {
  echo "fast rebuild: ninja -C ${BUILD_DIR} (-j${CMAKE_BUILD_PARALLEL_LEVEL})"
  ninja -C "${BUILD_DIR}"
}

case "$MODE" in
  pristine)
    do_west pristine
    ;;
  west)
    do_west west
    ;;
  fast)
    if ! configured; then
      echo "build dir not configured; running west configure+build" >&2
      do_west west
    else
      do_ninja
    fi
    ;;
  auto)
    if ! configured; then
      do_west west
    elif inputs_newer_than_build; then
      echo "conf/overlay newer than build — reconfigure"
      do_west west
    else
      do_ninja
    fi
    ;;
esac

EXE="${ROOT}/${BUILD_DIR}/zephyr/zephyr.exe"
if [[ ! -f "${EXE}" ]]; then
  echo "Build failed: missing ${EXE}" >&2
  exit 1
fi
echo "OK: ${EXE}"
echo "Run (needs DISPLAY / X11): ${EXE}"
echo "  then: eink show /tmp/eink-zephyr/images/lr.es6f jobLR"
