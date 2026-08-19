# RT118x FlexSPI stub: Renode IMXRT_FlexSPI does not complete MCUX IP command
# polling (STS0 idle + INTR RX/TX watermark). Enough for init to return so
# printk can run. Not a flash model.
STS0_IDLE = 0x3  # SEQIDLE | ARBIDLE
INTR_OK = 0x1 | 0x20 | 0x40  # IPCMDDONE | IPRXWA | IPTXWE

if request.IsInit:
    if "rt118x_flexspi" not in globals():
        rt118x_flexspi = {}
elif request.IsWrite:
    off = int(request.Offset)
    val = int(request.Value)
    # MCR0.SWRESET (bit 0) is self-clearing; firmware spins until it reads 0.
    if off == 0:
        val &= ~0x1
    rt118x_flexspi[off] = val
else:
    off = int(request.Offset)
    if off == 0xE0:
        request.Value = STS0_IDLE
    elif off == 0x14:
        request.Value = INTR_OK
    elif off == 0xF0:
        request.Value = 0xFF
    elif 0x100 <= off < 0x180:
        request.Value = 0xFFFFFFFF
    else:
        request.Value = int(rt118x_flexspi.get(off, 0))
