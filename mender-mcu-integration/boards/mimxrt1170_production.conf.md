# RT1170 Active ESL — production profile sketch (no EVK required)

Fragment for **field / release** images. Lab EVK profiles keep shell + selftest.

**Status:** Sketch — cannot fully validate until product HW arrives.  
**Use:** pass as an EXTRA conf after the board base + e-ink product conf, e.g.

```bash
# Illustrative — wire into build-rt1170-*.sh when shipping
EXTRA_CONF_FILE="boards/mimxrt1170_evk_mimxrt1176_cm7.conf;boards/mimxrt1170_production.conf"
```

## Intent

| Lab (default today) | Production (this fragment) |
|---------------------|----------------------------|
| Zephyr shell / `eink` cmds | Shell **off** |
| Selftest at boot | Selftest **off** |
| HTTP sync off until `eink creds` | Sync **on** after provisioned NVS/settings (no Kconfig tokens) |
| Demo MCUboot PEM OK for lab | Reject demo key via `RELEASE_BUILD=1` (build script) |
| `/node/v0` or v2 | Prefer **v2** on Cloudflare |

## Still required outside this fragment

- Per-device Etablone Bearer + Mender identity (`soc_uid`) provisioned at factory / claim — not committed in Kconfig
- Production `MCUBOOT_SIGNATURE_KEY_FILE` / HSM ceremony
- HAB / fuse policy (manufacturing; spare units only for experiments)
- Serial recovery entrance-gated if ever re-enabled
