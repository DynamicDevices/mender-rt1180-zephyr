# Cloud handoff — resource telemetry on the portal

**From:** device lane (`zephyr-rt1186-eink`, branch `feat/frdm-gpc-wait`)  
**To:** Cloud API implementation (`/data_drive/esl/etablone-cloud`)  
**Date:** 2026-08-20  
**Alex ask:** “I want to be able to see screen metrics like this on the portal”

Device already emits optional fields (`a7f490d`):
`mender-mcu-integration` telemetry on v0 `POST …/telemetry` and v2 `POST …/sync`.

## Wire contract (additive; v0 freeze — do not require)

| Field | UI label (suggested) |
|-------|----------------------|
| `storage_total_bytes` | Flash total |
| `storage_free_bytes` | Flash free |
| `ram_heap_pool_bytes` | Heap pool |
| `ram_heap_used_bytes` | Heap used |
| `ram_heap_free_bytes` | Heap free |
| `ram_heap_max_used_bytes` | Heap peak |

Omit when absent (old boards / probe fail). Never invent zeros.

## Implement in etablone-cloud

### 1. Persist
- Migration: columns on device (or latest-telemetry) table matching the six fields
  (`INTEGER NULL`).
- `admin.ts` telemetry / v2 sync ingest: copy from `body.telemetry` into device
  row the same way as `battery_capacity` (~L2401+ / sync path ~L2506+).
- Device list API: return the fields on each device JSON (alongside
  `battery_capacity`).

### 2. OpenAPI
- Add optional integer properties to `TelemetryRequest.telemetry` and
  `DeviceSyncRequest.telemetry` in `openapi.yaml`.
- Note in `docs/BOARD-API-COMPAT.md`: additive optional keys only.

### 3. Portal UI (`src/app_ui.ts`) — Alex wants this visible

**Screens table** (`renderDevicesTable`, ~L2581): today columns are
Screen | Health | Battery | Next change | Location.

- Add column **Flash** after Battery: `formatBytes(free) + " / " + formatBytes(total)`
  (reuse existing `formatBytes` used for assets ~L3048). Show `—` if either null.
- Add column **Heap** (or combine under a **Resources** header on wide layouts):
  `used / peak / pool` via `formatBytes`, or compact `used/pool` with peak as
  `title=` tooltip. Show `—` if null.
- Optional health tint: if `storage_free_bytes` &lt; 10% of total, or
  `ram_heap_used_bytes` &gt; 85% of pool, use a warning chip (same vocabulary as
  stale sync).

**Device settings / detail** (Settings button → modal): add a small
**Resources** block with the six values + last-seen, so deep dive matches the
table.

**List density:** keep Flash/Heap as compact muted text (12px), similar to the
device id under the screen name — do not dominate Battery.

### 4. Tests
- `test/api.test.ts`: post telemetry with the new fields → `GET` devices
  includes them; omit fields → nulls, still 200.
- Freeze test for v0 wire shape: still accept payloads **without** the fields.

### 5. Optional follow-ups (not blocking UI)
- Alert when flash free or heap free is low (`device_stale`-style email).
- Cap v2 `download[]` using `storage_free_bytes` + `gallery[].byte_size`.

## Out of scope
- Device firmware changes (done).
- Mender inventory attributes.
- Editing this tree from the device lane — **you** own `etablone-cloud`.

## Verify
1. native_sim or board with HTTP posts telemetry including the fields.
2. Portal Screens list shows Flash + Heap for that device.
3. Legacy device without fields still shows `—` / Battery only.
