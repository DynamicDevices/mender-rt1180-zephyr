# Renode — FRDM e-ink UART smoke

Proof class: **`renode`**. Same ARM ELF as the FRDM-IMXRT1186 e-ink build.
Does **not** prove FRDM silicon, Spectra 6 waveforms, NETC/PHY, or live e-tabelone.

Portal + schedule TAP stays **`native_sim`**.

## Host TAP (Renode *can* bridge — this ELF cannot use it yet)

Renode’s host path is real: virtual switch + Linux TAP (`emulation CreateTap`,
`connector Connect host.tap switch`, NAT/DHCP on the host). That only moves
Ethernet frames if the **guest MAC is a modelled NIC** with a connector
(SMSC, Synopsys, ENC28J60, …).

FRDM-IMXRT1186 Mender/e-ink uses **NETC**. In our `.repl` those windows are
**Tags**, not a NIC. TAP would sit empty. F1 gemba: PHY/ENETC unmodelled.

| Path | Same flash ELF? | Hosted Mender / e-tabelone |
|------|-----------------|----------------------------|
| Renode TAP + NETC | yes | **No** until NETC+PHY are models |
| Renode TAP + a *different* modelled MAC | **no** (other DT/driver) | Possible, not this product path |
| `native_sim` TAP (`zeth` / `native-sim-zeth`) | **no** (host ELF) | **Yes** — already used for Hosted noop |

Do not claim Renode Hosted OTA on this FRDM ELF.

```bash
# after ./scripts/build-rt1186-frdm-eink.sh
./scripts/renode-frdm-eink-uart.sh
```

Needs portable Renode (`~/.local/opt/renode-portable`) and Python 3.12 +
robotframework (`~/tmp/renode-venv`). Copy `scripts/pydev/rt118x_*.py` into
`~/.local/opt/renode-portable/scripts/pydev/` (the `.repl` `fileName:` is
relative to the Renode install).

VTOR is derived per ELF (`scripts/find-vtor.py`); MCUboot slot0 is not
`0x14000000`.

## Status (2026-08-19)

- Hello_world on this `.repl` + Python CCM/ANATOP: **PASS** (F1 gemba).
- This e-ink/Mender ELF: Robot **PASS** — `Hello World! frdm_imxrt1186` on
  LPUART1 (~6 s; immediate-log image, shell prompt follows). Extra models vs
  hello: BLK_CTRL_WAKEUPMIX + NETC PRIV/IERB RAM,
  TRDC HWCFG0/DACFG (SDK `assert` on NMSTR/NCM), FlexSPI STS0 idle +
  self-clearing `MCR0.SWRESET` (firmware spins until that bit reads 0).
- Still **not** modelled: NETC/PHY, Spectra 6, real FlexSPI flash contents.
  Proof class remains `renode` UART only.
