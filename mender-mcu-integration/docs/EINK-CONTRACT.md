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
Simulator uses SDL (ARGB8888 conversion from L_4). Hardware uses out-of-tree
`eink,el133uf1` driver (dual CS GPIO, full-frame write, blanking-gated refresh).

## Scheduler pure core

`eink_scheduler_core_*` is side-effect free for ztest and a future `no_std` Rust swap.

## e-tabelone HTTP

Client: `src/eink/eink_http.c` (Zephyr sockets + `HTTP_CLIENT`, TLS via mbedTLS/PSA).

| Call | Path |
|------|------|
| Config | `GET {base}/node/v0/device/{id}/config` |
| Image | `GET` image URL (Bearer omitted for `*.amazonaws.com`) |
| Telemetry | `POST {base}/node/v0/device/{id}/telemetry` |

Telemetry JSON (`telemetry` object) includes at least:

| Field | Notes |
|-------|--------|
| `battery_capacity` | Integer percent, or `-1` when unknown |
| `next_wakeup_date` | ISO-8601 UTC |
| `current_displayed_job_id` | Optional string when known |
| `latitude` / `longitude` | Optional WGS84 degrees; **omit** when no fix |
| `location_accuracy_m` | Optional metres; omit when unknown |

Top-level `schedule` is an array of `{ "job_id": "…" }` acks. Never send
null/zero placeholders for missing location — omit the keys entirely.

Lab / native_sim (no GNSS hardware yet; `CONFIG_APP_EINK_LOCATION` default y):

```text
eink location
eink location set 53.4808 -2.2426 12.5
eink location clear
```

Persisted under `{APP_EINK_STORE_ROOT}/location.json`. A future GNSS driver
should call `eink_location_set()` / `eink_location_clear()` — do not invent
DTS/drivers here until the hardware path is confirmed.

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

Defaults: base
`https://etablone.dynamicdevices.co.uk` (Cloudflare). Legacy AWS
`https://api.dev.e-tabelone.com` remains valid during dual-run — set via
`eink creds`. Sync **off** until credentials + `CONFIG_APP_EINK_HTTP_ENABLE`
or shell `eink creds` / `eink sync`. Cutover notes:
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
