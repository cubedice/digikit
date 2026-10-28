/* DT2 1.17 shims: preserve caller registers and mask queue access. */
    .text
    .even
    .globl pp_start_hook
pp_start_hook:
    move.l %d2,0x80004888
    lea -20(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,4(%sp)
    moveq #1,%d0
    move.l %d0,(%sp)
    bsr.w pp_snapshot
    movem.l 4(%sp),%d0-%d1/%a0-%a1
    lea 20(%sp),%sp
    jmp pp_start_back
    .globl pp_seq_hook
pp_seq_hook:
    move.l %d2,0x80004888
    lea -20(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,4(%sp)
    clr.l (%sp)
    bsr.w pp_snapshot
    movem.l 4(%sp),%d0-%d1/%a0-%a1
    lea 20(%sp),%sp
    jmp pp_tick_back
    .globl pp_step_due_hook
pp_step_due_hook:
    lea -28(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,8(%sp)
    move.w %sr,%d0
    move.w %d0,24(%sp)
    move.w #0x2700,%sr
    move.l 4(%a4),(%sp)
    moveq #5,%d0
    move.l %d0,4(%sp)
    bsr.w pp_due
    move.w 24(%sp),%d0
    move.w %d0,%sr
    movem.l 8(%sp),%d0-%d1/%a0-%a1
    lea 28(%sp),%sp
    jmp pp_stock_step
    .globl pp_start_due_hook
pp_start_due_hook:
    lea -28(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,8(%sp)
    move.w %sr,%d0
    move.w %d0,24(%sp)
    move.w #0x2700,%sr
    move.l 4(%a4),(%sp)
    moveq #7,%d0
    move.l %d0,4(%sp)
    bsr.w pp_due
    move.w 24(%sp),%d0
    move.w %d0,%sr
    movem.l 8(%sp),%d0-%d1/%a0-%a1
    lea 28(%sp),%sp
    moveq #1,%d1
    move.b %d1,-109(%a6)
    jmp pp_start_due_back
    .globl pp_write_frame
pp_write_frame:
    lea -12(%sp),%sp
    move.w %sr,%d0
    move.w %d0,8(%sp)
    move.w #0x2700,%sr
    move.l 16(%sp),(%sp)
    move.l 20(%sp),4(%sp)
    bsr.w pp_write_frame_impl
    move.w 8(%sp),%d0
    move.w %d0,%sr
    lea 12(%sp),%sp
    rts
