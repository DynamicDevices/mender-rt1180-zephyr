#!/usr/bin/env python3
from pathlib import Path

src = Path("mender-mcu-integration/modules/eink-el133/drivers/display/display_el133uf1.c").read_text()
for token in ("EL133_DTM", "EL133_PON", "EL133_DRF", "EL133_POF", "EL133_CS_BOTH",
              "SCREEN_INFO_EPD", "PIXEL_FORMAT_L_4"):
    assert token in src, token
body = src[src.index("static int refresh"): src.index("static int el133_write")]
assert body.index("EL133_PON") < body.index("EL133_DRF") < body.index("EL133_POF")
assert "EL133_CS_BOTH" in body
print("OK: EL133 driver sequence invariants")
