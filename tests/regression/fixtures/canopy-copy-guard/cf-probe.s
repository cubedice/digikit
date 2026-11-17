    .text
    .even
    .globl cg_probe_mask
cg_probe_mask:
    moveq #0,%d0
    move.w %sr,%d0
    move.w #0x2700,%sr
    rts
    .globl cg_probe_restore
cg_probe_restore:
    move.w 10(%sp),%d0
    move.w %d0,%sr
    rts
