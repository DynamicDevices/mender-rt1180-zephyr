# RT1170 EVK / product HW bring-up checklist

**Status:** Blocked — **EVK / product hardware not received** (2026-07-22)  
**Tree:** `/data_drive/dd/mender` `feat/rt1170-evk`  
**Related:** [CRA-COMPLIANCE.md](CRA-COMPLIANCE.md), [MCUBOOT-KEY-CEREMONY.md](MCUBOOT-KEY-CEREMONY.md), [EINK-CONTRACT.md](EINK-CONTRACT.md), [boards/mimxrt1170_production.conf](../boards/mimxrt1170_production.conf)

Do **not** HAB-fuse the sole lab EVK. Use spare units for irreversible fuse experiments.

---

## 0. Unblock criteria

- [ ] MIMXRT1170-EVK (or product board) physically available + MCU-Link / debug probe
- [ ] Lab power, Ethernet or IW612 path, panel loom as needed for the profile under test
- [ ] MemPalace: clear `blocked-rt1170-evk-hardware` when kit arrives

Until then: native_sim + Cloudflare remain the acceptance path for sync/paint/v2/SoC UID.

---

## 1. First boot / identity (day 0)

| Step | Pass criteria |
|------|----------------|
| Flash lab image (`scripts/build-rt1170-evk*.sh` + `west flash`) | Console @ 115200; boot to shell or app log |
| `eink uid` / Mender log `{"soc_uid":…}` | Uppercase hex from OCOTP (`hwinfo_imxrt`); 16 hex typical on RT1170 |
| Etablone: create device with that `device_id` + Bearer | `eink creds` + `eink sync` paints (v2 preferred) |
| Hosted Mender: accept pending `soc_uid` device | Inventory shows App; no MAC-keyed identity |

---

## 2. Mender OTA + MCUboot rollback (WS1)

| Step | Pass criteria |
|------|----------------|
| Lab demo key OK for first swap only | Document key file used |
| Deploy `zephyr-image` update via Hosted Mender | Swap succeeds; new image boots |
| Force rollback / failed image | MCUboot reverts to previous slot |
| Repeat with **production** signing key (`RELEASE_BUILD=1`) | Rejects demo PEM; signed artifact installs |
| Archive SBOM for that RC | `generate-sbom.sh --rt1170-only --archive-rc <name>` |

---

## 3. Radio-on / joules A/B (power)

No joules meter required for a first **time proxy**; upgrade to energy when instrumented.

| Measurement | How | Goal |
|-------------|-----|------|
| Radio-on wall time | `prof: sync total=… (v2)` vs v0 (`CONFIG_APP_EINK_HTTP_V2_SYNC=n`) | v2 ≤ v0 control-plane RTTs (warm: `download=0`) |
| Expand-on-download vs display | `eink-lz4-on-display.conf` A/B | Pick default from WiFi-on joules (or time proxy) |
| Duty-cycle wake | `APP_EINK_BATTERY_DUTY_CYCLE` + skip-radio path | Offline paint without IW612 when fresh |

Record numbers in a dated note under this doc or lab notebook; link from CRA evidence table.

---

## 4. HAB / manufacturing (WS2 — spare units)

| Step | Pass criteria | Caution |
|------|----------------|---------|
| SPSDK / HAB closed-board procedure drafted | Matches NXP AN for RT1170 | Do not fuse sole EVK |
| SRK hash / CSF signed images | Boot with HAB enabled on **spare** | Irreversible SRK lock |
| Prod MCUboot key in ceremony | [MCUBOOT-KEY-CEREMONY.md](MCUBOOT-KEY-CEREMONY.md) | Offline / HSM |
| OTFAD / IEE (optional) | Only after HAB + recovery plan | Skip if not required |

---

## 5. Production profile on HW

| Step | Pass criteria |
|------|----------------|
| Build with [`mimxrt1170_production.conf`](../boards/mimxrt1170_production.conf) last in EXTRA_CONF | No UART shell; HTTP+v2; empty Kconfig tokens |
| Factory/claim provisions Bearer + confirms `soc_uid` | Sync works without `eink creds` shell |
| Serial recovery policy | Off, or entrance-gated only |

---

## 6. Sign-off

- [ ] native_sim CF cutover still green (regression)
- [ ] HW cutover: sync + paint with real SoC UID
- [ ] Mender OTA + rollback proven once
- [ ] Joules or time-proxy A/B recorded for v2 vs v0
- [ ] HAB left **unfused** on primary lab EVK unless spare used
- [ ] Update `etablone-cloud/docs/CUTOVER.md` board HW box (cloud lane) + diary `wing_zephyr-mender`

---

*Parked until hardware arrives. Native_sim work continues independently.*
