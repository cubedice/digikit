/* Isolated ColdFire test caller. The test patches the stock continuation to
 * RTS, so this runs only the prototype and its displaced instructions. */
    .text
    .even
    .globl canopy_type_probe
canopy_type_probe:
    lea -12(%sp),%sp
    movem.l %d2/%d6/%a2,(%sp)
    move.l 16(%sp),%a0
    move.l 20(%sp),%d2
    move.l 24(%sp),%d6
    move.w #0x2710,%sr
    jsr ip_sound_type_gap
    move.w %sr,%d0
    andi.l #0x1f,%d0
    movem.l (%sp),%d2/%d6/%a2
    lea 12(%sp),%sp
    rts
