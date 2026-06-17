#!/usr/bin/env bash
# Upload zephyr-image artifact (EVK or FRDM — set MENDER_DEVICE_TYPE / MENDER_BUILD_DIR) and create one Hosted Mender deployment.
# Requires Phase 0 sysbuild with CONFIG_MENDER_ARTIFACT_GENERATE=y (see PROJECT-NOTES build section).
# Reads PAT from mender-mcu-integration/mender-pat-local.conf (gitignored). No secrets in this file.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAT_FILE="${MENDER_PAT_FILE:-${ROOT}/mender-mcu-integration/mender-pat-local.conf}"
SERVER="${MENDER_SERVER_URL:-https://hosted.mender.io}"
MENDER_CLI="${MENDER_CLI:-${ROOT}/.tools/bin/mender-cli}"
export PATH="${ROOT}/.tools/bin:${PATH}"

BUILD_DIR="${MENDER_BUILD_DIR:-build-rt1180-evk}"
ARTIFACT_PATH="${MENDER_ARTIFACT_PATH:-${ROOT}/${BUILD_DIR}/mender-mcu-integration/zephyr/zephyr.mender}"
DEVICE_TYPE="${MENDER_DEVICE_TYPE:-mimxrt1180_evk}"
DEVICE_ID="${MENDER_DEVICE_ID:-}"
ARTIFACT_NAME="${MENDER_ARTIFACT_NAME:-}"
DEPLOY_NAME="${MENDER_DEPLOYMENT_NAME:-}"
FORCE="${MENDER_FORCE_INSTALLATION:-true}"
DEVICE_GROUP="${MENDER_DEVICE_GROUP:-rt1180-lab}"
TARGET="${MENDER_DEPLOY_TARGET:-group}" # device | device_type | group

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Upload build-rt1180-evk/mender-mcu-integration/zephyr/zephyr.mender (zephyr-image) to Hosted Mender US
and create one deployment. EVK hardware and an accepted device are required for OTA to succeed.

Options:
  -h, --help      Show this help

Environment:
  MENDER_ARTIFACT_PATH   Default: \${ROOT}/build-rt1180-evk/mender-mcu-integration/zephyr/zephyr.mender
  MENDER_BUILD_DIR       Default: build-rt1180-evk (FRDM: build-frdm-rt1186 via create-rt1186-frdm-deployment.sh)
  MENDER_DEVICE_TYPE     Default: mimxrt1180_evk (use frdm_imxrt1186 for FRDM build)
  MENDER_DEPLOY_TARGET   device | device_type | group (default: group)
  MENDER_DEVICE_GROUP    Default: rt1180-lab (EVK + FRDM CM33; separate device types, same lab group)
  MENDER_DEVICE_ID       Required when MENDER_DEPLOY_TARGET=device
  MENDER_ARTIFACT_NAME   Override artifact name (default: read from .mender via mender-artifact)
  MENDER_DEPLOYMENT_NAME Deployment name (default: same as artifact name)

Build artifact (from West workspace root):

  west build -d build-rt1180-evk -b mimxrt1180_evk/mimxrt1189/cm33 mender-mcu-integration \\
    --sysbuild -- -DCONFIG_MENDER_ARTIFACT_GENERATE=y

Examples:
  ./scripts/create-rt1180-deployment.sh
  ./scripts/create-rt1186-frdm-deployment.sh  # FRDM (build-frdm-rt1186/)
  MENDER_DEPLOY_TARGET=device MENDER_DEVICE_ID=<uuid> ./scripts/create-rt1180-deployment.sh
  MENDER_DEPLOY_TARGET=device_type ./scripts/create-rt1180-deployment.sh

See PROJECT-NOTES — Device groups — rt1180-lab.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${PAT_FILE}" ]]; then
  echo "Missing PAT file: ${PAT_FILE}" >&2
  exit 1
fi
MENDER_PAT="$(tr -d '\n\r' < "${PAT_FILE}")"

if [[ ! -x "${MENDER_CLI}" ]]; then
  echo "Missing mender-cli: ${MENDER_CLI}" >&2
  exit 1
fi
command -v mender-artifact >/dev/null || {
  echo "mender-artifact not on PATH (expected ${ROOT}/.tools/bin)" >&2
  exit 1
}
command -v jq >/dev/null || {
  echo "jq is required" >&2
  exit 1
}
command -v curl >/dev/null || {
  echo "curl is required" >&2
  exit 1
}

if [[ "${TARGET}" != "device" && "${TARGET}" != "device_type" && "${TARGET}" != "group" ]]; then
  echo "Invalid MENDER_DEPLOY_TARGET=${TARGET} (use device, device_type, or group)" >&2
  exit 1
fi

if [[ "${TARGET}" == "device" && -z "${DEVICE_ID}" ]]; then
  echo "MENDER_DEPLOY_TARGET=device requires MENDER_DEVICE_ID (accepted EVK device UUID)" >&2
  exit 1
fi

