# TRDC1 DACFG: m1 CM33 CPU (NCM=0), m2 DMA3 non-CPU (NCM=1)
HWCFG0 = 0x08040810
NCM = (1, 0, 1, 1, 1, 1, 1, 1)
if request.IsInit:
    if "rt118x_trdc1" not in globals():
        rt118x_trdc1 = {}
elif request.IsWrite:
    off = int(request.Offset)
    if off != 0xF0:
        rt118x_trdc1[off] = int(request.Value)
else:
    off = int(request.Offset)
    if off == 0xF0:
        request.Value = HWCFG0
    elif 0x100 <= off < 0x108:
        base = off - 0x100
        val = 0
        for i in range(4):
            m = base + i
            bit = 0x80 if (m < len(NCM) and NCM[m]) else 0
            val |= bit << (8 * i)
        request.Value = val
    else:
        request.Value = int(rt118x_trdc1.get(off, 0))
