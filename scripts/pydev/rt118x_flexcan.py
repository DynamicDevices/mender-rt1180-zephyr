# FlexCAN MCR (fsl_flexcan.h): Enable waits LPMACK==0; disable waits LPMACK==1.
MDIS = 1 << 31
FRZ = 1 << 30
HALT = 1 << 28
SOFTRST = 1 << 25
FRZACK = 1 << 24
LPMACK = 1 << 20
if request.IsInit:
    if "flexcan_mcr" not in globals():
        flexcan_mcr = MDIS | LPMACK
        flexcan_mem = {}
elif request.IsWrite:
    off = int(request.Offset)
    val = int(request.Value)
    if off == 0:
        mcr = val & ~SOFTRST
        if mcr & MDIS:
            mcr |= LPMACK
        else:
            mcr &= ~LPMACK
        if mcr & FRZ:
            mcr |= FRZACK
        else:
            mcr &= ~FRZACK
        flexcan_mcr = mcr
    else:
        flexcan_mem[off] = val
else:
    off = int(request.Offset)
    if off == 0:
        request.Value = int(globals().get("flexcan_mcr", MDIS | LPMACK))
    else:
        request.Value = int(globals().get("flexcan_mem", {}).get(off, 0))
