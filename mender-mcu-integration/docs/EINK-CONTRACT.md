# E-ink Zephyr contract

Canonical portable contract for Zephyr e-ink display + scheduler.
Summary also lives in [PROJECT-NOTES.md](../PROJECT-NOTES.md#e-ink-display-and-scheduler-zephyr).

## References

- Scheduler / HTTP: `/data_drive/esl/eink-scheduler-rust`
- Panel SPI: `/data_drive/esl/eink-spectra6` (MIT driver core; do not copy GPL image helpers)

## Packed frame (`ES6F` v1)

See PROJECT-NOTES. CRC32 is IEEE polynomial over the payload only.
Generator: `scripts/gen-eink-frame.py`.

## Display API boundary

Application code talks only to `eink_display_*` / Zephyr `display_*`.
Simulator uses SDL (ARGB8888 conversion from L_4). Hardware profiles:

| Profile | Backend | Geometry |
|---------|---------|----------|
| `native_sim` | SDL / `dummy_dc` | 1200×1600 (or landscape window) |
| EVK LCD lab (`APP_EINK_DISPLAY_LCD_PREVIEW`) | Rocktech RK055 MIPI via LCDIF | Scale ES6F → **720×1280** RGB565 |
| EVK / product EL133 | `eink,el133uf1` stream | **1200×1600** L_4 Spectra 6 |

LCD lab and EL133/`lpspi1` are **mutually exclusive** on the EVK (LCDIF vs
LPSPI1 pinmux). Product Active ESL board: EL133 + dual FlexSPI, **no** MIPI
LCD, SDRAM DNP — see PROJECT-NOTES and AESL-HW-RT1170-EINK-SPEC Rev 0.7.

Build helpers: `scripts/build-rt1170-evk-lcd.sh`, `scripts/build-rt1170-evk-eink.sh`.

## Scheduler pure core

`eink_scheduler_core_*` is side-effect free for ztest and a future `no_std` Rust swap.

## e-tabelone HTTP

Client: `src/eink/eink_http.c` (Zephyr sockets + `HTTP_CLIENT`, TLS via mbedTLS/PSA).

| Call | Path |
|------|------|
| Config | `GET {base}/node/v0/device/{id}/config` |
| Image | `GET` image URL (Bearer omitted for `*.amazonaws.com`) |
| Telemetry | `POST {base}/node/v0/device/{id}/telemetry` |
| Sync (v2) | `POST {base}/node/v2/device/{id}/sync` (opt-in; see below) |

### `/node/v2` radio-on-time sync (Cloudflare)

**Not MQTT.** Hard-gated WiFi: one TLS session, fewer RTTs, then radio off.

`POST /node/v2/device/{id}/sync` with Bearer device token:

- **Request:** v0-shaped `telemetry` + `schedule` acks, plus `gallery[]` of
  `{asset_id, content_sha256, byte_size}` for local LittleFS frames, plus optional
  `client.{max_download_bytes,supports_lz4,radio_budget_ms}`.
- **Response:** merged `schedule`/`images` (ES6F.lz4 only), `download[]` of assets
  whose hash does not match gallery, `sync_now`, `noop`, `radio_budget_ms`.
- Asset bytes still via `GET …/node/v0/.../assets/{id}.es6f.lz4` (pipelined).
- `/node/v0` remains frozen for AWS dual-run; see etablone-cloud
  `docs/BOARD-API-COMPAT.md`.

Firmware: `CONFIG_APP_EINK_HTTP_V2_SYNC` (Zephyr). When enabled, HTTPS bases
use v2; `file://` fixtures keep the v0 path. Acceptance metric is
**joules / radio-on time per wake** vs v0 config+telemetry+GETs (native_sim
proxies with `prof: sync … (v2)` wall time).

Telemetry JSON (`telemetry` object) includes at least:

| Field | Notes |
|-------|--------|
| `battery_capacity` | Integer percent, or `-1` when unknown |
| `next_wakeup_date` | ISO-8601 UTC |
| `current_displayed_job_id` | Optional string when known |
| `screen_type` | Optional; portal Type — only `"13in"` or `"25in"` (`CONFIG_APP_EINK_SCREEN_TYPE`; omit when empty). With `APP_EINK_PANEL_AUTODETECT`, a successful probe overrides (EL133→`13in`, T2000→`25in`). |
| `latitude` / `longitude` | Optional WGS84 degrees; **omit** when no fix |
| `location_accuracy_m` | Optional metres; omit when unknown |
| `storage_total_bytes` | Optional; LittleFS size for `APP_EINK_STORE_ROOT` (`fs_statvfs`) |
| `storage_free_bytes` | Optional; free bytes on that FS |
| `ram_heap_pool_bytes` | Optional; `CONFIG_HEAP_MEM_POOL_SIZE` |
| `ram_heap_free_bytes` | Optional; system-heap free (`SYS_HEAP_RUNTIME_STATS`) |
| `ram_heap_used_bytes` | Optional; system-heap allocated |
| `ram_heap_max_used_bytes` | Optional; high-water allocated since boot |

`CONFIG_APP_EINK_RESOURCE_TELEMETRY` (default y when HTTP is on) controls the
storage/RAM group. **Omit** a group when the probe fails — never send zeros as
placeholders. Cloud must treat these as additive optional keys (v0 freeze).

Top-level `schedule` is an array of `{ "job_id": "…" }` acks. Never send
null/zero placeholders for missing location — omit the keys entirely.

Lab / native_sim (`CONFIG_APP_EINK_LOCATION` default y):

```text
eink location
eink location set 53.4808 -2.2426 12.5
eink location clear
```

Persisted under `{APP_EINK_STORE_ROOT}/location.json`.

**GNSS (`CONFIG_APP_EINK_GNSS`):** Zephyr GNSS driver API → `eink_location_set()`
via `eink_gnss_*`. native_sim uses `zephyr,gnss-emul` (alias `gnss`). EVK lab
scaffold: `boards/mimxrt1170_evk_mimxrt1176_cm7_gnss.{conf,overlay}` with
`gnss-nmea-generic` on LPUART2 (disabled until loom/pins confirmed). Swap the
compatible to the BOM module when known (u-blox / Quectel / …). Manual shell
set/clear remains available for lab without sky view.

Device `GET …/config` may include `"sync_now": true` when an operator requested
force-sync; the cloud clears the flag after that response. Clients should treat
it as a hint to complete a full sync this wake (log/observe; optional freshness
invalidation). It is not a push wake.

### On-demand debug log upload

Additive Cloudflare path (see [`docs/CLOUD-HANDOFF-DEBUG-LOG.md`](../../docs/CLOUD-HANDOFF-DEBUG-LOG.md)).
**Do not** put log text in `telemetry` / v2 sync JSON (v0 freeze).

| Piece | Contract |
|-------|----------|
| Config / v2 response | `"request_debug_log": true` (one-shot; cloud clears after successful upload) |
| Board upload | `POST {base}/node/v0/device/{id}/debug-log` Bearer; `text/plain` or gzip; **≤64 KiB** uncompressed |
| Capture | Circular RAM ring (**32 KiB** default, `APP_EINK_DEBUG_LOG_RING_SIZE`); Zephyr log backend |
| Trigger | Portal request **or** shell `eink log upload` — never every-wake by default |
| Privacy | Redact `Bearer …`, `token=`, `eink creds` lines (device best-effort + cloud) |

`CONFIG_APP_EINK_DEBUG_LOG_UPLOAD` (depends on HTTP). native_sim `file://` may
write `{store}/debug-log.txt` instead of HTTP.

Production accepts **ES6F** raw, or an **LZ4 frame** whose decompressed
payload is a complete ES6F v1 file (magic `04 22 4D 18`, same as `lz4 -f`).
JPEG/PNG remain rejected by magic. Expand timing is selectable:

| Kconfig | Store | When CPU/flash pay for expand |
|---------|--------|-------------------------------|
| `APP_EINK_LZ4_EXPAND_ON_DOWNLOAD` (default) | raw `.es6f` | During sync (often WiFi still relevant) |
| `APP_EINK_LZ4_EXPAND_ON_DISPLAY` | `.es6f.lz4` | During paint (`prof: lz4_materialize=…`) |

Overlay fragment: `eink-lz4-on-display.conf`. Helper: `scripts/eink-lz4-wrap.py`.

**Server / bridge:** Cloudflare Etablone defaults packed delivery to
`.es6f.lz4` (`delivery_format: es6f.lz4`); `?format=es6f` keeps raw.
The native_sim host bridge (`scripts/eink-etabelone-bridge.py`) defaults to
LZ4 (`--no-lz4` for raw). Both use the same `lz4 -f` frame contract.

**Power A/B (measure on real HW):** WiFi-on joules dominate MCU; panel/controller
during refresh also matters. Expand-on-download lengthens the radio/flash window
(write full ES6F while/after transfer). Expand-on-display shortens transfer and
lets you hard-gate WiFi sooner; paint pays LZ4 + scratch write with radio off.
Prefer on-display until HW joules say otherwise; switch to on-download if the
same frame is repainted often offline (amortize expand once).

**FRDM-IMXRT1186 bench (2026-08-22, expand-on-display, W25Q128 `/lfs1`):**
HTTP LZ4 write ~**115 KiB/s** (5.6 s / 658 KiB); LZ4→ES6F expand write
~**117 KiB/s** (8.0 s / 960 KiB); flash reads ~**5–7 MiB/s**. Full v2 sync +
paint wall ~**22 s** then BOM/sleep candidate. Detail + sleep implications:
[`POWER-HARDWARE-CONTRACT.md`](POWER-HARDWARE-CONTRACT.md#frdm-wake-window-bench--littlefs--lz4-flash-io-2026-08-22).
Shell: `eink flash_bench <path> [write]`; logs `prof: flash_read|write=…`.

Defaults: base
`https://etablone.dynamicdevices.co.uk` (Cloudflare). Legacy AWS
`https://api.dev.e-tabelone.com` remains valid during dual-run — set via
`eink creds`. Sync **off** until credentials + `CONFIG_APP_EINK_HTTP_ENABLE`
or shell `eink creds` / `eink sync`. **Device id SoT:** uppercase SoC UID hex
(`hwinfo` / `soc_uid_get_hex`); leave `CONFIG_APP_EINK_HTTP_DEVICE_ID` empty
in product builds. Auth remains Bearer `device_token`. With
`CONFIG_APP_EINK_HTTP_CREDS_PERSIST` (default), `eink creds` writes
`APP_EINK_STORE_ROOT/creds.json` (LittleFS; plaintext) and reloads at boot —
clear with token `none`/`-`. Cutover notes:
`/data_drive/esl/etablone-cloud/docs/CUTOVER.md`.

S3 pre-signed downloads (AWS gallery): omit default `:443` from the HTTP
`Host` header (Zephyr otherwise breaks AWS signatures). JPEG/PNG payloads are
rejected as soon as magic bytes are seen; use the host bridge for AWS JPEG/PNG
or talk to Cloudflare which already serves ES6F (optionally LZ4-framed).

The sync downloads the **currently due image first**, then caches remaining
gallery ES6F assets while the radio is up (skipping frames that already
validate in the store). It displays the due job, then posts telemetry with a
**schedule-driven** `next_wakeup` (earliest future cron vs poll deadline).
Previously cached frames remain available for offline display wakes.

### native_sim live e-tabelone

The development API currently returns JPEG/PNG gallery assets. Keep that
translation out of production firmware: the host bridge fetches the real
config/schedule, converts the selected source asset to ES6F, and rewrites only
the simulator image URL.

```bash
./scripts/run-native-sim-etabelone.sh <device_id>
# visual 1200x1600 SDL window:
./scripts/run-native-sim-etabelone.sh <device_id> --sdl
```

This proves live config, schedule selection, image conversion/streaming,
display refresh, and upstream telemetry. `eink-native-sim*.conf` disables
Mender **autostart only** to isolate the focused e-tabelone run; normal
Mender integration and product profiles keep Mender enabled.

Local cleartext fixture (host TAP gateway): `http://192.0.2.2:8765` with an
HTTP/1.0 fixture server. `file://` fixtures remain supported.

Cron fields are **numeric `minute hour` only** (no `*`); fixture schedules
should use an overdue wall time such as `0 0 * * *`.

Verify gate: `./scripts/eink-verify-sim.sh` (selftest + `file://` sync/show).

TLS note (Zephyr 4.4 / Mbed TLS 4.x): enable the ECDHE-RSA AES-GCM ciphersuite
Kconfigs (pulls in `PSA_WANT_ALG_TLS12_PRF` / HMAC) and X25519
(`PSA_WANT_ECC_MONTGOMERY_255`) — see `prj.conf`. Without those, handshakes
fail as `-0x7F80`.

Credentials: Bitwarden / runtime only — never commit tokens.
