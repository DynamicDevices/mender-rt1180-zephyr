# FRDM-IMXRT1186 — T2000 USB host MVP

**Status:** software MVP (host + thin client). Hardware proof needs TCON on Type-C OTG.  
**Protocol SoT:** [DynamicDevices/eink-t2000-usb](https://github.com/DynamicDevices/eink-t2000-usb) (`/data_drive/esl/eink-t2000-usb`).  
**Bench (Michael):** [`FRDM-T2000-BENCH-MICHAEL.md`](FRDM-T2000-BENCH-MICHAEL.md).

## Hardware

FRDM has **two** USB Type-C connectors — do not conflate them:

| Connector | Role | Use |
|-----------|------|-----|
| **J23** MCU-Link | Debug + console + usual board power | `west flash` / LinkServer / UART @ 115200 |
| **J63** USB OTG | SoC USBHS host/device | T2000 TCON only |

| Item | Notes |
|------|--------|
| Host role | FRDM must **source VBUS** on **J63** to the T2000 board |
| Cable | Host-capable Type-C / adapter on J63; confirm VBUS before chasing software |
| Device IDs | VID `0x3558`, PID `0x2002` (or `0x4002` main) |
| Panel | 25.3″ Spectra 6 via T2000 Mini-LVDS TCON |

### VBUS mod vs programming (lab)

Stock FRDM OTG may not drive VBUS as a USB host. A board VBUS-source mod belongs on **J63 only**.

- Programming path stays **J23** (MCU-Link). A correct OTG-only mod should not block LinkServer.
- If Michael reports “can’t program with the mod fitted”, check in order:
  1. PC cable is on **J23**, not J63.
  2. T2000 unplugged during flash (load / brownout).
  3. Mod does not backfeed or short the board **5V** tree into MCU-Link power.
- Clean workaround: leave FRDM OTG stock; feed T2000 **external 5V VBUS** (Y-cable / injector). Flash always via J23.
- Prefer a **jumper/switch** on any board VBUS source so flash can run with VBUS force off.

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

After flash + plug T2000 on **J63** (VBUS present):

```text
usbh ...                    # optional Zephyr host shell smoke
eink t2000 info             # W/H, panel_id, TCON/LUT
eink t2000 clear            # clear + wait idle
eink t2000 fill [idx] [wf]  # solid Y8 fill (default idx=0 wf=0)
```

Wire protocol is vendor bulk (`0xF1` / `0xF2`), not MSC BOT for paint.

## Protocol coverage vs Linux SoT

| Linux `epd_device` | Opcode(s) | Zephyr MVP |
|--------------------|-----------|------------|
| `t2000_get_dev_info` | F1/F2 `0xE0` | `eink_t2000_get_info` / shell `info` |
| `t2000_clear` | F1 `0x40` | `eink_t2000_clear` / shell `clear` |
| `t2000_set_mode` | F1 `0x43` | `eink_t2000_set_mode` (via fill) |
| `t2000_get_tcon_status` | F1/F2 `0x4F` | `eink_t2000_get_status` / wait idle |
| `t2000_multi_trigger_enable` | F1 `0x4C` | called inside `eink_t2000_fill` (SoT parity) |
| `t2000_multi_trigger` | F1 `0x4D` | end of fill |
| `t2000_Display` (Y8) | F1 `0xA1` + bulk | solid stripe fill only |
| `t2000_set_ColorIP` | `0x53` | **deferred** |
| `t2000_set_temp` | `0x33` | **deferred** |
| `t2000_set_panelID` | `0x80` | **deferred** |
| `t2000_Power_Control` | `0x3B` | **deferred** |
| `t2000_set_vcom` / write WF / OTA | various | **deferred** |
| Full-frame / RGB image path | Display + host decode | **deferred** (portal `show`/`sync`) |

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
| UART: USB host attach VID 3558 | `FRDM` | **Blocked** — MCU-Link / VBUS mod bench |
| `eink t2000 info` matches Linux W/H | `FRDM` | Pending silicon |
| `eink t2000 fill` visible refresh | `FRDM` | Pending silicon |
| `FRDM_T2000=1` west build | compile | **OK** — signed app + `eink_t2000_*` linked |

Flash when bench is up (J23 = LinkServer; J63 = T2000 + VBUS):

```bash
FRDM_T2000=1 ./scripts/build-rt1186-frdm-eink.sh
west flash -d build-frdm-rt1186-eink
# console: ser2net / MCU-Link @ 115200
eink t2000 info
eink t2000 fill 0 0
```

## Deferred

Portal `show`/`sync`, RGB decode, LittleFS streaming, EL133 coexistence productization, upstream Zephyr `usbh*` in `nxp_rt118x.dtsi`, remaining Linux opcodes in the table above.
