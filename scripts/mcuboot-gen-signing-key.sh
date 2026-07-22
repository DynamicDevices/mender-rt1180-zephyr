#!/usr/bin/env bash
# Generate an RSA-2048 PEM for MCUboot image signing (CRA key ceremony helper).
# Writes ONLY to the path you pass — never into the git tree by default.
set -euo pipefail
OUT="${1:-}"
if [[ -z "${OUT}" ]]; then
  echo "usage: $0 /absolute/or/secure/path/to/mcuboot-rsa2048.pem" >&2
  exit 2
fi
if [[ "${OUT}" == *"/bootloader/mcuboot/"* ]] || [[ "${OUT}" == *"/mender/"* && "${OUT}" != /tmp/* && "${OUT}" != /secure/* && "${OUT}" != "$HOME/"* ]]; then
  # Soft guard: discourage writing into the west workspace copy of demo keys.
  case "${OUT}" in
    */bootloader/mcuboot/root-rsa-2048.pem)
      echo "error: refusing to overwrite upstream demo key path" >&2
      exit 1
      ;;
  esac
fi
mkdir -p "$(dirname "${OUT}")"
if [[ -e "${OUT}" ]]; then
  echo "error: refuses to overwrite existing ${OUT}" >&2
  exit 1
fi
openssl genrsa -out "${OUT}" 2048
chmod 400 "${OUT}"
echo "OK: wrote ${OUT}"
echo "Build with: MCUBOOT_SIGNATURE_KEY_FILE=${OUT} ./scripts/build-rt1170-evk.sh"
