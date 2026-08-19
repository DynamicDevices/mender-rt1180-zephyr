# Keep XCACHE disabled so M33 does not treat HyperRAM as cacheable
# (no cache model in this stub; ENCACHE=1 made memset appear stuck).
if request.IsInit:
    pass
elif request.IsWrite:
    pass
else:
    request.Value = 0
