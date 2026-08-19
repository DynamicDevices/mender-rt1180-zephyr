# Generic RW block for GPC / similar (no wait-for-lock in hello path).
if request.IsInit:
    if "rt118x_gpc" not in globals():
        rt118x_gpc = {}
elif request.IsWrite:
    rt118x_gpc[int(request.Offset)] = int(request.Value)
else:
    request.Value = int(rt118x_gpc.get(int(request.Offset), 0))
