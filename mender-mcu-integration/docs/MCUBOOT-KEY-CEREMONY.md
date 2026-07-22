# MCUboot signing key ceremony (RT1170 / Active ESL)

**Status:** Procedure (engineering)  
**Date:** 2026-07-22  
**Parent:** [CRA-COMPLIANCE.md](CRA-COMPLIANCE.md)

Lab images still default to upstream demo `bootloader/mcuboot/root-rsa-2048.pem`.
**Production / `RELEASE_BUILD=1` must not use that key.**

---

## Roles

| Role | Duty |
|------|------|
| Key custodian | Generate, store, rotate; never commit private PEM to git |
| Build engineer | Point `MCUBOOT_SIGNATURE_KEY_FILE` at the custodian path |
| Release approver | Confirm `RELEASE_BUILD=1` CI rejects demo key |

---

## Generate a product key (offline / HSM preferred)

On an air-gapped or restricted host:

```bash
# Example RSA-2048 PEM (lab→pre-prod). Prefer HSM-backed keys for production.
./scripts/mcuboot-gen-signing-key.sh /secure/aesl/rt1170-mcuboot-rsa2048.pem
chmod 400 /secure/aesl/rt1170-mcuboot-rsa2048.pem
```

Record: key ID, algorithm, creation date, custodian, backup location (encrypted).
**Do not** store the private key in this repository, chat logs, or CI artifacts.

---

## Build with the product key

```bash
export MCUBOOT_SIGNATURE_KEY_FILE=/secure/aesl/rt1170-mcuboot-rsa2048.pem
export RELEASE_BUILD=1   # optional hard gate against demo PEM
./scripts/build-rt1170-evk-lcd.sh   # or -eink / plain build-rt1170-evk.sh
```

The build script passes the PEM to:

- `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE` (app → `zephyr.signed.bin`)
- `mcuboot_CONFIG_BOOT_SIGNATURE_KEY_FILE` (bootloader verify key)

Flash **MCUboot** built with that key, then the signed app. Devices already in
the field with the demo key need a controlled migration (re-provision bootloader
via SWD / serial recovery) before enforcing the product key.

---

## Checklist

- [ ] Private key never in git (`.gitignore` / secrets store)
- [ ] Public/ceremony record in technical file (Annex VII)
- [ ] CI `RELEASE_BUILD=1` job fails on demo PEM
- [ ] Spare lab EVK retained on demo key for bring-up
- [ ] Rotation procedure documented (new key + reflash bootloader)

---

## HAB note

MCUboot image signing ≠ HAB ROM secure boot. HAB SRK fusing is a separate
manufacturing step (SPSDK); do not experiment on the sole lab EVK.
