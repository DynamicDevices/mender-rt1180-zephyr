#!/usr/bin/env python3
"""Verify the recreated EL133UF1 Zephyr driver against the E Ink reference.

Self-contained invariants always run. When the MIT reference tree
(/data_drive/esl/eink-spectra6) is present, this also proves opcode values,
register payloads, and the power-up register order match byte-for-byte — so a
skeleton/partial recreation can never silently pass again.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

DRIVER = Path(
    "mender-mcu-integration/modules/eink-el133/drivers/display/display_el133uf1.c"
)
REF_ROOT = Path("/data_drive/esl/eink-spectra6")
REF_HDR = REF_ROOT / "include/el133uf1.h"
REF_SRC = REF_ROOT / "src/core/el133uf1_driver.c"


def fail(msg: str) -> "NoReturn":  # type: ignore[name-defined]
    print(f"FAIL: {msg}", file=sys.stderr)
    raise SystemExit(1)


def parse_hex_bytes(text: str) -> list[int]:
    out = []
    for tok in text.split(","):
        tok = tok.strip()
        if not tok:
            continue
        out.append(int(tok, 0))
    return out


def main() -> int:
    src = DRIVER.read_text()

    # --- Self-contained invariants -------------------------------------------------
    for token in (
        "EL133_DTM", "EL133_PON", "EL133_DRF", "EL133_POF", "EL133_CS_BOTH",
        "SCREEN_INFO_EPD", "PIXEL_FORMAT_L_4", "epd_init_registers", "wait_ready",
    ):
        if token not in src:
            fail(f"missing token {token}")

    refresh = src[src.index("static int refresh"): src.index("static int el133_write")]
    if not (refresh.index("EL133_PON") < refresh.index("EL133_DRF") < refresh.index("EL133_POF")):
        fail("refresh order must be PON < DRF < POF")
    if refresh.count("EL133_PON") < 2 or refresh.count("EL133_POF") < 2:
        fail("refresh must drive PON and POF per-controller (CS0 then CS1)")
    if "EL133_CS_BOTH, EL133_DRF" not in refresh:
        fail("DRF must be issued to both controllers simultaneously")

    # BUSY HIGH == ready (reference semantics).
    if "!= 1" not in src[src.index("wait_ready"): src.index("static int xfer_cmd")]:
        fail("wait_ready must poll for BUSY HIGH (== 1 means ready)")

    # Zephyr driver opcodes and register payloads.
    zop = {m.group(1): int(m.group(2), 0)
           for m in re.finditer(r"#define\s+EL133_(\w+)\s+(0x[0-9A-Fa-f]+)", src)}
    zval = {m.group(1): parse_hex_bytes(m.group(2))
            for m in re.finditer(r"static const uint8_t\s+V_(\w+)\[\]\s*=\s*\{([^}]*)\}", src)}

    # Init order as coded in epd_init_registers().
    init_body = src[src.index("epd_init_registers"): src.index("static int load_dtm")]
    z_order = re.findall(r"EL133_WRITE_REG\(cfg,\s*(EL133_CS0|EL133_CS1|EL133_CS_BOTH),\s*EL133_(\w+),\s*V_(\w+)\)",
                         init_body)
    if len(z_order) < 20:
        fail(f"expected >=20 init register writes, found {len(z_order)}")

    # --- Equivalence against the E Ink reference (if available) --------------------
    if REF_HDR.is_file() and REF_SRC.is_file():
        hdr = REF_HDR.read_text()
        rsrc = REF_SRC.read_text()
        rop = {m.group(1): int(m.group(2), 0)
               for m in re.finditer(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)", hdr)}
        rval = {m.group(1): parse_hex_bytes(m.group(2))
                for m in re.finditer(r"static const uint8_t\s+(\w+)_V\[\d*\]\s*=\s*\{([^}]*)\}", rsrc)}

        checked_ops = 0
        for name, val in zop.items():
            if name in rop:
                if rop[name] != val:
                    fail(f"opcode {name}: driver 0x{val:02X} != reference 0x{rop[name]:02X}")
                checked_ops += 1
        if checked_ops < 15:
            fail(f"only {checked_ops} opcodes cross-checked against reference")

        checked_vals = 0
        for name, val in zval.items():
            if name in rval:
                if rval[name] != val:
                    fail(f"register {name}: driver {val} != reference {rval[name]}")
                checked_vals += 1
        if checked_vals < 15:
            fail(f"only {checked_vals} register payloads cross-checked against reference")

        # Reference init order from el133uf1_epd_init().
        epd = rsrc[rsrc.index("el133uf1_epd_init"):]
        epd = epd[: epd.index("EPD controller initialized")]
        r_names = re.findall(r"write_epd_register\(device,\s*[^,]+,\s*(\w+),", epd)
        z_names = [n for (_cs, _op, n) in z_order]
        # Compare the ordered sequence of register names present in both.
        common = [n for n in r_names if n in zval]
        z_common = [n for n in z_names if n in rval]
        if common != z_common:
            fail(f"init order mismatch:\n  reference: {common}\n  driver:    {z_common}")
        print(f"OK: EL133 driver matches reference "
              f"({checked_ops} opcodes, {checked_vals} payloads, {len(common)}-step init order)")
    else:
        print(f"OK: EL133 driver invariants ({len(z_order)} init writes); "
              f"reference tree absent, skipped byte-equivalence")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
