# Cloud handoff — on-demand device debug log upload

**From:** device lane (`zephyr-rt1186-eink`, branch `feat/frdm-gpc-wait`)  
**To:** Cloud API implementation (`/data_drive/esl/etablone-cloud`)  
**Date:** 2026-08-22  
**Portal:** `https://etablone-dev.active-esl.com`  
**Alex ask:** upload debugging logs from an imxrt screen to the portal  
**MCQ sign-off:** on-demand only; 32 KiB ring / POST ≤64 KiB; separate POST→R2; cloud API/UI before firmware upload path.

## Goal

Operators **request** a recent debug log; the board uploads a capped text snapshot on the next wake; the portal can **download/view** the last log. Do **not** put log bodies in frozen telemetry JSON.

## Wire contract (additive; v0 freeze)

### Config / v2 sync response

```json
{ "request_debug_log": true }
```

One-shot, like `sync_now`. Cloud clears the flag after a successful board upload (`201`).

### Board upload

```http
POST /node/v0/device/{deviceId}/debug-log
Authorization: Bearer <device_token>
Content-Type: text/plain; charset=utf-8
Content-Encoding: gzip
X-Etablone-Log-Boot-Id: <optional>
X-Etablone-Log-Lines: <optional count>

<body ≤ 65536 bytes uncompressed>
```

- `Content-Encoding: gzip` optional; plain UTF-8 text also accepted.
- Response `201`: `{ "id": "…", "bytes": N, "stored_at": "…" }` (`bytes` = stored object size or uncompressed length — document which; prefer uncompressed).
- Errors: `401`, `413` oversize, `429` rate limit (recommend 1 upload / 5 min / device).

### Admin (portal)

| Method | Path | Role |
|--------|------|------|
| `POST` | `/admin/v1/orgs/{orgId}/devices/{deviceId}/debug-log/request` | Set `request_debug_log` (audit `device.debug_log_request`) |
| `GET` | `/admin/v1/orgs/{orgId}/devices/{deviceId}/debug-log/latest` | Stream body or short-lived URL |

Device list/detail JSON metadata (no body): `debug_log_updated_at`, `debug_log_bytes`, `request_debug_log` (bool).

## Implement in etablone-cloud

### 1. Persist

Migration columns on `devices` (names flexible):

| Column | Type |
|--------|------|
| `request_debug_log` | INTEGER/BOOL default 0 |
| `debug_log_r2_key` | TEXT NULL |
| `debug_log_bytes` | INTEGER NULL |
| `debug_log_updated_at` | INTEGER NULL (ms epoch) |

R2 key: `debug-logs/{deviceId}/{iso}-{shortId}.txt.gz` (or `.txt` if uncompressed).

### 2. Board ingest

- Route `POST /node/v0/device/:id/debug-log`.
- Auth = device Bearer (same as telemetry).
- Cap uncompressed ≤65536; gzip inflate then measure.
- **Redact** before R2 put: `Bearer …`, `token=…`, lines containing `eink creds`.
- On success: put R2, update metadata, **clear** `request_debug_log`.
- Emit `request_debug_log` on `GET …/config` and v2 sync response while set (mirror `sync_now`).

### 3. OpenAPI + BOARD-API-COMPAT

- Document additive `request_debug_log` on config/sync.
- New board path + admin paths.
- Note: telemetry object stays metrics-only (no log field).

### 4. Portal UI (`app_ui.ts`)

Device settings / detail:

- **Request debug log** (write) → admin request endpoint.
- **Download last log** when `debug_log_updated_at` set.
- Show last size + time; `—` when never uploaded.
- Pending chip while `request_debug_log` true and no newer upload.

### 5. Tests

- Request → config shows `request_debug_log: true`.
- POST body ≤64 KiB → 201, flag cleared, GET latest returns text.
- POST >64 KiB uncompressed → 413.
- Planted `Bearer secret` in body → not present in stored object.
- v0 telemetry freeze unchanged (no log keys required).

## Device follow-up (this tree)

Circular log ring + `eink log upload` + honour `request_debug_log` on sync — see
[`EINK-CONTRACT.md`](../mender-mcu-integration/docs/EINK-CONTRACT.md#on-demand-debug-log-upload).

## Out of scope

- Always-on log shipping every poll  
- Crash dumps / coredump  
- Mender inventory as transport  
- Editing device firmware from the cloud lane  

## Verify

1. Admin request on FRDM / native_sim device → next config has flag.  
2. Board POST → portal Download shows recent `prof:` / `refresh done` lines.  
3. Redact check passes.
