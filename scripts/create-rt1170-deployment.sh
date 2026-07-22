#!/usr/bin/env bash
# Upload MIMXRT1170-EVK zephyr-image artifact and create one Hosted Mender deployment.
# Wrapper around create-rt1180-deployment.sh with RT1170 defaults.
set -euo pipefail

# Usage (from West workspace root):
#   ./scripts/create-rt1170-deployment.sh
#   ./scripts/create-rt1170-deployment.sh --help
# Defaults: MENDER_DEVICE_TYPE=mimxrt1170_evk, MENDER_BUILD_DIR=build-rt1170-evk,
#           MENDER_DEVICE_GROUP=rt1170-lab. Requires mender-pat-local.conf and zephyr.mender from build.

export MENDER_DEVICE_TYPE="${MENDER_DEVICE_TYPE:-mimxrt1170_evk}"
export MENDER_DEVICE_GROUP="${MENDER_DEVICE_GROUP:-rt1170-lab}"
export MENDER_BUILD_DIR="${MENDER_BUILD_DIR:-build-rt1170-evk}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "${ROOT}/scripts/create-rt1180-deployment.sh" "$@"
