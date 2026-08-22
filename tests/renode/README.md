# Renode — FRDM e-ink UART / PC-path smoke

Proof class: **`renode`**. Same ARM ELF as the FRDM-IMXRT1186 e-ink build.
Does **not** prove FRDM silicon, Spectra 6 waveforms, NETC/PHY, live e-tabelone,
or µA sleep current.

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

## Scripts

```bash
# after ./scripts/build-rt1186-frdm-eink.sh
./scripts/renode-frdm-eink-uart.sh          # LPUART Robot (Booting + Hello)

# BOM_POWER_LOOP lab image
BOM_POWER_LOOP=1 ./scripts/build-rt1186-frdm-eink.sh
./scripts/renode-frdm-eink-bom-pc-smoke.sh  # PC-path: BBNSM RTC_EN after settle
```

Needs portable Renode (`~/.local/opt/renode-portable`) and Python 3.12 +
robotframework (`~/tmp/renode-venv`). Copy `scripts/pydev/rt118x_*.py` into
`~/.local/opt/renode-portable/scripts/pydev/` (the `.repl` `fileName:` is
relative to the Renode install).

VTOR is derived per ELF (`scripts/find-vtor.py`); MCUboot slot0 is not
`0x14000000`.

Wrap UART Robot with a wall-clock `timeout` when testing BOM images: after the
5 s settle the guest hits GPC STOP + WFI and virtual-time Waits can hang the
suite.

## Status (2026-08-20)

- Hello_world on this `.repl` + Python CCM/ANATOP: **PASS** (F1 gemba).
- E-ink/Mender ELF **LPUART Robot**: currently **FAIL** — guest runs (GPIO /
  BBNSM side effects visible) but Terminal Tester sees no
  `Booting Zephyr OS` / `Hello World!` / `BOM power-loop` text. Open issue;
  do not claim UART PASS for this ELF.
- E-ink/Mender **BOM_POWER_LOOP** ELF **PC-path**: **PASS** via
  `renode-frdm-eink-bom-pc-smoke.sh` — after ~5 s settle the log shows BBNSM
  CTRL `0x54440008` write `0x2` and the sticky-read spin from
  `bbnsm_rtc_start()` (`eink_power.c`). That is enough to say the BOM loop
  entered `enter_snvs`; it is **not** Spectra 6, FRDM µA, or Hosted OTA.
- Still **not** modelled: NETC/PHY, Spectra 6, real FlexSPI flash contents,
  BBNSM alarm wake from WFI.
