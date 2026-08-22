#!/usr/bin/env bash
# Restore FRDM lab credentials after a wipe — same pattern as portal Bearer in BWS.
#
# Portal: ETABLONE_FRDM_DEVICE_TOKEN → `eink creds` over serial (LittleFS persist).
# Mender: MENDER_HOSTED_US_PAT → accept pending auth set for MENDER_FRDM_DEVICE_ID
#         (device private key stays in on-device NVS; we never export it to BWS).
#
# Usage (West workspace root):
#   ./scripts/restore-frdm-lab-creds.sh              # portal + mender accept
#   ./scripts/restore-frdm-lab-creds.sh --portal-only
#   ./scripts/restore-frdm-lab-creds.sh --mender-only
#
# Env:
#   FRDM_SOC_UID     default B1EF425C3B305DADE90FBB2D10211000
#   ETABLONE_BASE    default https://etablone-dev.active-esl.com
#   CONSOLE_HOST/PORT  default 192.168.2.10 / 2325
#   MENDER_SERVER_URL  default https://hosted.mender.io
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESOLVE="${RESOLVE_SECRET:-$HOME/.cursor-secrets/bin/resolve-secret.sh}"
FRDM_SOC_UID="${FRDM_SOC_UID:-B1EF425C3B305DADE90FBB2D10211000}"
ETABLONE_BASE="${ETABLONE_BASE:-https://etablone-dev.active-esl.com}"
CONSOLE_HOST="${CONSOLE_HOST:-192.168.2.10}"
CONSOLE_PORT="${CONSOLE_PORT:-2325}"
MENDER_SERVER_URL="${MENDER_SERVER_URL:-https://hosted.mender.io}"

DO_PORTAL=1
DO_MENDER=1
for arg in "$@"; do
  case "$arg" in
    --portal-only) DO_MENDER=0 ;;
    --mender-only) DO_PORTAL=0 ;;
    -h|--help)
      sed -n '2,20p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $arg" >&2
      exit 1
      ;;
  esac
done

command -v "$RESOLVE" >/dev/null || {
  echo "missing resolve-secret.sh" >&2
  exit 1
}

mender_accept_pending() {
  local pat device_id
  pat="$("$RESOLVE" get MENDER_HOSTED_US_PAT)"
  device_id="$("$RESOLVE" get MENDER_FRDM_DEVICE_ID)"

  python3 - "$MENDER_SERVER_URL" "$pat" "$device_id" "$FRDM_SOC_UID" <<'PY'
import json, sys, urllib.request

base, pat, device_id, soc_uid = sys.argv[1:5]

def api(method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        base + path,
        data=data,
        method=method,
        headers={
            "Authorization": f"Bearer {pat}",
            "Accept": "application/json",
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        raw = resp.read()
        return resp.status, (json.loads(raw) if raw else None)

status, dev = api("GET", f"/api/management/v2/devauth/devices/{device_id}")
if status != 200 or not isinstance(dev, dict):
    raise SystemExit(f"devauth GET failed: {status}")

pending = [a for a in (dev.get("auth_sets") or []) if a.get("status") == "pending"]
accepted = [a for a in (dev.get("auth_sets") or []) if a.get("status") == "accepted"]

# Prefer pending whose identity matches this SoC.
def ident_ok(a):
    idata = a.get("identity_data") or {}
    return soc_uid in json.dumps(idata)

pending = [a for a in pending if ident_ok(a)] or pending
if not pending:
    if accepted:
        print(f"mender: no pending auth set (accepted={len(accepted)}) — nothing to do")
        sys.exit(0)
    raise SystemExit("mender: no pending auth set to accept")

new = pending[-1]  # newest last in API order we observed
# Free the single accepted slot if the plan blocks a second accept.
for old in accepted:
    st, _ = api(
        "PUT",
        f"/api/management/v2/devauth/devices/{device_id}/auth/{old['id']}/status",
        {"status": "rejected"},
    )
    print(f"mender: rejected stale accepted auth {old['id']} (HTTP {st})")

st, _ = api(
    "PUT",
    f"/api/management/v2/devauth/devices/{device_id}/auth/{new['id']}/status",
    {"status": "accepted"},
)
if st not in (200, 204):
    raise SystemExit(f"mender: accept failed HTTP {st}")
print(f"mender: accepted auth set {new['id']}")
PY
}

portal_creds_serial() {
  local token
  token="$("$RESOLVE" get ETABLONE_FRDM_DEVICE_TOKEN)"
  python3 - "$CONSOLE_HOST" "$CONSOLE_PORT" "$ETABLONE_BASE" "$FRDM_SOC_UID" "$token" <<'PY'
import select, socket, sys, time

host, port_s, base, uid, token = sys.argv[1:6]
port = int(port_s)
cmd = f"eink creds {base} {uid} {token}\r\n".encode()

def redact(b: bytes) -> bytes:
    return b.replace(token.encode(), b"[REDACTED_TOKEN]")

s = socket.create_connection((host, port), timeout=5)
s.setblocking(False)
time.sleep(0.2)
while select.select([s], [], [], 0.1)[0]:
    s.recv(8192)

s.sendall(cmd)
end = time.time() + 6
buf = b""
while time.time() < end:
    r, _, _ = select.select([s], [], [], 0.3)
    if not r:
        continue
    chunk = s.recv(4096)
    if not chunk:
        break
    buf += chunk
    sys.stdout.buffer.write(redact(chunk))
    sys.stdout.buffer.flush()
s.close()
if b"credentials updated" not in buf and b"creds saved" not in buf:
    raise SystemExit("portal: creds restore did not confirm on console")
print("\nportal: creds restored over serial (token not logged)")
PY
}

if [[ "$DO_MENDER" == 1 ]]; then
  echo "== Mender: accept pending auth (BWS PAT + MENDER_FRDM_DEVICE_ID) =="
  mender_accept_pending
fi
if [[ "$DO_PORTAL" == 1 ]]; then
  echo "== Portal: eink creds via serial (BWS ETABLONE_FRDM_DEVICE_TOKEN) =="
  portal_creds_serial
fi

echo "OK: lab restore complete"
echo "Tip: app-only flash without chip erase keeps NVS (Mender) + LittleFS (portal)."
