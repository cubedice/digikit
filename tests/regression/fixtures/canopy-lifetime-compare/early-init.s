/* Emulator-only DTII 1.17 Sound initializer entry candidate. The live edit
 * must already have an immutable capture; this does not suspend admission. */
    .text
    .even
    .globl canopy_early_init_entry
canopy_early_init_entry:
    lea -12(%sp),%sp
    movem.l %d0/%a0,(%sp)
    move.w %sr,%d0
    move.w %d0,10(%sp)
    move.l 16(%sp),%a0
    tst.l %a0
    beq.s 1f
    move.w #0xffff,0x13a(%a0)
1:
    move.w 10(%sp),%d0
    move.w %d0,%sr
    movem.l (%sp),%d0/%a0
    lea 12(%sp),%sp
    /* Replay the complete original initializer prologue. */
    lea -40(%sp),%sp
    movem.l %d2-%d3/%a2-%a6,(%sp)
    jmp 0x400e64e4
