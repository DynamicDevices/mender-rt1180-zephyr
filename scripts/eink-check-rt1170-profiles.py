#!/usr/bin/env python3
"""Static contract checks for RT1170 lab LCD / EL133 and Active ESL profiles."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def fail(msg: str) -> None:
    print("FAIL: " + msg, file=sys.stderr)
    raise SystemExit(1)


def read(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        fail("missing " + rel)
    return path.read_text()


def require(text: str, needle: str, where: str) -> None:
    if needle not in text:
        fail(where + ": missing " + repr(needle))


def forbid(text: str, needle: str, where: str) -> None:
    if needle in text:
        fail(where + ": must not contain " + repr(needle))


def conf_flag(text: str, key: str):
    m = re.search(r"^" + re.escape(key) + r"=(.*)$", text, re.M)
    return m.group(1).strip() if m else None


def main() -> int:
    lcd_conf = read("mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_lcd.conf")
    el_conf = read("mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_eink_el133.conf")
    aesl = read("mender-mcu-integration/boards/mimxrt1170_aesl_eink.conf")
    lcd_ov = read("mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_lcd.overlay")
    el_ov = read("mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7_eink_el133.overlay")
    flex = read("mender-mcu-integration/boards/mimxrt1170_custom_eink_flexspi2.overlay")
    kconfig = read("mender-mcu-integration/Kconfig")
    lcd_sh = read("scripts/build-rt1170-evk-lcd.sh")
    el_sh = read("scripts/build-rt1170-evk-eink.sh")
    frame = read("mender-mcu-integration/src/eink/eink_frame.c")
    disp = read("mender-mcu-integration/src/eink/eink_display.c")

    require(kconfig, "config APP_EINK_DISPLAY_LCD_PREVIEW", "Kconfig")
    require(frame, "eink_frame_nibble_to_rgb565", "eink_frame.c")
    require(disp, "write_lcd_preview_from_halves", "eink_display.c")
    require(disp, "CONFIG_APP_EINK_DISPLAY_LCD_PREVIEW", "eink_display.c")

    if conf_flag(lcd_conf, "CONFIG_APP_EINK_DISPLAY_LCD_PREVIEW") != "y":
        fail("lcd.conf must enable APP_EINK_DISPLAY_LCD_PREVIEW")
    if conf_flag(lcd_conf, "CONFIG_APP_EINK_FULL_FRAMEBUFFER") == "y":
        fail("lcd.conf must not enable FULL_FRAMEBUFFER")
    require(lcd_conf, "CONFIG_FILE_SYSTEM_LITTLEFS=y", "lcd.conf")
    require(lcd_conf, "CONFIG_FILE_SYSTEM_SHELL=y", "lcd.conf")
    require(lcd_conf, "CONFIG_APP_EINK_SHELL=y", "lcd.conf")
    require(lcd_conf, 'CONFIG_APP_EINK_STORE_ROOT="/lfs1/eink"', "lcd.conf")

    if conf_flag(el_conf, "CONFIG_APP_EINK_DISPLAY_LCD_PREVIEW") == "y":
        fail("el133.conf must not enable LCD preview")
    require(el_conf, "CONFIG_DUMMY_DISPLAY=y", "el133.conf (interim until EL133 GPIOs)")
    require(el_conf, "CONFIG_FILE_SYSTEM_LITTLEFS=y", "el133.conf")
    require(el_conf, "CONFIG_FILE_SYSTEM_SHELL=y", "el133.conf")
    require(el_conf, "CONFIG_APP_EINK_SHELL=y", "el133.conf")

    board = read("mender-mcu-integration/boards/mimxrt1170_evk_mimxrt1176_cm7.conf")
    shell_frag = read("mender-mcu-integration/boards/mimxrt1170_eink_shell.conf")
    require(board, "CONFIG_SHELL=y", "EVK board conf (shell parity with native_sim)")
    require(shell_frag, "CONFIG_SHELL=y", "eink_shell.conf")
    require(shell_frag, "CONFIG_FILE_SYSTEM_SHELL=y", "eink_shell.conf")
    forbid(shell_frag, "CONFIG_POSIX_SYSTEM_INTERFACES=y", "eink_shell.conf (keep MCU light)")
    require(lcd_sh, "mimxrt1170_eink_shell.conf", "build-rt1170-evk-lcd.sh")
    require(el_sh, "mimxrt1170_eink_shell.conf", "build-rt1170-evk-eink.sh")

    if conf_flag(aesl, "CONFIG_APP_EINK_DISPLAY_LCD_PREVIEW") == "y":
        fail("aesl product conf must not enable LCD preview")
    if conf_flag(aesl, "CONFIG_APP_EINK_FULL_FRAMEBUFFER") == "y":
        fail("aesl product conf must not enable FULL_FRAMEBUFFER")
    require(aesl, "CONFIG_APP_EINK_BATTERY_DUTY_CYCLE=y", "aesl.conf")
    require(aesl, "CONFIG_APP_EINK_OTA_FLASH_STAGING=y", "aesl.conf")

    require(lcd_sh, "rk055hdmipi4ma0", "build-rt1170-evk-lcd.sh")
    forbid(lcd_sh, "eink-el133", "build-rt1170-evk-lcd.sh")
    require(el_sh, "eink-el133", "build-rt1170-evk-eink.sh")
    forbid(el_sh, "rk055hdmipi4ma0", "build-rt1170-evk-eink.sh")

    require(lcd_ov, "w25q512nw", "lcd.overlay (EVK rev B NOR)")
    require(lcd_ov, "eink_storage_partition", "lcd.overlay")
    require(lcd_ov, 'mount-point = "/lfs1"', "lcd.overlay")
    require(el_ov, "el133uf1", "el133.overlay")
    require(el_ov, 'status = "disabled"', "el133.overlay (GPIOs TBD)")
    require(el_ov, "dummy_dc", "el133.overlay interim display")
    require(el_ov, "&lpspi1", "el133.overlay")

    require(flex, "zephyr,fstab,littlefs", "flexspi2.overlay")
    require(flex, "DNP", "flexspi2.overlay docs")
    require(flex, "64", "flexspi2.overlay size comments")

    print("OK: RT1170 lab/product profile contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
