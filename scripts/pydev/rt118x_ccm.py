if request.IsInit:
    rt118x_ccm = {}
elif request.IsWrite:
    rt118x_ccm[int(request.Offset)] = int(request.Value)
else:
    off = int(request.Offset)
    if off in (0x4440, 0x44C0):
        request.Value = 24000000
    else:
        request.Value = int(rt118x_ccm.get(off, 0))
