/* DT2 1.17: mirror28 is a full-width CANOPY MOD carrier. Stock sampler
 * bank/slot splitting and recombination would discard its upper six bits.
 * Leave every other track/type on its exact displaced instruction path.
 * The split loop keeps its track in d7, the recombine loop in d6. */
    .text
    .globl ip_mod_split
ip_mod_split:
    lea     -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l  %d7,-(%sp)
    jsr     instrument_mod_carrier
    addq.l  #4,%sp
    tst.l   %d0
    beq.s   2f
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea     16(%sp),%sp
    jmp     ip_mod_split_skip
2:  movem.l (%sp),%d0-%d1/%a0-%a1
    lea     16(%sp),%sp
1:  move.w  0x5a(%a4),%d0
    move.l  %d0,%d1
    jmp     ip_mod_split_next

    .globl ip_mod_recombine
ip_mod_recombine:
    lea     -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l  %d6,-(%sp)
    jsr     instrument_mod_carrier
    addq.l  #4,%sp
    tst.l   %d0
    beq.s   2f
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea     16(%sp),%sp
    jmp     ip_mod_recombine_skip
2:  movem.l (%sp),%d0-%d1/%a0-%a1
    lea     16(%sp),%sp
1:  mvs.w   0x5a(%a4),%d0
    cmpi.l  #0x400,%d0
    jmp     ip_mod_recombine_next
