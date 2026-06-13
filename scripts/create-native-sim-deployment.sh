#!/usr/bin/env bash
# Create and deploy a minimal noop-update artifact for native_sim (Hosted Mender US).
# Reads PAT from mender-mcu-integration/mender-pat-local.conf (gitignored). No secrets in this file.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAT_FILE="${MENDER_PAT_FILE:-${ROOT}/mender-mcu-integration/mender-pat-local.conf}"
SERVER="${MENDER_SERVER_URL:-https://hosted.mender.io}"
MENDER_CLI="${MENDER_CLI:-${ROOT}/.tools/bin/mender-cli}"
export PATH="${ROOT}/.tools/bin:${PATH}"

DEVICE_ID="${MENDER_DEVICE_ID:-03fac8bb-567a-4008-b6d5-57efb522d1c3}"
DEVICE_TYPE="${MENDER_DEVICE_TYPE:-native_sim}"
ARTIFACT_NAME="${MENDER_ARTIFACT_NAME:-native-sim-noop-$(date +%Y%m%d-%H%M%S)}"
DEPLOY_NAME="${MENDER_DEPLOYMENT_NAME:-${ARTIFACT_NAME}}"
FORCE="${MENDER_FORCE_INSTALLATION:-true}"
PAYLOAD_SIZE="${MENDER_NOOP_PAYLOAD_BYTES:-256}"
DEVICE_GROUP="${MENDER_DEVICE_GROUP:-simulator}"
TARGET="${MENDER_DEPLOY_TARGET:-device}" # device | device_type | group


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

tmpdir="$(mktemp -d)"
cleanup() { rm -rf "${tmpdir}"; }
trap cleanup EXIT

payload="${tmpdir}/payload.bin"
artifact="${tmpdir}/${ARTIFACT_NAME}.mender"
dd if=/dev/urandom bs="${PAYLOAD_SIZE}" count=1 of="${payload}" status=none

mender-artifact write module-image \
  -o "${artifact}" \
  --artifact-name "${ARTIFACT_NAME}" \
  -T noop-update \
  -f "${payload}" \
  --compression none \
  -t "${DEVICE_TYPE}"

echo "Uploading artifact ${ARTIFACT_NAME} (type noop-update, device type ${DEVICE_TYPE})..."
"${MENDER_CLI}" artifacts upload \
  --server "${SERVER}" \
  --token-value "${MENDER_PAT}" \
  "${artifact}"

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
else
  echo "Invalid MENDER_DEPLOY_TARGET=${TARGET} (use device, device_type, or group)" >&2
  exit 1
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
