/* Only the per-destination calls in the stock MIDI note router consult the
 * shared adapter. Keep the remaining stock targets and original call ABI.
 */
    .text
    .even
    .globl tk_midi_target_on
tk_midi_target_on:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    pea 1.w                   /* on */
    move.l 24(%sp),-(%sp)     /* source among the already-pushed arguments */
    move.l %d2,-(%sp)         /* original MIDI note */
    move.l 104(%sp),-(%sp)    /* input channel from the router's stack frame */
    move.l %d3,-(%sp)         /* destination track */
    jsr tk_midi_native_action
    lea 20(%sp),%sp
    cmpi.l #2,%d0
    bne.s 2f
    moveq #1,%d0
    move.l %d0,24(%sp)       /* native arg 5 suppresses audition, preserves recording */
    clr.l %d0
2:  tst.l %d0
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    bne.s 1f
    jmp tk_midi_on_tail
1:  subq.l #8,%sp             /* stock return pops two more arguments */
    jmp tk_midi_on_return

    .even
    .globl tk_midi_target_off
tk_midi_target_off:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    clr.l -(%sp)              /* off */
    move.l 20(%sp),-(%sp)     /* pushed source */
    move.l %d2,-(%sp)         /* original MIDI note */
    move.l 64(%sp),-(%sp)     /* input channel from the router's stack frame */
    move.l %d4,-(%sp)         /* destination track */
    jsr tk_midi_native_action
    lea 20(%sp),%sp
    tst.l %d0
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    bne.s 1f
    jmp tk_midi_off_tail
1:  subq.l #8,%sp
    jmp tk_midi_off_return

/* The worker has already built the message and retained its native note
 * owner. Pass its event record in A2 to the generated-route filter. */
    .macro wire_note
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l %a2,-(%sp)
    move.l 24(%sp),-(%sp)    /* message among the stock call arguments */
    move.l 24(%sp),-(%sp)    /* message length */
    jsr tk_midi_routed_note
    lea 12(%sp),%sp
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    .endm

    .globl tk_midi_wire_on
tk_midi_wire_on:
    wire_note
    mvs.b %d5,%d0
    jmp tk_midi_wire_on_resume

    .globl tk_midi_wire_off
tk_midi_wire_off:
    wire_note
    clr.l (%a4,%d7.l)
    jmp tk_midi_wire_off_resume
