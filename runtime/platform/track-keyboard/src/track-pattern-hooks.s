/* DTII 1.17. Replay the complete displaced prologue and preserve the
 * stock return value. The save callback may fail the native transaction if
 * the new record would not fit. */
    .text
    .even

    .macro track_record_entry symbol, callback, frame, registers, resume, saving
    .globl \symbol
\symbol:
    lea -12(%sp),%sp
    movem.l %d2/%a2-%a3,(%sp)
    move.l 16(%sp),%a2
    move.l 20(%sp),%a3
    move.l 24(%sp),-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.s 1f
    lea 12(%sp),%sp
    move.l %d0,%d2
    move.l %d2,-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr \callback
    lea 12(%sp),%sp
    .if !\saving
    move.l %d2,%d0
    .endif
    movem.l (%sp),%d2/%a2-%a3
    lea 12(%sp),%sp
    rts
1:
    lea \frame(%sp),%sp
    movem.l \registers,(%sp)
    jmp \resume
    .endm

    track_record_entry tk_pattern_save_entry, tk_pattern_fw_save, -24, %d2-%d6/%a2, tk_pattern_save_resume, 1
    track_record_entry tk_pattern_load_entry, tk_pattern_fw_load, -32, %d2-%d5/%a2-%a5, tk_pattern_load_resume, 0

    .globl tk_pattern_reset_entry
tk_pattern_reset_entry:
    lea -8(%sp),%sp
    movem.l %d2/%a2,(%sp)
    move.l 12(%sp),%a2
    move.l 16(%sp),-(%sp)
    move.l %a2,-(%sp)
    bsr.s 1f
    addq.l #8,%sp
    move.l %d0,%d2
    move.l %a2,-(%sp)
    jsr tk_pattern_fw_reset
    addq.l #4,%sp
    move.l %d2,%d0
    movem.l (%sp),%d2/%a2
    lea 8(%sp),%sp
    rts
1:
    lea -24(%sp),%sp
    movem.l %d2-%d5/%a2-%a3,(%sp)
    jmp tk_pattern_reset_resume

    .even
    .globl tk_pattern_track_clear_entry
tk_pattern_track_clear_entry:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l 24(%sp),-(%sp)        /* native track data */
    move.l 24(%sp),-(%sp)        /* native track index */
    jsr tk_pattern_fw_track_clear
    addq.l #8,%sp
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    jmp tk_pattern_track_clear_tail
