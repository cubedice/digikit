    .text
    .even
    .globl tp_probe_sr
tp_probe_sr:
    moveq #0,%d0
    move.w %sr,%d0
    rts
    .globl tp_probe_mask
tp_probe_mask:
    moveq #0,%d0
    move.w %sr,%d0
    move.w #0x2700,%sr
    .globl tp_probe_mask_applied
tp_probe_mask_applied:
    rts
    .globl tp_probe_restore
tp_probe_restore:
    move.w 10(%sp),%d0
    .globl tp_probe_restore_before
tp_probe_restore_before:
    move.w %d0,%sr
    rts
