# FRDM-IMXRT1186 — T2000 USB host MVP

**Status:** software MVP (host + thin client). Hardware proof needs TCON on Type-C OTG.  
**Protocol SoT:** [DynamicDevices/eink-t2000-usb](https://github.com/DynamicDevices/eink-t2000-usb) (`/data_drive/esl/eink-t2000-usb`).

## Hardware

| Item | Notes |
|------|--------|
| FRDM connector | Type-C **USB OTG** (not MCU-Link debug USB / J23 serial) |
| Role | Host — FRDM must **source VBUS** to the T2000 board |
| Cable | Host-capable Type-C / adapter; confirm VBUS before chasing software |
| Device IDs | VID `0x3558`, PID `0x2002` (or `0x4002` main) |
| Panel | 25.3″ Spectra 6 via T2000 Mini-LVDS TCON |

Linux Gemba (same TCON on a PC):

```bash
lsusb | grep -i 3558
/data_drive/esl/eink-t2000-usb/build/bin/t2000_usb   # interactive / -i image
```

Phase 0 note (2026-08-21): no `3558` device was present on the build host USB at implement time.

## Build

```bash
cd /data_drive/dd/zephyr-rt1170-eink-spectra6-frdm
FRDM_T2000=1 ./scripts/build-rt1186-frdm-eink.sh
```

Adds:

- HyperRAM profile (`eink_hyperram.conf`)
- [`boards/frdm_imxrt1186_mimxrt1186_cm33_eink_t2000.conf`](../mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_eink_t2000.conf)
- [`boards/frdm_imxrt1186_mimxrt1186_cm33_eink_t2000.overlay`](../mender-mcu-integration/boards/frdm_imxrt1186_mimxrt1186_cm33_eink_t2000.overlay) — `zephyr_uhc0` / `usbh1` EHCI host + PHY

Do **not** combine with `FRDM_ENROLL_OCRAM=1`. UDC stays disabled on the same controller.

## Shell

After flash + plug T2000:

```text
usbh ...                    # optional Zephyr host shell smoke
eink t2000 info             # W/H, panel_id, TCON/LUT
eink t2000 clear            # clear + wait idle
eink t2000 fill [idx] [wf]  # solid Y8 fill (default idx=0 wf=0)
```

Wire protocol is vendor bulk (`0xF1` / `0xF2`), not MSC BOT for paint.

## Code map

| File | Role |
|------|------|
| `src/eink/eink_t2000_usb.c` | USBHS clocks, USBH init, class probe, bulk IN/OUT |
| `src/eink/eink_t2000_proto.c` | info / clear / mode / Display stripe fill / status |
| `src/eink/eink_t2000.h` | Public API |

## Proof class

| Gate | Class | 2026-08-21 |
|------|--------|------------|
| `lsusb` + Linux `t2000_usb` info | Linux Gemba | **Blocked** — no VID `3558` on build host |
| UART: USB host attach VID 3558 | `FRDM` | **Blocked** — no LinkServer probe / MCU-Link |
| `eink t2000 info` matches Linux W/H | `FRDM` | Pending silicon |
| `eink t2000 fill` visible refresh | `FRDM` | Pending silicon |
| `FRDM_T2000=1` west build | compile | **OK** — `zephyr.elf` links `eink_t2000_*`, UHC overlay merged |

Flash when bench is up:

```bash
FRDM_T2000=1 ./scripts/build-rt1186-frdm-eink.sh
west flash -d build-frdm-rt1186-eink
# console: ser2net / MCU-Link @ 115200
eink t2000 info
eink t2000 fill 0 0
```

## Deferred

Portal `show`/`sync`, RGB decode, LittleFS streaming, EL133 coexistence productization, upstream Zephyr `usbh*` in `nxp_rt118x.dtsi`.
