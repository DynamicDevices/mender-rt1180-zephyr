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

Production accepts **ES6F only** (reject JPEG/PNG by magic). Defaults: base
`https://api.dev.e-tabelone.com`, sync **off** until credentials +
`CONFIG_APP_EINK_HTTP_ENABLE` or shell `eink creds` / `eink sync`.

S3 pre-signed downloads: omit default `:443` from the HTTP `Host` header
(Zephyr otherwise breaks AWS signatures). JPEG/PNG payloads are rejected as
soon as magic bytes are seen.

The sync downloads the **currently due image first**, displays it, then posts
telemetry. It does not hold the radio/network open to download every gallery
asset. Previously cached frames remain available for offline display wakes.

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
