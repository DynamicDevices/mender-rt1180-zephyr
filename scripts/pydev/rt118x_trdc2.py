# TRDC2: m0/m1 CM7 CPU, m2 DAP nCPU, m3 CS nCPU, m4 DMA4 nCPU, m5 NETC CPU
HWCFG0 = 0x08040810
NCM = (0, 0, 1, 1, 1, 0, 1, 1)
if request.IsInit:
    if "rt118x_trdc2" not in globals():
        rt118x_trdc2 = {}
elif request.IsWrite:
    off = int(request.Offset)
    if off != 0xF0:
        rt118x_trdc2[off] = int(request.Value)
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
        request.Value = int(rt118x_trdc2.get(off, 0))
