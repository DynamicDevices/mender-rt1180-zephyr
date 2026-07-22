# CRA compliance — Active ESL / RT1170 firmware baseline

**Status:** Engineering gap analysis (draft)  
**Date:** 2026-07-22  
**Product line:** Active ESL (13.3" Spectra 6 / EL133) on **i.MX RT1170**  
**Firmware tree:** [`/data_drive/dd/mender`](file:///data_drive/dd/mender) (`feat/rt1170-evk`)  
**Related:** [PROJECT-NOTES — CRA mapping](../PROJECT-NOTES.md#cyber-resilience-act-cra--technical-mapping) (RT118x ELE programme), [SECURITY.md](../../SECURITY.md), [CRA-ARTICLE-14-RUNBOOK.md](CRA-ARTICLE-14-RUNBOOK.md), [CRA-ADVISORY-WATCH.md](CRA-ADVISORY-WATCH.md), [MCUBOOT-KEY-CEREMONY.md](MCUBOOT-KEY-CEREMONY.md)

> **Disclaimer:** This document is an **engineering gap analysis and programme plan**. It is **not legal advice**, **not a conformity assessment**, **not an EU Declaration of Conformity**, and **not CE / UKCA sign-off**. Product classification, support-period justification, Annex VII technical documentation, and Article 14 reporting require qualified legal/regulatory review before placing products on the EU or UK market.

---

## 1. Regulation and scope

| Item | Value |
|------|--------|
| Regulation | [Regulation (EU) 2024/2847](https://eur-lex.europa.eu/eli/reg/2024/2847/oj) (Cyber Resilience Act) |
| Manufacturer context | Active Edge Solutions Ltd / Dynamic Devices Ltd (England & Wales) — CRA applies if product is **placed on the EU market**; UK **PSTI** may also apply for UK market |
| PDE under review | Hardware + **Zephyr/MCUboot/Mender MCU firmware** + **Etablone cloud** + operator console (assessed as one product with digital elements) |
| In-scope firmware | RT1170 CM7 images from this workspace (EVK lab + Active ESL product profiles) |
| Out of scope (separate stacks) | Companion MPU Linux (e.g. Jaguar i.MX93), RT118x ELE roadmap (parallel product family) |

### Key dates (manufacturers — verify against official text)

| Date | Obligation |
|------|------------|
| **11 September 2026** | Vulnerability / severe-incident **reporting** (Article 14) |
| **11 December 2027** | Full **essential requirements** (Annex I) for products placed on the market from this date |

Official hubs: [Commission CRA](https://digital-strategy.ec.europa.eu/en/policies/cyber-resilience-act), [ENISA](https://www.enisa.europa.eu/), Zephyr [CRA guidance](https://docs.zephyrproject.org/latest/security/standards/cyber-resilience-act.html).

---

## 2. Classification and conformity route

| Field | Working assumption | Action |
|-------|-------------------|--------|
| Product class | **Default** (electronic shelf label / connected display) unless counsel finds Annex III/IV security-core function | Confirm with legal before Module choice |
| Conformity route (if default) | Likely **Module A** (internal control) after essential requirements met | Confirm; Important/Critical → third-party modules |
| Radio | Product includes **Wi-Fi/BT (e.g. IW612)** → **RED** cybersecurity for the *product*, not Ethernet-only MCU notes | Track radio module compliance separately |
| RT1170 vs ELE | RT1170 has **HAB + CAAM + OTFAD/IEE + SNVS + PUF** — **no EdgeLock Secure Enclave** | Do not claim ELE opaque keys on this SKU |

---

## 3. Annex I Part I — essential requirements (firmware slice)

Status: 🟢 adequate for lab / 🟡 needs work before ship / 🔴 blocking for production claims.

| Ref | Theme | Status | Current | Gap / fix |
|-----|-------|--------|---------|-----------|
| (a) | No known exploitable vulns at release | 🟡 | Zephyr v4.4.0 pin; no release CVE gate | SBOM + advisory watch + block ship on open criticals |
| (b) | Secure by default | 🟡 | MCUboot + TLS; shell on lab | Production: no demo keys; disable open debug/shell or gate; factory reset story |
| (c) | Security updates | 🟡 | Mender MCU + MCUboot swap designed | **Prove hardware OTA** on RT1170; document update SLO |
| (d) | Unauthorised access | 🟡 | Per-device Mender keypair; Etablone device tokens | Harden provisioning; cloud authz audit (Cloud API owner) |
| (e) | Confidentiality | 🟡 | TLS 1.2 to Mender/Etablone; **CAAM entropy on** | Prod MCUboot key; consider OTFAD for NOR; no secrets in logs |
| (f) | Integrity | 🟡 | MCUboot RSA image verify; ES6F CRC | **Replace demo PEM**; HAB for ROM root of trust (manufacturing) |
| (g) | Data minimisation | 🟡 | Telemetry fields per EINK contract | Review inventory/telemetry with privacy owner |
| (h)–(i) | Availability / no network harm | 🟡 | Bounded HTTP; duty-cycle wake | Document backoff; DoS posture on cloud |
| (j) | Attack surface | 🟡 | Lab shell / serial; [production sketch](../boards/mimxrt1170_production.conf) | Validate production fragment on HW; serial recovery entrance-gated only |
| (k) | Incident impact reduction | 🟡 | MCUboot rollback | Confirm image; staged Mender groups |
| (l) | Security logging | 🟡 | ISO8601 logs | Define security-event set; never log tokens |
| (m) | Secure data removal | 🔴 | Not defined | Device wipe / de-provision + LittleFS erase procedure |

---

## 4. Annex I Part II — vulnerability handling

| Requirement | Status | Current | Fix |
|-------------|--------|---------|-----|
| SBOM (machine-readable) | 🟡 | `scripts/generate-sbom.sh` + `--archive-rc` | Run per RC; keep under `sbom/rc/` |
| Remediate without delay; security updates separable | 🟡 | Mender deployments; [CRA-ADVISORY-WATCH.md](CRA-ADVISORY-WATCH.md) | Process: CVE → pin → build → deploy; feature vs security channels |
| Regular security testing | 🔴 | Ad-hoc selftest / verify-sim | `hardenconfig`; periodic bench; dependency scan in CI |
| Public disclosure of fixed vulns | 🔴 | — | Product advisory process + SECURITY.md contact live |
| CVD policy | 🟡 | [SECURITY.md](../../SECURITY.md) placeholder | Publish contact; response SLAs |
| Secure update distribution | 🟡 | Mender Hosted TLS | Hardware proof (WS1) |
| Free / timely dissemination | 🟡 | Org policy TBD | Document in support-period statement |

**Article 14 runbook (must exist before 11 Sep 2026):** early warning ≤24h, notification ≤72h, final report ≤14 days (actively exploited vuln) / ≤1 month (severe incident) to coordinating CSIRT + ENISA. Owner TBD — see PROJECT-NOTES WS4.

---

## 5. RT1170 security hardware enablement (WS2)

| Block | CRA role | Lab | Production |
|-------|----------|-----|------------|
| **CAAM entropy** | Credible RNG for TLS / keygen | **On** (`zephyr,entropy = &caam`, `CONFIG_ENTROPY_MCUX_CAAM`) — pinned in board conf | Keep; never ship timer RNG |
| **MCUboot signing** | Image integrity | Demo `root-rsa-2048.pem` | HSM / offline prod key |
| **HAB** | ROM secure boot | Off (do not fuse sole EVK) | Enable in manufacturing with SPSDK |
| **OTFAD / IEE** | Flash confidentiality | Off | Optional after HAB + key story |
| **SNVS** | Duty-cycle / secure RTC path | Software contract + stub | Wire rails on product board |
| **PUF** | HW-bound identity (no ELE) | Off | Optional if NVS keys insufficient |
| **ELE** | Opaque keystore | **N/A on RT1170** | Use RT118x SKU if required |

---

## 6. Documentation checklist (Annex VII / II — firmware slice)

- [ ] Product description + intended use (Active ESL)
- [ ] Architecture: MCUboot A/B + Mender + Etablone sync
- [ ] Risk assessment (device + Wi-Fi + cloud)
- [ ] SBOM per release artifact
- [ ] Vulnerability-handling process + Art. 14 runbook
- [ ] Test evidence (OTA swap, entropy, hardenconfig)
- [ ] User instructions: how updates apply; support end date; vuln contact
- [ ] Support period statement (≥5 years unless justified shorter)

---

## 7. UK PSTI (if UK market)

Already binding for many internet-connectable consumer products:

1. No universal default passwords  
2. Published vulnerability disclosure / contact  
3. Stated minimum security-update period  

Align CRA work with PSTI; confirm B2B industrial exemptions with counsel if claimed.

---

## 8. Prioritised action list

### Done / in tree

- [x] MCUboot sysbuild + swap-using-move  
- [x] Mender MCU Hosted path (`native_sim` Phase 0b)  
- [x] **RT1170 CAAM entropy** (SoC DTS + build; board conf pin)  
- [x] SECURITY.md contact placeholder  
- [x] This gap document  
- [x] `generate-sbom.sh --rt1170` + build script `west spdx --init` + `CONFIG_BUILD_OUTPUT_META=y`  

### Before 11 Sep 2026 (Art. 14)

- [x] Art. 14 runbook draft ([CRA-ARTICLE-14-RUNBOOK.md](CRA-ARTICLE-14-RUNBOOK.md)) — **owners/CSIRT still TBD**  
- [x] Advisory watch + triage stub ([CRA-ADVISORY-WATCH.md](CRA-ADVISORY-WATCH.md)) — weekly cadence; owners TBD  
- [ ] Publish live vuln contact (replace SECURITY.md placeholder)  
- [x] SBOM RC archive convention (`generate-sbom.sh --archive-rc` → `sbom/rc/<name>/`) — run per release candidate  

### Before production / Dec 2027 claims

- [ ] Hardware-proven RT1170 Mender OTA + MCUboot rollback  
- [x] Key ceremony procedure ([MCUBOOT-KEY-CEREMONY.md](MCUBOOT-KEY-CEREMONY.md)); `MCUBOOT_SIGNATURE_KEY_FILE` + `RELEASE_BUILD=1` gate — **prod PEM still to be generated offline**  
- [x] Production profile sketch ([boards/mimxrt1170_production.conf](../boards/mimxrt1170_production.conf)) — no lab shell; validate on product HW  
- [ ] HAB manufacturing flow (spare units only for fuse experiments)  
- [ ] Device identity hardening beyond plaintext NVS (or accepted residual risk)  
- [ ] Support period + risk register signed  
- [ ] Cloud Etablone CRA slice (Cloud API owner)  
- [ ] Legal classification + Module A / third-party decision  

### Explicitly deferred / avoid on sole lab EVK

- HAB SRK fuse lock  
- OTFAD keyblobs without recovery plan  
- Claiming ELE / EdgeLock 2GO on RT1170  

---

## 9. Workstream map

| WS | Focus | Near-term owner track |
|----|--------|------------------------|
| **WS1** | OTA / MCUboot / Mender | RT1170 Phase 1–2 bench |
| **WS2** | CAAM → HAB → optional OTFAD/PUF | Entropy **done**; keys + HAB next |
| **WS3** | SBOM + vuln triage | Extend `generate-sbom.sh`; process before Sep 2026 |
| **WS4** | Support period, Art. 14, technical file | Org / legal |
| **Cloud** | Etablone TLS, authz, CVD | Cloud API implementation |

RT118x ELE S1–S4 remains a **parallel** programme in PROJECT-NOTES for SKUs that ship that silicon.

---

## 10. Evidence pointers

| Evidence | Location |
|----------|----------|
| CAAM chosen entropy | `zephyr/dts/arm/nxp/imxrt/nxp_rt11xx_cm7.dtsi` (`zephyr,entropy = &caam`) |
| Board pin (no timer RNG) | `boards/mimxrt1170_evk_mimxrt1176_cm7.conf` |
| Build proof | `CONFIG_ENTROPY_MCUX_CAAM=y`, `# CONFIG_TEST_RANDOM_GENERATOR is not set` in RT1170 `.config` |
| SBOM script | `scripts/generate-sbom.sh` (`--rt1170`); build scripts call `west spdx --init` automatically. Evidence: `sbom/rt1170-dev-1-*/{app,mcuboot}/*.spdx` (2026-07-22). Needs Python `reuse` + `.tools/bin` on PATH. |
| Vuln contact | [SECURITY.md](../../SECURITY.md) |

---

*End of engineering gap analysis. Re-verify CRA dates and Annex wording against EUR-Lex before conformity claims.*
