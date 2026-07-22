#!/usr/bin/env python3
"""CI-safe contract checks for Active ESL identity, v2 sync, and production profile.

No Zephyr build required — safe for GitHub Actions checkout on the self-hosted runner.
"""
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
        fail(f"{where}: missing {needle!r}")


def forbid(text: str, needle: str, where: str) -> None:
    if needle in text:
        fail(f"{where}: must not contain {needle!r}")


def conf_flag(text: str, key: str) -> str | None:
    m = re.search(r"^" + re.escape(key) + r"=(.*)$", text, re.M)
    return m.group(1).strip() if m else None


def conf_unset(text: str, key: str) -> bool:
    return bool(re.search(r"^#\s*" + re.escape(key) + r"\s+is not set\s*$", text, re.M))


def main() -> int:
    main_c = read("mender-mcu-integration/src/main.c")
    soc_c = read("mender-mcu-integration/src/utils/soc_uid.c")
    soc_h = read("mender-mcu-integration/src/utils/soc_uid.h")
    http_c = read("mender-mcu-integration/src/eink/eink_http.c")
    store_h = read("mender-mcu-integration/src/eink/eink_store.h")
    kconfig = read("mender-mcu-integration/Kconfig")
    cmake = read("mender-mcu-integration/CMakeLists.txt")
    shell = read("mender-mcu-integration/src/eink/eink_shell.c")
    prod = read("mender-mcu-integration/boards/mimxrt1170_production.conf")
    sim = read("mender-mcu-integration/eink-native-sim.conf")
    contract = read("mender-mcu-integration/docs/EINK-CONTRACT.md")
    probe = read("scripts/eink-sim-etabelone-probe.py")
    sbom = read("scripts/generate-sbom.sh")

    # --- SoC UID identity SoT ---
    require(soc_h, "SOC_UID_HEX_MAX", "soc_uid.h")
    require(soc_c, "hwinfo_get_device_id", "soc_uid.c")
    require(soc_c, '"%02X"', "soc_uid.c (uppercase hex)")
    require(cmake, "src/utils/soc_uid.c", "CMakeLists.txt")
    require(main_c, '.name = "soc_uid"', "main.c Mender identity key")
    require(main_c, "soc_uid_get_hex", "main.c")
    forbid(main_c, '.name = "mac"', "main.c must not use mac identity")
    forbid(main_c, "netup_get_mac_address(mender_identity", "main.c must not fill identity from MAC")
    require(shell, "cmd_uid", "eink_shell.c")
    require(shell, "soc_uid=%s", "eink_shell.c")
    require(kconfig, "empty = SoC UID hex", "Kconfig APP_EINK_HTTP_DEVICE_ID help")

    # --- /node/v2 sync ---
    require(kconfig, "config APP_EINK_HTTP_V2_SYNC", "Kconfig")
    require(http_c, "CONFIG_APP_EINK_HTTP_V2_SYNC", "eink_http.c")
    require(http_c, "/node/v2/device/", "eink_http.c")
    require(http_c, "sync_v2_once_inner", "eink_http.c")
    require(http_c, "(v2)", "eink_http.c prof marker")
    require(store_h, "eink_store_save_content_hash", "eink_store.h")
    require(store_h, "eink_store_load_content_hash", "eink_store.h")
    require(http_c, "eink_http_download_image_hashed", "eink_http.c")
    if conf_flag(sim, "CONFIG_APP_EINK_HTTP_V2_SYNC") != "y":
        fail("eink-native-sim.conf must enable APP_EINK_HTTP_V2_SYNC for CF lab")
    require(contract, "CONFIG_APP_EINK_HTTP_V2_SYNC", "EINK-CONTRACT.md")
    require(probe, "(v2)", "eink-sim-etabelone-probe.py accepts v2 completion")
    require(probe, "-device_id=0x", "probe auto-passes hwinfo -device_id")

    # --- Production profile sketch ---
    require(prod, "CONFIG_APP_EINK_HTTP_V2_SYNC=y", "production.conf")
    require(prod, "CONFIG_APP_EINK_HTTP_ENABLE=y", "production.conf")
    if not conf_unset(prod, "CONFIG_SHELL"):
        fail("production.conf must unset CONFIG_SHELL")
    if not conf_unset(prod, "CONFIG_APP_EINK_SHELL"):
        fail("production.conf must unset CONFIG_APP_EINK_SHELL")
    if not conf_unset(prod, "CONFIG_APP_EINK_SELFTEST"):
        fail("production.conf must unset CONFIG_APP_EINK_SELFTEST")
    if conf_flag(prod, "CONFIG_APP_EINK_HTTP_AUTH_TOKEN") not in ('""', "''", ""):
        # empty string assignment
        tok = conf_flag(prod, "CONFIG_APP_EINK_HTTP_AUTH_TOKEN")
        if tok not in ('""',):
            fail("production.conf must keep AUTH_TOKEN empty (got %r)" % (tok,))

    # --- CRA process hooks still present ---
    require(sbom, "--archive-rc", "generate-sbom.sh")
    require(read("mender-mcu-integration/docs/CRA-ADVISORY-WATCH.md"), "Ship gate", "CRA-ADVISORY-WATCH.md")
    require(read("mender-mcu-integration/docs/HW-BRINGUP-CHECKLIST.md"), "Blocked", "HW-BRINGUP-CHECKLIST.md")

    print("OK: Active ESL identity / v2 / production contract checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
