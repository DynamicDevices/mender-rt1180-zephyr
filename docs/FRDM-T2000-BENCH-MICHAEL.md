# FRDM + T2000 — bench note for Michael

**Goal:** prove FRDM USB **host** can talk to T2000 (VID `3558`) and paint a solid fill.  
**Firmware:** branch `feat/frdm-gpc-wait` / PR [#20](https://github.com/DynamicDevices/zephyr-rt1186-eink/pull/20) — build with `FRDM_T2000=1`.  
**Detail:** [`FRDM-T2000-USB-HOST.md`](FRDM-T2000-USB-HOST.md).

## Two Type-Cs (do not mix)

| Port | What it is | Cable |
|------|------------|--------|
| **J23** | MCU-Link — **flash + console** | Always to the PC for `west flash` / UART 115200 |
| **J63** | USB OTG — **T2000 only** | FRDM as host; must **source VBUS** to the TCON |

Programming is **never** via J63.

## If VBUS mod breaks programming

A VBUS-source hack belongs on **J63 only**. If LinkServer dies with the mod fitted:

1. Confirm the PC is on **J23**, not J63.
2. Unplug T2000, flash, then reconnect for host tests.
3. Check the mod is not tying into / backfeeding the board **5V** rail used by MCU-Link.

**Preferred lab workaround:** leave FRDM OTG stock; feed T2000 **5V VBUS externally** (Y-cable / injector). Flash stays on J23. If you do a board mod, put a **jumper** so VBUS force can be off during flash.

## Flash + smoke (when J23 works)

```bash
cd /data_drive/dd/zephyr-rt1170-eink-spectra6-frdm   # or your lane tree
FRDM_T2000=1 ./scripts/build-rt1186-frdm-eink.sh
west flash -d build-frdm-rt1186-eink
# console on MCU-Link @ 115200
```

Then on **J63** (VBUS present, T2000 plugged):

```text
eink t2000 info
eink t2000 clear
eink t2000 fill 0 0
```

**Pass:** `info` shows sensible W/H + panel_id; `fill` visibly refreshes the 25″ panel.  
**Linux cross-check (same TCON on a PC):** `lsusb | grep -i 3558` then `/data_drive/esl/eink-t2000-usb/build/bin/t2000_usb`.

## What we need back

- Photo or net list of the VBUS mod (which pads).
- Whether MCU-Link enumerates with mod on, **J23 only**, T2000 unplugged.
- UART snippet from `eink t2000 info` / `fill` when it works (or the fail log).
