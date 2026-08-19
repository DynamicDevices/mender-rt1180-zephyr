# MIMXRT118x ANADIG OSC/PLL. API: request.IsInit / IsWrite like scripts/pydev/flipflop.py
if request.IsInit:
    rt118x_anatop = {}
    rt118x_pfd_gen = {}
elif request.IsWrite:
    off = int(request.Offset)
    rt118x_anatop[off] = int(request.Value)
    if off in (0x4030, 0x4070, 0x4020, 0x4050):
        key = 0x4030 if off in (0x4020, 0x4030) else 0x4070
        rt118x_pfd_gen[key] = 1 - rt118x_pfd_gen.get(key, 0)
else:
    off = int(request.Offset)
    val = int(rt118x_anatop.get(off, 0))
    if off == 0x4320:
        val |= 0x40000000
    elif off in (0x4000, 0x4010, 0x4040, 0x4100, 0x4200):
        val |= 0x20000000
    elif off in (0x4030, 0x4070):
        if rt118x_pfd_gen.get(off, 0):
            val |= 0x40404040
        else:
            val &= ~0x40404040
    request.Value = val
