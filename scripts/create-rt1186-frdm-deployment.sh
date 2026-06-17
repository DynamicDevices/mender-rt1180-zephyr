#!/usr/bin/env bash
# Upload FRDM-IMXRT1186 zephyr-image artifact and create one Hosted Mender deployment.
# Wrapper around create-rt1180-deployment.sh with FRDM defaults.
set -euo pipefail

# Usage (from West workspace root):
#   ./scripts/create-rt1186-frdm-deployment.sh
#   ./scripts/create-rt1186-frdm-deployment.sh --help
# Defaults: MENDER_DEVICE_TYPE=frdm_imxrt1186, MENDER_BUILD_DIR=build-frdm-rt1186,
#           MENDER_DEVICE_GROUP=rt1180-lab. Requires mender-pat-local.conf and zephyr.mender from build.

export MENDER_DEVICE_TYPE="${MENDER_DEVICE_TYPE:-frdm_imxrt1186}"
export MENDER_DEVICE_GROUP="${MENDER_DEVICE_GROUP:-rt1180-lab}"
export MENDER_BUILD_DIR="${MENDER_BUILD_DIR:-build-frdm-rt1186}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "${ROOT}/scripts/create-rt1180-deployment.sh" "$@"
