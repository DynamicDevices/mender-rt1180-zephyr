#!/usr/bin/env bash
# Generate SPDX SBOMs for Mender MCU sysbuild trees (CRA WS3).
#
# Zephyr west spdx requires CMake file-based API query files created *before*
# the build (`west spdx --init -d BUILD_DIR`). If a build tree was created
# without that init step, re-init + rebuild before expecting a full SBOM.
#
# Requires: Python package `reuse` (west spdx dependency).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ -d "${ROOT}/.tools/bin" ]]; then
  export PATH="${ROOT}/.tools/bin:${PATH}"
fi

if [[ -f zephyr/zephyr-env.sh ]]; then
  # shellcheck source=/dev/null
  source zephyr/zephyr-env.sh
fi

EVK_DIR="${EVK_BUILD_DIR:-build-rt1180-evk}"
FRDM_DIR="${FRDM_BUILD_DIR:-build-frdm-rt1186}"
RT1170_DIR="${RT1170_BUILD_DIR:-build-rt1170-evk}"
OUT_DIR="${SBOM_OUT_DIR:-sbom}"
ARTIFACT_NAME="${CONFIG_MENDER_ARTIFACT_NAME:-dev-1}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"

usage() {
  cat <<EOF
Usage: generate-sbom.sh [--evk-only | --frdm-only | --rt1170-only]

Run west spdx for Mender sysbuild output directories (app + MCUboot child images).

Environment:
  EVK_BUILD_DIR          Default: build-rt1180-evk
  FRDM_BUILD_DIR         Default: build-frdm-rt1186
  RT1170_BUILD_DIR       Default: build-rt1170-evk
  SBOM_OUT_DIR           Default: sbom (created under workspace root)
  CONFIG_MENDER_ARTIFACT_NAME  Label for output filenames (default: dev-1)

Prerequisite:
  1. west spdx --init -d <sysbuild-dir>   # once, before west build
  2. matching build script completed
  3. pip install reuse   # for the same Python that runs west

See mender-mcu-integration/docs/CRA-COMPLIANCE.md (WS3).
EOF
}

TARGETS=(evk frdm rt1170)
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
elif [[ "${1:-}" == "--evk-only" ]]; then
  TARGETS=(evk)
elif [[ "${1:-}" == "--frdm-only" ]]; then
  TARGETS=(frdm)
elif [[ "${1:-}" == "--rt1170-only" ]]; then
  TARGETS=(rt1170)
elif [[ -n "${1:-}" ]]; then
  echo "error: unknown option: $1" >&2
  usage >&2
  exit 1
fi

if ! python3 -c 'import reuse' 2>/dev/null; then
  echo "error: Python package 'reuse' is required for west spdx" >&2
  echo "  Install with: python3 -m pip install --user reuse" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

run_spdx() {
  local label="$1"
  local build_dir="$2"
  local app_dir="${build_dir}/mender-mcu-integration"
  local mcuboot_dir="${build_dir}/mcuboot"
  local dest="${OUT_DIR}/${label}-${ARTIFACT_NAME}-${STAMP}"

  if [[ ! -d "${app_dir}" ]]; then
    echo "error: missing ${app_dir} — run the matching build script first" >&2
    exit 1
  fi

  mkdir -p "${dest}/app"
  echo "==> ${label}: west spdx (application) ${app_dir}"
  if ! west spdx -d "${app_dir}" -s "${dest}/app" -n "etablone-${label}-app-${ARTIFACT_NAME}"; then
    echo "error: west spdx failed for ${app_dir}" >&2
    echo "hint: west spdx --init -d ${build_dir} && rebuild, then re-run" >&2
    exit 1
  fi

  if [[ -d "${mcuboot_dir}" ]]; then
    mkdir -p "${dest}/mcuboot"
    echo "==> ${label}: west spdx (MCUboot) ${mcuboot_dir}"
    if ! west spdx -d "${mcuboot_dir}" -s "${dest}/mcuboot" -n "etablone-${label}-mcuboot-${ARTIFACT_NAME}"; then
      echo "warning: MCUboot SBOM failed for ${mcuboot_dir}" >&2
    fi
  else
    echo "warning: ${mcuboot_dir} not found — skipping MCUboot SBOM" >&2
  fi

  echo "    outputs under ${dest}/"
}

for t in "${TARGETS[@]}"; do
  case "${t}" in
    evk) run_spdx "evk" "${EVK_DIR}" ;;
    frdm) run_spdx "frdm" "${FRDM_DIR}" ;;
    rt1170) run_spdx "rt1170" "${RT1170_DIR}" ;;
  esac
done

echo "OK: SBOM files written under ${OUT_DIR}/"
