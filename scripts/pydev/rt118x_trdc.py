# RT118x TRDC HWCFG0/DACFG. MappedMemory zeros fail SDK asserts.
# HWCFG0: NDID=16, NMSTR=8, NMBC=4, NMRC=8
# NCM per master is set by the instance script via NCM list (1 = non-CPU).
HWCFG0 = 0x08040810

if request.IsInit:
    if "rt118x_trdc_mem" not in globals():
        rt118x_trdc_mem = {}
elif request.IsWrite:
    off = int(request.Offset)
    if off != 0xF0:
        rt118x_trdc_mem[off] = int(request.Value)
else:
    off = int(request.Offset)
    if off == 0xF0:
        request.Value = HWCFG0
    elif 0x100 <= off < 0x108:
        ncm = globals().get("RT118X_TRDC_NCM", [0, 0, 1, 1, 1, 1, 1, 1])
        base = off - 0x100
        val = 0
        for i in range(4):
            m = base + i
            bit = 0x80 if (m < len(ncm) and ncm[m]) else 0
            val |= bit << (8 * i)
        request.Value = val
    else:
        request.Value = int(rt118x_trdc_mem.get(off, 0))
