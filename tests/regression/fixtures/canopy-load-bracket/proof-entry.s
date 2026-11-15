/* Test-only DTII 1.17 load entry. Stock callers have no refusal contract. */
    .text
    .even
    .globl canopy_bracket_entry
canopy_bracket_entry:
    lea -16(%sp),%sp
    movem.l %d2-%d3/%a2-%a3,(%sp)
    move.l 20(%sp),%a2
    move.l 24(%sp),%a3
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr ip_sound_begin
    addq.l #8,%sp
    tst.l %d0
    bmi.s 1f
    move.l %d0,%d2
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.w canopy_bracket_original
    addq.l #8,%sp
    move.l %d0,%d3
    tst.l canopy_bracket_force_partial
    beq.s 2f
    moveq #0,%d3
2:
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    jsr ip_sound_proof_end
    addq.l #8,%sp
    move.l %d3,%d0
    bra.s 3f
1:
    moveq #0,%d0
3:
    movem.l (%sp),%d2-%d3/%a2-%a3
    lea 16(%sp),%sp
    rts
canopy_bracket_original:
    lea -32(%sp),%sp
    movem.l %d2-%d4/%a2-%a6,(%sp)
    jmp canopy_bracket_resume
