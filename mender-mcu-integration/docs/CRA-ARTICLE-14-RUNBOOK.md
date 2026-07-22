# Article 14 — vulnerability / incident reporting runbook

**Status:** Draft (engineering)  
**Date:** 2026-07-22  
**Applies from:** **11 September 2026** (verify against [Regulation (EU) 2024/2847](https://eur-lex.europa.eu/eli/reg/2024/2847/oj))  
**Product:** Active ESL (RT1170 firmware + Etablone cloud)  
**Parent:** [CRA-COMPLIANCE.md](CRA-COMPLIANCE.md)

> **Not legal advice.** Confirm CSIRT routing, severity definitions, and filing
> channels with counsel before the duty applies. Use the EU single reporting
> platform once available; until then record drafts locally and escalate via
> the contacts below.

---

## 1. Roles (fill before Sep 2026)

| Role | Name / team | Contact |
|------|-------------|---------|
| **Incident commander** | TBD | TBD |
| **Firmware lead** | TBD | TBD |
| **Cloud / Etablone lead** | TBD | TBD |
| **Legal / regulatory** | TBD | TBD |
| **Public vuln contact** | (see SECURITY.md) | `security@dynamicdevices.co.uk` *(placeholder)* |
| **Coordinating CSIRT** | TBD (member state) | TBD |
| **ENISA filing** | via single platform | TBD |

---

## 2. What triggers this runbook

| Trigger | Definition (working) | Clock starts when |
|---------|----------------------|-------------------|
| **Actively exploited vulnerability** | Credible evidence of exploitation in the wild against our PDE (device, firmware, cloud path in intended use) | Manufacturer **becomes aware** |
| **Severe incident** affecting product security | Incident with significant impact on confidentiality, integrity, or availability of the product or users | Manufacturer **becomes aware** |

Awareness sources: customer report, SECURITY.md inbox, Zephyr/NXP/Mender advisory mapped to our SBOM, internal detection, cloud WAF/auth anomalies.

---

## 3. Timelines (Article 14)

| Step | Actively exploited vuln | Severe incident |
|------|-------------------------|-----------------|
| Early warning | ≤ **24 hours** | ≤ **24 hours** |
| Notification | ≤ **72 hours** | ≤ **72 hours** |
| Final report | ≤ **14 days** after corrective/mitigating measure | ≤ **1 month** |

Notify the **coordinating CSIRT** and **ENISA** via the single reporting platform.

---

## 4. Immediate actions (first 24 hours)

1. **Open incident** — ticket ID, severity, products/firmware `artifact_name` / git SHA / SBOM stamp.
2. **Contain** — Mender: pause risky deployments; Etablone: revoke tokens / disable endpoints if needed; do **not** log secrets.
3. **Classify** — exploited vuln vs severe incident vs ordinary CVE (ordinary → WS3 triage, not Art. 14).
4. **Early warning** — submit within 24h (even if incomplete): product ID, nature, awareness time, first mitigation if any.
5. **Notify counsel** — legal/regulatory owner.

---

## 5. 72-hour notification pack

- Affected SKUs / firmware versions / cloud versions (from SBOM + Mender inventory)
- Impact summary (CIA), exploitability, known exposure
- Mitigation / workaround / update availability
- Contact point for authorities
- Whether personal data is involved → also follow UK GDPR / ICO process (separate)

---

## 6. Remediation and final report

1. Fix in firmware and/or cloud; build signed image with **non-demo** MCUboot key.
2. Archive SPDX via `./scripts/build-rt1170-evk*.sh` (auto SBOM) or `generate-sbom.sh --rt1170-only`.
3. Deploy security update via Mender to affected `device_type` / groups; document rollout.
4. Public disclosure of **fixed** vulnerability per CVD policy (SECURITY.md).
5. Final Art. 14 report within the statutory window.

Upstream (Art. 13(6)): report/fix-share to Zephyr/NXP/Mender as appropriate.

---

## 7. Decision tree (short)

```text
New security signal
  ├─ Maps to our SBOM / fleet? ─ no → watch / close
  └─ yes
       ├─ Active exploit or severe product incident? ─ yes → THIS RUNBOOK (24h clock)
       └─ no → WS3 triage → optional security deploy
```

---

## 8. Evidence to keep

- Awareness timestamp and source  
- Early warning / notification / final report copies  
- SBOM + `artifact_name` for affected and fixed builds  
- Mender deployment IDs and serial logs (redacted)  
- Post-incident review notes  

---

## 9. Drill

Schedule a **tabletop drill** before 11 Sep 2026 using a fictional Zephyr CVE affecting RT1170 TLS. Record gaps in this file.
