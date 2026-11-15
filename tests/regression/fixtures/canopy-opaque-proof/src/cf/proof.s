/* Emulator-only 1.17 wrappers, adapted from canopy-sound-proof. */
    .text
    .even
    .globl canopy_opaque_load_entry
canopy_opaque_load_entry:
    lea 12(%sp),%sp
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr canopy_opaque_loaded
    addq.l #8,%sp
    moveq #1,%d0
    jmp 0x400db802

    .globl canopy_opaque_save_entry
canopy_opaque_save_entry:
    lea -16(%sp),%sp
    movem.l %d2-%d3/%a2-%a3,(%sp)
    move.l 20(%sp),%a2
    move.l 24(%sp),%a3
    move.l 28(%sp),-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.w 1f
    lea 12(%sp),%sp
    move.l %d0,%d3
    move.l %d3,-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr canopy_opaque_saved
    lea 12(%sp),%sp
    move.l %d3,%d0
    movem.l (%sp),%d2-%d3/%a2-%a3
    lea 16(%sp),%sp
    rts
1:
    lea -36(%sp),%sp
    movem.l %d2-%d5/%a2-%a6,(%sp)
    jmp 0x400dba9c
