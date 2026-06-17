#!/usr/bin/env bash
# Generate SPDX SBOMs for RT118x CM33 sysbuild trees (CRA WS3).
# Requires a completed west sysbuild in each target directory first.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ -f zephyr/zephyr-env.sh ]]; then
  # shellcheck source=/dev/null
  source zephyr/zephyr-env.sh
fi

EVK_DIR="${EVK_BUILD_DIR:-build-rt1180-evk}"
FRDM_DIR="${FRDM_BUILD_DIR:-build-frdm-rt1186}"
OUT_DIR="${SBOM_OUT_DIR:-sbom}"
ARTIFACT_NAME="${CONFIG_MENDER_ARTIFACT_NAME:-dev-1}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"

usage() {
  cat <<EOF
Usage: generate-sbom.sh [--evk-only | --frdm-only]

Run west spdx for RT118x Mender sysbuild output directories (app + MCUboot child images).

Environment:
  EVK_BUILD_DIR          Default: build-rt1180-evk
  FRDM_BUILD_DIR         Default: build-frdm-rt1186
  SBOM_OUT_DIR           Default: sbom (created under workspace root)
  CONFIG_MENDER_ARTIFACT_NAME  Label for output filenames (default: dev-1)

Prerequisite: run ./scripts/build-rt1180-evk.sh and/or ./scripts/build-rt1186-frdm.sh first.

See PROJECT-NOTES — CRA compliance programme (WS3, milestone M-009).
EOF
}

TARGETS=(evk frdm)
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
elif [[ "${1:-}" == "--evk-only" ]]; then
  TARGETS=(evk)
elif [[ "${1:-}" == "--frdm-only" ]]; then
  TARGETS=(frdm)
elif [[ -n "${1:-}" ]]; then
  echo "error: unknown option: $1" >&2
  usage >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

run_spdx() {
  local label="$1"
  local build_dir="$2"
  local app_dir="${build_dir}/mender-mcu-integration"
  local mcuboot_dir="${build_dir}/mcuboot"

  if [[ ! -d "${app_dir}" ]]; then
    echo "error: missing ${app_dir} — run the matching build script first" >&2
    exit 1
  fi

  local out_app="${OUT_DIR}/${label}-${ARTIFACT_NAME}-${STAMP}-app.spdx"
  local out_boot="${OUT_DIR}/${label}-${ARTIFACT_NAME}-${STAMP}-mcuboot.spdx"

  echo "==> ${label}: west spdx (application) ${build_dir}"
  west spdx -d "${app_dir}" -o "${out_app}"

  if [[ -d "${mcuboot_dir}" ]]; then
    echo "==> ${label}: west spdx (MCUboot) ${build_dir}"
    west spdx -d "${mcuboot_dir}" -o "${out_boot}"
  else
    echo "warning: ${mcuboot_dir} not found — skipping MCUboot SBOM" >&2
  fi
}

for t in "${TARGETS[@]}"; do
  case "${t}" in
    evk) run_spdx "evk" "${EVK_DIR}" ;;
    frdm) run_spdx "frdm" "${FRDM_DIR}" ;;
  esac
done

echo "OK: SBOM files written under ${OUT_DIR}/"
