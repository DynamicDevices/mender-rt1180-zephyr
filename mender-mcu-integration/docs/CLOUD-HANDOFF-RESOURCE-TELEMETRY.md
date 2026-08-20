# Cloud handoff — resource telemetry (storage + RAM)

**From:** device lane (`zephyr-rt1186-eink` / FRDM Spectra worktree)  
**To:** Cloud API implementation (`etablone-cloud`)  
**Date:** 2026-08-20  
**Device commit intent:** firmware already emits optional fields on v0
`POST …/telemetry` and v2 `POST …/sync` `telemetry` object.

## Wire contract (additive; v0 freeze)

Optional numbers on `telemetry` (omit when unavailable — same as GNSS):

| Field | Meaning |
|-------|---------|
| `storage_total_bytes` | LittleFS total for device store root |
| `storage_free_bytes` | Free bytes on that FS |
| `ram_heap_pool_bytes` | Configured system heap pool size |
| `ram_heap_free_bytes` | Runtime free in system heap |
| `ram_heap_used_bytes` | Runtime allocated |
| `ram_heap_max_used_bytes` | High-water allocated since boot |

## Desired cloud actions

1. Extend OpenAPI `TelemetryRequest` / `DeviceSyncRequest.telemetry` with the
   fields above (optional integers).
2. Persist latest values on device row (migration) for portal UI / alerts.
3. Optional: warn when `storage_free_bytes` or heap free drops below a threshold;
   use `gallery[].byte_size` + free space to avoid queuing oversized `download[]`.
4. Do **not** require the fields (old boards omit them).
5. UI: show “Flash free / total” and “Heap used / max / pool” on device detail.

## Out of scope for this handoff

- Changing `download[]` planner (can follow once fields are stored).
- Mender inventory attributes (separate channel).
