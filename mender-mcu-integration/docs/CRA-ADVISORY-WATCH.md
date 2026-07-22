# CRA advisory watch and triage (RT1170 Active ESL)

**Status:** Process stub (engineering)  
**Date:** 2026-07-22  
**Related:** [CRA-COMPLIANCE.md](CRA-COMPLIANCE.md), [CRA-ARTICLE-14-RUNBOOK.md](CRA-ARTICLE-14-RUNBOOK.md), [SECURITY.md](../../SECURITY.md)

> Not a public CVD policy. Replace the SECURITY.md placeholder contact before claiming a live intake channel.

## 1. What we watch

| Source | What to watch | Cadence |
|--------|---------------|---------|
| [Zephyr Security Advisories](https://docs.zephyrproject.org/latest/security/vulnerabilities.html) / CVE | Kernel, net, TLS, MCUboot | Weekly + on release pin bumps |
| NXP RT1170 / MCUXpresso / HAB errata | Silicon + HAB/CAAM guidance | Monthly + before HAB fuse campaigns |
| Mender MCU / Hosted Mender | Client auth, artifact handling | On version bumps + critical notices |
| Mbed TLS / TF-PSA-Crypto (Zephyr module) | TLS / PSA | With Zephyr upgrade reviews |
| Etablone Cloudflare Worker (Cloud API owner) | Board `/node/v0`·`/v2`, admin authz | Cloud lane; escalate device impact here |

Pin source of truth for this tree: `west.yml` revisions + `sbom/` SPDX from the last RC archive.

## 2. Triage → fix → deploy loop

```text
Advisory lands
  → classify: affects our pin / config? (Y/N)
  → severity: Critical / High / Medium / Low (CVSS + exploitability on ESL threat model)
  → ticket: GitHub issue + label security + cra
  → remediate: west bump / backport / Kconfig disable / cloud fix
  → prove: eink-verify-sim.sh (+ EVK OTA when HW available)
  → ship: Mender security deployment (separate from feature releases when possible)
  → disclose: fixed-vuln note per product policy (SECURITY.md / advisory page)
```

| Severity | Target response | Deploy |
|----------|-----------------|--------|
| Critical / actively exploited | Art. 14 path + emergency patch | Same day–48h once build proven |
| High | Patch in next security window | ≤14 days goal |
| Medium / Low | Schedule with pin upgrade | Next planned RC |

If **actively exploited** or **severe incident**: stop here and open [CRA-ARTICLE-14-RUNBOOK.md](CRA-ARTICLE-14-RUNBOOK.md) (≤24h early warning).

## 3. Ship gate (release candidate)

Before tagging an RC / fleet image:

- [ ] `scripts/generate-sbom.sh --rt1170-only` (or full matrix) succeeded
- [ ] `scripts/generate-sbom.sh --archive-rc` copied SPDX under `sbom/rc/<name>/`
- [ ] No open **Critical** advisories against the pinned Zephyr/MCUboot/Mender/MbedTLS set (or accepted residual risk recorded)
- [ ] Production MCUboot key used when `RELEASE_BUILD=1` (see [MCUBOOT-KEY-CEREMONY.md](MCUBOOT-KEY-CEREMONY.md))
- [ ] Lab-only fragments **not** in the ship EXTRA_CONF list (`*_shell*`, selftest-on-boot, demo PEM)

## 4. Owners (fill before Sep 2026)

| Role | Name / team | Notes |
|------|-------------|-------|
| Firmware triage | TBD | This tree |
| Cloud triage | Cloud API lane | Etablone |
| Art. 14 reporter | TBD | See Art. 14 runbook |
| Public contact | TBD | Replace SECURITY.md placeholder |

---

*Engineering process stub only — not legal advice.*