if [[ ! -f "${ARTIFACT_PATH}" ]]; then
  echo "Missing Mender artifact: ${ARTIFACT_PATH}" >&2
  echo "" >&2
  echo "Build the RT1180 sysbuild image with artifact generation enabled:" >&2
  echo "  west build -d ${BUILD_DIR} -b mimxrt1180_evk/mimxrt1189/cm33 mender-mcu-integration \\" >&2
  echo "    --sysbuild -- -DCONFIG_MENDER_ARTIFACT_GENERATE=y" >&2
  echo "" >&2
  echo "Then accept the device in Hosted Mender and assign it to group '${DEVICE_GROUP}' before group deploy." >&2
  exit 1
fi

if [[ -z "${ARTIFACT_NAME}" ]]; then
  ARTIFACT_NAME="$(mender-artifact read "${ARTIFACT_PATH}" 2>&1 | awk -F': ' '/^  Name: / {print $2; exit}')"
fi
if [[ -z "${ARTIFACT_NAME}" ]]; then
  echo "Could not read artifact name from ${ARTIFACT_PATH}" >&2
  exit 1
fi

if [[ -z "${DEPLOY_NAME}" ]]; then
  DEPLOY_NAME="${ARTIFACT_NAME}"
fi

compatible="$(mender-artifact read "${ARTIFACT_PATH}" 2>&1 | awk -F'[][]' '/Compatible types:/ {print $2; exit}')"
if [[ -n "${compatible}" && "${compatible}" != *"${DEVICE_TYPE}"* ]]; then
  echo "Artifact compatible types [${compatible}] do not include MENDER_DEVICE_TYPE=${DEVICE_TYPE}" >&2
  exit 1
fi

tmpdir="$(mktemp -d)"
cleanup() { rm -rf "${tmpdir}"; }
trap cleanup EXIT

echo "Uploading artifact ${ARTIFACT_NAME} (zephyr-image, device type ${DEVICE_TYPE}) from ${ARTIFACT_PATH}..."
"${MENDER_CLI}" artifacts upload \
  --server "${SERVER}" \
  --token-value "${MENDER_PAT}" \
  "${ARTIFACT_PATH}"

deploy_url="${SERVER}/api/management/v1/deployments/deployments"
if [[ "${TARGET}" == "device_type" ]]; then
  body="$(jq -n \
    --arg name "${DEPLOY_NAME}" \
    --arg artifact_name "${ARTIFACT_NAME}" \
    --arg device_type "${DEVICE_TYPE}" \
    --argjson force "$([[ "${FORCE}" == true ]] && echo true || echo false)" \
    '{
      name: $name,
      artifact_name: $artifact_name,
      force_installation: $force,
      filter: {
        terms: [
          {scope: "system", attribute: "device_type", type: "$eq", value: $device_type},
          {scope: "identity", attribute: "status", type: "$eq", value: "accepted"}
        ]
      }
    }')"
elif [[ "${TARGET}" == "group" ]]; then
  deploy_url="${SERVER}/api/management/v1/deployments/deployments/group/${DEVICE_GROUP}"
  body="$(jq -n \
    --arg name "${DEPLOY_NAME}" \
    --arg artifact_name "${ARTIFACT_NAME}" \
    --argjson force "$([[ "${FORCE}" == true ]] && echo true || echo false)" \
    '{
      name: $name,
      artifact_name: $artifact_name,
      force_installation: $force
    }')"
elif [[ "${TARGET}" == "device" ]]; then
  body="$(jq -n \
    --arg name "${DEPLOY_NAME}" \
    --arg artifact_name "${ARTIFACT_NAME}" \
    --arg device_id "${DEVICE_ID}" \
    --argjson force "$([[ "${FORCE}" == true ]] && echo true || echo false)" \
    '{
      name: $name,
      artifact_name: $artifact_name,
      force_installation: $force,
      devices: [$device_id]
    }')"
fi

headers="${tmpdir}/headers.txt"
curl -sS -D "${headers}" -o "${tmpdir}/deploy.json" -X POST \
  -H "Authorization: Bearer ${MENDER_PAT}" \
  -H "Content-Type: application/json" \
  -d "${body}" \
  "${deploy_url}"

deploy_id="$(jq -r '.id // empty' "${tmpdir}/deploy.json")"
if [[ -z "${deploy_id}" ]]; then
  deploy_id="$(awk -F/ '/^Location:/ {print $NF}' "${headers}" | tr -d '\r')"
fi

if [[ -z "${deploy_id}" ]]; then
  echo "Deployment create failed:" >&2
  cat "${tmpdir}/deploy.json" >&2
  exit 1
fi

echo "Deployment created:"
echo "  name:          ${DEPLOY_NAME}"
echo "  id:            ${deploy_id}"
echo "  artifact_name: ${ARTIFACT_NAME}"
echo "  target:        ${TARGET}$([[ "${TARGET}" == "group" ]] && echo " (${DEVICE_GROUP})")"
echo "  server:        ${SERVER}"
echo "Poll in UI or: curl -s -H \"Authorization: Bearer \$(cat ${PAT_FILE})\" \\"
echo "  ${SERVER}/api/management/v1/deployments/deployments/${deploy_id} | jq .status,.statistics"
