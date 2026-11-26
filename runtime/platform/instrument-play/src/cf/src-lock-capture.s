/* DT2 1.17: inspect both due-event paths, including an absent lock list.
 * d2 is track, a0 the stock event record, and record+0x54 its lock list.
 * Replaying the displaced move/branch preserves the stock result and flags. */
    .text
    .even
    .globl ip_lock_due_1
    .globl ip_lock_due_2
    .macro capture
    lea     -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l  0x54(%a0),-(%sp)
    move.l  %d2,-(%sp)
    jsr     ip_lock_capture
    addq.l  #8,%sp
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea     16(%sp),%sp
    move.l  0x54(%a0),%d0
    .endm
ip_lock_due_1:
    capture
    beq.s   1f
    jmp     ip_lock_due_1_next
1:  jmp     ip_lock_due_1_skip
ip_lock_due_2:
    capture
    beq.s   2f
    jmp     ip_lock_due_2_next
2:  jmp     ip_lock_due_2_skip
