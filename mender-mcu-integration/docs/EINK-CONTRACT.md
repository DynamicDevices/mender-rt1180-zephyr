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

Accept **ES6F only** (reject JPEG/PNG by magic). Defaults: base
`https://api.dev.e-tabelone.com`, sync **off** until credentials +
`CONFIG_APP_EINK_HTTP_ENABLE` or shell `eink creds` / `eink sync`.

S3 pre-signed downloads: omit default `:443` from the HTTP `Host` header
(Zephyr otherwise breaks AWS signatures). JPEG/PNG payloads are rejected as
soon as magic bytes are seen.

### native_sim bring-up

```bash
./scripts/build-native-sim-eink.sh
# after DHCP on zeth0:
eink creds https://api.dev.e-tabelone.com <device_id> <token>
eink sync
```

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
