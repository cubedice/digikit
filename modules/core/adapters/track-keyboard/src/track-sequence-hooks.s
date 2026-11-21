/* OS 1.17 scheduler builds note records in the timer interrupt. Capture the
 * resolved step after all native chance/condition decisions. Only these call
 * sites are sequencer starts; other calls use the same builders for previews.
 */
    .text
    .even
    .globl tk_seq_audio_stage
tk_seq_audio_stage:
    move.l %a6,84(%a3)
    move.l 52(%sp),%d0
    cmpi.l #tk_seq_audio_builder_return,%d0
    bne.s 1f
    move.l 28(%a3),-(%sp)
    move.l %d3,-(%sp)
    move.l 16(%a3),-(%sp)
    move.l %a3,-(%sp)
    jsr tk_fw_seq_stage
    lea 16(%sp),%sp
1:
    move.l %a3,%d0
    jmp tk_seq_audio_stage_resume

    .globl tk_seq_midi_stage
tk_seq_midi_stage:
    move.l %d2,16(%a6)
    move.l 52(%sp),%d0
    cmpi.l #tk_seq_midi_builder_return,%d0
    bne.s 1f
    move.l 20(%a6),-(%sp)
    move.l %d4,-(%sp)
    move.l 8(%a6),-(%sp)
    move.l %a6,-(%sp)
    jsr tk_fw_seq_stage_midi
    lea 16(%sp),%sp
1:
    move.l %a3,%d0
    jmp tk_seq_midi_stage_resume

/* The audio callback has applied native mute/solo, retrig relevance and
 * event priority at this point. Preserve its temporaries across the C call.
 */
    .globl tk_seq_audio_due
tk_seq_audio_due:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l %a0,-(%sp)
    jsr tk_fw_seq_audio_due
    addq.l #4,%sp
    tst.l %d0
    bne.s 1f
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    movea.l 56(%a0),%a1
    move.l 32(%a0),%d0
    jmp tk_seq_audio_due_resume
1:
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    jmp tk_seq_audio_due_skip

/* The MIDI task has already applied the selected route, mute/solo and CC
 * locks. The same cleanup tail releases a consumed record in both paths.
 */
    .globl tk_seq_midi_due
tk_seq_midi_due:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l %d7,-(%sp)
    move.l %a2,-(%sp)
    jsr tk_fw_seq_midi_due
    addq.l #8,%sp
    tst.l %d0
    bne.s 1f
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    moveq #1,%d3
    move.w 66(%sp),%d1
    jmp tk_seq_midi_due_resume
1:
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    jmp tk_seq_midi_due_skip

/* Live record placement after the native step and lock update. */
    .globl tk_seq_capture_live
tk_seq_capture_live:
    move.l 20(%a6),-(%sp)
    move.l 16(%a6),-(%sp)
    move.l %d2,-(%sp)
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    jsr tk_fw_capture_native_live
    lea 20(%sp),%sp
    jsr tk_seq_capture_clock
    jmp tk_seq_capture_live_resume
