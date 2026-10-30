/* DT2 1.17 deserializer wrapper. C ABI args: destination, serialized Sound.
 * Keep the stock result and all callee-saved registers; bracket the whole call.
 * The original eight-byte prologue is replayed only by cp_sound_original. */
    .text
    .even
    .globl cp_sound_entry
cp_sound_entry:
    lea -16(%sp),%sp
    movem.l %d2-%d3/%a2-%a3,(%sp)
    move.l 20(%sp),%a2
    move.l 24(%sp),%a3
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr cp_sound_begin
    addq.l #8,%sp
    move.l %d0,%d2
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.w cp_sound_original
    addq.l #8,%sp
    move.l %d0,%d3
    move.l %d2,-(%sp)
    jsr cp_sound_end
    addq.l #4,%sp
    move.l %d3,%d0
    movem.l (%sp),%d2-%d3/%a2-%a3
    lea 16(%sp),%sp
    rts
cp_sound_original:
    lea -32(%sp),%sp
    movem.l %d2-%d4/%a2-%a6,(%sp)
    jmp cp_sound_resume
