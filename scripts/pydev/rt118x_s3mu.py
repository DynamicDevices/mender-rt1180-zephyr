# ELE S3MU (PERI_S3MU.h). Firmware loops ELE_BaseAPI_GetFwStatus until
# rmsg[0]==0xe1C50306 and rmsg[1]==0xd6. Size in header bits 15:8.
if request.IsInit:
    if "s3mu_cmd" not in globals():
        s3mu_cmd = 0
        s3mu_rr = [0xE1C50306, 0xD6, 0, 0]
elif request.IsWrite:
    off = int(request.Offset)
    val = int(request.Value) & 0xFFFFFFFF
    if off == 0x200:
        s3mu_cmd = val
        cmd = (val >> 16) & 0xFF
        ver = val & 0xFF
        if cmd == 0xC5:
            size = 3
        else:
            size = 2
        hdr = 0xE1000000 | (cmd << 16) | (size << 8) | ver
        s3mu_rr = [hdr, 0xD6, 0, 0]
else:
    off = int(request.Offset)
    if off == 0x124:
        request.Value = 0xFF
    elif off == 0x12C:
        request.Value = 0xF
    elif 0x280 <= off < 0x290:
        idx = (off - 0x280) // 4
        rr = globals().get("s3mu_rr", [0xE1C50306, 0xD6, 0, 0])
        request.Value = rr[idx] if idx < 4 else 0
    else:
        request.Value = 0
