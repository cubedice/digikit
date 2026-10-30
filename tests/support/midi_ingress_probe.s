/* Call the real USB/DIN trampolines with the firmware's argument-stack shape.
 * Continuations record which branch was selected; no stock router is simulated. */
    .text
    .even
    .macro snapshot
    move.l %d0,mi_probe_regs
    move.l %d1,mi_probe_regs+4
    move.l %d2,mi_probe_regs+8
    move.l %a0,mi_probe_regs+12
    move.l %a1,mi_probe_regs+16
    move.l %a2,mi_probe_regs+20
    .endm
    .globl mi_probe_hook
mi_probe_hook:
    lea -8(%sp),%sp
    movem.l %d2/%a2,(%sp)
    move.l 24(%sp),%d2
    move.l 20(%sp),-(%sp)
    move.l 20(%sp),-(%sp)
    move.l 20(%sp),-(%sp)
    move.l %sp,mi_probe_sp
    move.l #0x12345678,%d0
    move.l #0x23456789,%d1
    move.l #0x3456789a,%a0
    move.l #0x456789ab,%a1
    move.l #0x56789abc,%a2
    tst.l %d2
    bne.s 1f
    jmp dkrt_midi_hook
1:  jmp mi_din_hook

    .globl dkrt_midi_next,dkrt_midi_ret,mi_din_next,mi_din_return
dkrt_midi_next:
mi_din_next:
    snapshot
    moveq #1,%d0
    move.l %d0,mi_probe_stock
    bra.s mi_probe_return
dkrt_midi_ret:
mi_din_return:
    snapshot
    clr.l mi_probe_stock
mi_probe_return:
    move.l %sp,mi_probe_return_sp
    move.l (%sp),%d0
    move.l %d0,mi_probe_args
    move.l 4(%sp),%d0
    move.l %d0,mi_probe_args+4
    move.l 8(%sp),%d0
    move.l %d0,mi_probe_args+8
    lea 12(%sp),%sp
    movem.l (%sp),%d2/%a2
    lea 8(%sp),%sp
    rts
