// Overloop: a looper that listens all the time. What is heard (this effect's output, the master mix) is written to a
// rolling record. When the pattern changes while the engine is armed, the last cycle of the pattern that was playing
// becomes the loop: the last L frames of the rolling record, L the cycle's length in frames. From that block on the
// loop plays from its start and the crossfader stands at LOOP, so what is heard goes on as it was one cycle earlier.
//
// Two buffers of N frames (the loop lease and the second phrase bank of core/loop-storage), each two planes of floats.
// One holds the rolling record. An armed change freezes it as the loop and the record goes on in the other one, so a
// loop can play while the record runs. The record in the other one begins empty: the frames heard before the change are
// copied into it from the loop's buffer, a block each block, so that a second change soon after the first finds them.
//
// One block:
//   controls   the parameter words: the crossfader, and the counters of ARM and DROP (core/loop-control holds them)
//   position   the record of core/pattern-position: a step boundary begins with this block or not; a boundary of
//              another pattern is a change, a boundary of another generation is a START. A record that comes again is
//              no news, and a few frames that did not arrive end nothing (OL_SKIP)
//   mix        output = input + (loop - input) * crossfader; LIVE passes the input as it is
//   record     the output goes to the rolling record, and a block of the frames before its start is filled in
//   wet        a send effect's output from the wet mailbox (PRISM's, while this engine runs it does not go to the FX return)
//              is added at the master stage's gain after its sum: behind the crossfader, and not in the record
//   publish    the tap, the status, and LW_TAKEN: the send effect sees in its next block that this engine finished one
#include "dk_fx.inc"
#include "dk_loop_imports.inc"
#include "overloop.h"

.SECTION/PM seg_pmco;
.GLOBAL _bl_process;
.EXTERN _loop_buffer_info;
.EXTERN _phrase_storage_info;
.EXTERN _pattern_position_info;

_bl_process:
    I5 = R4;
    I4 = R8;
    R0 = DM(DK_FX_IN_W_IN,I4);
    DM(OL_IN,I5) = R0;
    R0 = DM(DK_FX_IN_W_OUT,I4);
    DM(OL_OUT,I5) = R0;
    R0 = DM(DK_FX_IN_W_BLOCK,I4);
    DM(OL_BLOCK,I5) = R0;
    DM(OL_PARAMS,I5) = R12;

    // ---- The two buffers. Both descriptors are checked on every call, before any audio access: their addresses are
    // imports, never guessed locations. R2 the loop lease, R6 the second phrase bank, R9 the frames both hold.
    I4 = _loop_buffer_info;
    R0 = DM(0,I4);
    R1 = 0x4c425546;
    COMP(R0,R1);
    IF NE JUMP _ol_bad;
    R0 = DM(1,I4);
    R1 = 1;
    COMP(R0,R1);
    IF NE JUMP _ol_bad;
    R2 = DM(2,I4);
    R3 = DM(3,I4);
    I4 = _phrase_storage_info;
    R0 = DM(0,I4);
    R1 = 0x50484231;
    COMP(R0,R1);
    IF NE JUMP _ol_bad;
    R0 = DM(1,I4);
    R1 = 1;
    COMP(R0,R1);
    IF NE JUMP _ol_bad;
    R6 = DM(3,I4);
    R7 = DM(4,I4);
    R9 = DM(5,I4);
    R3 = PASS R3;
    IF LE JUMP _ol_bad;
    R7 = PASS R7;
    IF LE JUMP _ol_bad;
    R9 = PASS R9;
    IF LE JUMP _ol_bad;
    R0 = LSHIFT R3 BY -3;
    R1 = LSHIFT R7 BY -3;
    R0 = MIN(R0,R1);
    R9 = MIN(R9,R0);
    R0 = 0xffffffe0;
    R9 = R9 AND R0;
    R0 = OL_MIN_FRAMES;
    COMP(R9,R0);
    IF LT JUMP _ol_bad;
    R0 = R2 OR R6;
    R1 = 3;
    R0 = R0 AND R1;
    R0 = PASS R0;
    IF NE JUMP _ol_bad;
    // both lie in external memory and do not reach its end; they do not overlap
    R8 = LSHIFT R9 BY 3;
    R0 = R2;
    CALL _ol_range;
    R0 = PASS R0;
    IF NE JUMP _ol_bad;
    R0 = R6;
    CALL _ol_range;
    R0 = PASS R0;
    IF NE JUMP _ol_bad;
    R0 = R2 - R6;
    R0 = ABS R0;
    COMP(R0,R8);
    IF LT JUMP _ol_bad;
    R0 = DM(OL_INIT,I5);
    R0 = PASS R0;
    IF NE JUMP _ol_same;
    DM(OL_N,I5) = R9;
    DM(OL_BUF0,I5) = R2;
    DM(OL_BUF1,I5) = R6;
    R0 = 1;
    DM(OL_INIT,I5) = R0;
    CALL _ol_count_on;
    JUMP _ol_controls;
_ol_same:
    R0 = DM(OL_N,I5);
    COMP(R0,R9);
    IF NE JUMP _ol_moved;
    R0 = DM(OL_BUF0,I5);
    COMP(R0,R2);
    IF NE JUMP _ol_moved;
    R0 = DM(OL_BUF1,I5);
    COMP(R0,R6);
    IF EQ JUMP _ol_controls;
_ol_moved:
    // A lease that changed under a running engine: everything recorded is forgotten (the loop, the rolling record, the
    // position and the counters last read), and the engine goes on in the new buffers.
    R0 = I5;
    R1 = OL_ROLL * 4;
    R0 = R0 + R1;
    I4 = R0;
    R0 = 0;
    LCNTR = OL_IN - OL_ROLL, DO _ol_moved_clear UNTIL LCE;
_ol_moved_clear: DM(I4,M6) = R0;
    DM(OL_N,I5) = R9;
    DM(OL_BUF0,I5) = R2;
    DM(OL_BUF1,I5) = R6;
    CALL _ol_count_on;

    // ---- The controls. The counters come as floats 0 .. 127; a change of ARM arms or disarms, a change of DROP drops.
_ol_controls:
    I4 = DM(OL_PARAMS,I5);
    R1 = 0x3f000000;
    R5 = 127;
    R0 = DM(LP_ARM,I4);
    F0 = F0 + F1;
    R2 = TRUNC F0;
    R2 = R2 AND R5;
    R0 = DM(LP_DROP,I4);
    F0 = F0 + F1;
    R3 = TRUNC F0;
    R3 = R3 AND R5;
    R0 = DM(LP_SEEN,I4);
    F0 = F0 + F1;
    R4 = TRUNC F0;
    R4 = R4 AND R5;
    R0 = DM(OL_CTL,I5);
    R0 = PASS R0;
    IF NE JUMP _ol_ctl_edges;
    R0 = 1;
    DM(OL_CTL,I5) = R0;
    JUMP _ol_ctl_store;
_ol_ctl_edges:
    R0 = DM(OL_ARM_SEQ,I5);
    COMP(R0,R2);
    IF EQ JUMP _ol_ctl_drop;
    R0 = DM(OL_ARMED,I5);
    R1 = 1;
    R0 = R0 XOR R1;
    DM(OL_ARMED,I5) = R0;
_ol_ctl_drop:
    R0 = DM(OL_DROP_SEQ,I5);
    COMP(R0,R3);
    IF EQ JUMP _ol_ctl_store;
    // dropping: the crossfader runs to LIVE, then the loop is gone
    R0 = DM(OL_LOOP,I5);
    DM(OL_DROP,I5) = R0;
_ol_ctl_store:
    DM(OL_ARM_SEQ,I5) = R2;
    DM(OL_DROP_SEQ,I5) = R3;
    // the home has seen this loop: the crossfader is the home's again
    R0 = DM(OL_COUNT,I5);
    R0 = R0 AND R5;
    COMP(R0,R4);
    IF NE JUMP _ol_track;
    R0 = 0;
    DM(OL_HELD,I5) = R0;

    // ---- The position. A boundary is known to the block: it begins with the block whose record counts it, as the
    // notes of that step do.
_ol_track:
    I4 = _pattern_position_info;
    R0 = DM(0,I4);
    R1 = 0x50504f53;
    COMP(R0,R1);
    IF NE JUMP _ol_lost;
    R0 = DM(1,I4);
    R1 = 2;
    COMP(R0,R1);
    IF NE JUMP _ol_lost;
    R2 = DM(2,I4);
    R3 = DM(3,I4);
    R0 = DM(OL_HAVE,I5);
    R4 = DM(OL_TX,I5);
    R5 = DM(OL_BSEQ,I5);
    R7 = DM(OL_SAME,I5);
    R1 = 1;
    R0 = PASS R0;
    IF EQ JUMP _ol_first;
    // the same record again (a frame that came twice or late): nothing new in this block; OL_SKIP of them in a row end
    // the run
    R6 = R2 - R4;
    IF NE JUMP _ol_news;
    R7 = R7 + 1;
    DM(OL_SAME,I5) = R7;
    R0 = OL_SKIP;
    COMP(R7,R0);
    IF LT JUMP _ol_target;
    JUMP _ol_unrun;
_ol_first:
    // the first record: no edge can be placed
    DM(OL_TX,I5) = R2;
    DM(OL_BSEQ,I5) = R3;
    DM(OL_HAVE,I5) = R1;
    R0 = 0;
    DM(OL_SAME,I5) = R0;
    JUMP _ol_unrun;
_ol_news:
    DM(OL_TX,I5) = R2;
    DM(OL_BSEQ,I5) = R3;
    R0 = 0;
    DM(OL_SAME,I5) = R0;
    // a send count that went back, or a gap of more than OL_SKIP frames or blocks since the last new record: no edge can
    // be placed
    R6 = PASS R6;
    IF LT JUMP _ol_unrun;
    R0 = OL_SKIP;
    COMP(R6,R0);
    IF GT JUMP _ol_unrun;
    COMP(R7,R0);
    IF GE JUMP _ol_unrun;
    // stopped, or the timing is not valid
    R0 = DM(5,I4);
    R6 = 3;
    COMP(R0,R6);
    IF NE JUMP _ol_unrun;
    R10 = DM(8,I4);
    COMP(R10,R1);
    IF LE JUMP _ol_unrun;
    R0 = 128;
    COMP(R10,R0);
    IF GT JUMP _ol_unrun;
    R11 = DM(7,I4);
    R11 = PASS R11;
    IF LT JUMP _ol_unrun;
    COMP(R11,R10);
    IF GE JUMP _ol_unrun;
    R3 = R3 - R5;
    R3 = PASS R3;
    IF EQ JUMP _ol_target;
    COMP(R3,R1);
    IF NE JUMP _ol_unrun;
    // A boundary: step R11 of pattern R12 (R10 steps, generation R13) begins with this block.
    R12 = DM(6,I4);
    R13 = DM(4,I4);
    R0 = DM(OL_GEN_OK,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_gen_new;
    R0 = DM(OL_GEN,I5);
    COMP(R0,R13);
    IF EQ JUMP _ol_gen_same;
    // START. A loop that is held begins again with the pattern; the old position fades out under it.
    R0 = DM(OL_LOOP,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_gen_new;
    R0 = DM(OL_PHASE,I5);
    DM(OL_OLD,I5) = R0;
    R0 = OL_JUMP_FRAMES;
    DM(OL_JUMP,I5) = R0;
    R0 = 0;
    DM(OL_PHASE,I5) = R0;
_ol_gen_new:
    DM(OL_GEN,I5) = R13;
    R0 = 1;
    DM(OL_GEN_OK,I5) = R0;
    JUMP _ol_new_run;
_ol_gen_same:
    R0 = DM(OL_RUN,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_new_run;
    R0 = DM(OL_PAT,I5);
    COMP(R0,R12);
    IF NE JUMP _ol_change;
    R0 = DM(OL_LEN,I5);
    COMP(R0,R10);
    IF NE JUMP _ol_new_run;
    // The same pattern: the next step, or a jump inside it.
    CALL _ol_next;
    R0 = DM(OL_STEP,I5);
    R0 = R0 + 1;
    R1 = 0;
    COMP(R0,R10);
    IF GE R0 = PASS R1;
    COMP(R0,R11);
    IF EQ JUMP _ol_in_order;
    DM(OL_CYC,I5) = R1;
_ol_in_order:
    R11 = PASS R11;
    IF NE JUMP _ol_step_done;
    // step 0: a cycle begins, and a whole one has ended if every step since the last step 0 came in order
    R1 = DM(OL_T,I5);
    R0 = DM(OL_CYC,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_cycle_start;
    R0 = DM(OL_CYC_T,I5);
    R0 = R1 - R0;
    DM(OL_CYC_LEN,I5) = R0;
    DM(OL_NEXT,I5) = R0;
_ol_cycle_start:
    DM(OL_CYC_T,I5) = R1;
    R0 = 1;
    DM(OL_CYC,I5) = R0;
_ol_step_done:
    DM(OL_STEP,I5) = R11;
    R0 = DM(OL_RUN_N,I5);
    R0 = R0 + 1;
    DM(OL_RUN_N,I5) = R0;
    JUMP _ol_target;
_ol_change:
    // The pattern changed with this block. What an armed change keeps: the cycle that has just ended when it was a whole
    // one (a change at the pattern's end), else the length of the last whole cycle, else the steps so far.
    CALL _ol_next;
    R0 = DM(OL_CYC,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_change_armed;
    R11 = PASS R11;
    IF NE JUMP _ol_change_armed;
    R0 = DM(OL_STEP,I5);
    R0 = R0 + 1;
    R1 = DM(OL_LEN,I5);
    COMP(R0,R1);
    IF NE JUMP _ol_change_armed;
    R0 = DM(OL_T,I5);
    R1 = DM(OL_CYC_T,I5);
    R0 = R0 - R1;
    DM(OL_NEXT,I5) = R0;
_ol_change_armed:
    R0 = DM(OL_ARMED,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_new_run;
    R0 = DM(OL_NEXT,I5);
    R0 = PASS R0;
    IF LE JUMP _ol_new_run;
    CALL _ol_capture;
_ol_new_run:
    DM(OL_PAT,I5) = R12;
    DM(OL_LEN,I5) = R10;
    DM(OL_STEP,I5) = R11;
    R0 = 1;
    DM(OL_RUN,I5) = R0;
    DM(OL_RUN_N,I5) = R0;
    R1 = DM(OL_T,I5);
    DM(OL_RUN_T,I5) = R1;
    DM(OL_CYC_T,I5) = R1;
    R0 = 0;
    DM(OL_CYC_LEN,I5) = R0;
    DM(OL_NEXT,I5) = R0;
    R1 = 1;
    R11 = PASS R11;
    IF EQ R0 = PASS R1;
    DM(OL_CYC,I5) = R0;
    JUMP _ol_target;
_ol_lost:
    R0 = 0;
    DM(OL_HAVE,I5) = R0;
_ol_unrun:
    R0 = 0;
    DM(OL_RUN,I5) = R0;
    DM(OL_NEXT,I5) = R0;

    // ---- The crossfader's target: LIVE without a loop, LOOP while it is held there, else the home's value. First the
    // master stage's gain for the tap and the wet mailbox.
_ol_target:
    CALL _ol_master;
    R14 = 0;
    R0 = DM(OL_LOOP,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_audio;
    R0 = DM(OL_DROP,I5);
    R0 = PASS R0;
    IF NE JUMP _ol_audio;
    R14 = 0x3f800000;
    R0 = DM(OL_HELD,I5);
    R0 = PASS R0;
    IF NE JUMP _ol_audio;
    I4 = DM(OL_PARAMS,I5);
    R0 = DM(LP_XFAD,I4);
    // a value that is not finite is LIVE
    R1 = 0x7f800000;
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF NE JUMP _ol_xfad_finite;
    R0 = 0;
_ol_xfad_finite:
    R1 = 0;
    F0 = MAX(F0,F1);
    F14 = MIN(F0,F14);

_ol_audio:
    DM(OL_TARGET,I5) = R14;
    I0 = DM(OL_IN,I5);
    R0 = I0;
    R1 = 128;
    R0 = R0 + R1;
    I1 = R0;
    I2 = DM(OL_OUT,I5);
    R0 = I2;
    R0 = R0 + R1;
    I3 = R0;
    R0 = DM(OL_LOOP,I5);
    R0 = PASS R0;
    IF NE JUMP _ol_mix;
    // ---- No loop: the input as it is.
    LCNTR = DK_BLOCK, DO _ol_live_end UNTIL LCE;
        R0 = DM(I0,M6);
        R1 = DM(I1,M6);
        DM(I2,M6) = R0;
_ol_live_end: DM(I3,M6) = R1;
    // the tap is silent: cleared once
    R0 = DM(OL_TAP_SET,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_record;
    R0 = _loop_tap;
    R1 = LT_LEFT * 4;
    R0 = R0 + R1;
    I4 = R0;
    R0 = 0;
    LCNTR = 2 * DK_BLOCK, DO _ol_tap_clear UNTIL LCE;
_ol_tap_clear: DM(I4,M6) = R0;
    DM(OL_TAP_SET,I5) = R0;
    JUMP _ol_record;

    // ---- A loop is held. R5 the phase, R6 the loop's frames, R7 the buffer's frames, R15 the crossfader, R14 its
    // target, R13 its step a frame (1/256), R12 the tap's next word.
_ol_mix:
    R15 = DM(OL_GAIN,I5);
    R13 = 0x3b800000;
    R11 = 0x3f800000;
    R5 = DM(OL_PHASE,I5);
    R6 = DM(OL_L,I5);
    R7 = DM(OL_N,I5);
    M2 = R7;
    R12 = _loop_tap;
    R0 = LT_LEFT * 4;
    R12 = R12 + R0;
    LCNTR = DK_BLOCK, DO _ol_mix_end UNTIL LCE;
        // the loop's frame at this phase: R8 its index in the buffer, R9 and R10 its samples
        R0 = DM(OL_BASE,I5);
        R0 = R0 + R5;
        R1 = R0 - R7;
        COMP(R0,R7);
        IF GE R0 = PASS R1;
        R8 = PASS R0;
        R9 = 0;
        R10 = 0;
        R0 = DM(OL_SILENT,I5);
        COMP(R5,R0);
        IF LT JUMP _ol_seam;
        R1 = LSHIFT R8 BY 2;
        R0 = DM(OL_LBUF,I5);
        R0 = R0 + R1;
        I4 = R0;
        R9 = DM(I4,M5);
        R10 = DM(M2,I4);
_ol_seam:
        // the last OL_X frames run into the frames before the loop's start, so the turn is as it was recorded
        R0 = DM(OL_X,I5);
        R1 = R6 - R0;
        R1 = R5 - R1;
        R1 = PASS R1;
        IF LT JUMP _ol_jump;
        R1 = R1 + 1;
        F3 = FLOAT R1;
        R0 = DM(OL_XINV,I5);
        F3 = F3 * F0;
        // the weight starts and ends without a corner: a * a * (3 - 2a)
        F0 = F3 * F3;
        F1 = F3 + F3;
        R2 = 0x40400000;
        F1 = F2 - F1;
        F3 = F0 * F1;
        R0 = 0;
        R2 = 0;
        R4 = DM(OL_PRE,I5);
        R4 = PASS R4;
        IF EQ JUMP _ol_seam_mix;
        R4 = R8 - R6;
        R4 = PASS R4;
        IF GE JUMP _ol_pre_at;
        R4 = R4 + R7;
_ol_pre_at:
        R4 = LSHIFT R4 BY 2;
        R1 = DM(OL_LBUF,I5);
        R4 = R4 + R1;
        I4 = R4;
        R0 = DM(I4,M5);
        R2 = DM(M2,I4);
_ol_seam_mix:
        F0 = F0 - F9;
        F0 = F0 * F3;
        F9 = F9 + F0;
        F2 = F2 - F10;
        F2 = F2 * F3;
        F10 = F10 + F2;
_ol_jump:
        // after a START: the old position fades out over OL_JUMP_FRAMES
        R0 = DM(OL_JUMP,I5);
        R0 = PASS R0;
        IF EQ JUMP _ol_gain;
        F3 = FLOAT R0;
        F3 = F3 * F13;
        R0 = R0 - 1;
        DM(OL_JUMP,I5) = R0;
        // the weight starts and ends without a corner: a * a * (3 - 2a)
        F0 = F3 * F3;
        F1 = F3 + F3;
        R2 = 0x40400000;
        F1 = F2 - F1;
        F3 = F0 * F1;
        R4 = DM(OL_OLD,I5);
        R0 = R4 + 1;
        R1 = 0;
        COMP(R0,R6);
        IF GE R0 = PASS R1;
        DM(OL_OLD,I5) = R0;
        R0 = 0;
        R2 = 0;
        R1 = DM(OL_SILENT,I5);
        COMP(R4,R1);
        IF LT JUMP _ol_jump_mix;
        R0 = DM(OL_BASE,I5);
        R0 = R0 + R4;
        R1 = R0 - R7;
        COMP(R0,R7);
        IF GE R0 = PASS R1;
        R0 = LSHIFT R0 BY 2;
        R1 = DM(OL_LBUF,I5);
        R0 = R0 + R1;
        I4 = R0;
        R0 = DM(I4,M5);
        R2 = DM(M2,I4);
_ol_jump_mix:
        F0 = F0 - F9;
        F0 = F0 * F3;
        F9 = F9 + F0;
        F2 = F2 - F10;
        F2 = F2 * F3;
        F10 = F10 + F2;
_ol_gain:
        // the crossfader moves to its target by at most 1/256 a frame and lands on it exactly
        F0 = F14 - F15;
        F1 = ABS F0;
        COMP(F1,F13);
        IF GT JUMP _ol_ramp;
        R15 = PASS R14;
        JUMP _ol_ramped;
_ol_ramp:
        F1 = -F13;
        F2 = MAX(F0,F1);
        F0 = MIN(F2,F13);
        F15 = F15 + F0;
_ol_ramped:
        R4 = DM(I0,M6);
        R8 = DM(I1,M6);
        // the tap: what the loop adds to the output, at the level of the mix before the master stage
        F0 = F9 * F15;
        F1 = F10 * F15;
        R2 = DM(OL_INV,I5);
        F0 = F0 * F2;
        F1 = F1 * F2;
        I4 = R12;
        DM(0,I4) = R0;
        DM(DK_BLOCK,I4) = R1;
        R0 = 4;
        R12 = R12 + R0;
        // LIVE is the input as it is, LOOP the loop as it is
        R0 = 0;
        COMP(F15,F0);
        IF EQ JUMP _ol_dry;
        COMP(F15,F11);
        IF EQ JUMP _ol_out;
        F0 = F9 - F4;
        F0 = F0 * F15;
        F9 = F4 + F0;
        F0 = F10 - F8;
        F0 = F0 * F15;
        F10 = F8 + F0;
        JUMP _ol_out;
_ol_dry:
        R9 = PASS R4;
        R10 = PASS R8;
_ol_out:
        R5 = R5 + 1;
        R0 = 0;
        COMP(R5,R6);
        IF GE R5 = PASS R0;
        DM(I2,M6) = R9;
_ol_mix_end: DM(I3,M6) = R10;
    DM(OL_PHASE,I5) = R5;
    DM(OL_GAIN,I5) = R15;
    R0 = 1;
    DM(OL_TAP_SET,I5) = R0;
    // a dropped loop is gone when the crossfader has reached LIVE
    R0 = DM(OL_DROP,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_record;
    R0 = 0;
    COMP(F15,F0);
    IF NE JUMP _ol_record;
    DM(OL_LOOP,I5) = R0;
    DM(OL_DROP,I5) = R0;
    DM(OL_HELD,I5) = R0;
    DM(OL_CUT,I5) = R0;
    DM(OL_JUMP,I5) = R0;

    // ---- What was heard goes to the rolling record. Its next frame is a whole number of blocks, so a block never wraps.
_ol_record:
    I2 = DM(OL_OUT,I5);
    R0 = I2;
    R1 = 128;
    R0 = R0 + R1;
    I3 = R0;
    R7 = DM(OL_N,I5);
    M2 = R7;
    R2 = DM(OL_ROLL,I5);
    R3 = DM(OL_BUF0,I5);
    R4 = DM(OL_BUF1,I5);
    R2 = PASS R2;
    IF NE R3 = PASS R4;
    R0 = DM(OL_W,I5);
    R1 = LSHIFT R0 BY 2;
    R3 = R3 + R1;
    I4 = R3;
    LCNTR = DK_BLOCK, DO _ol_record_end UNTIL LCE;
        R4 = DM(I2,M6);
        R8 = DM(I3,M6);
        DM(M2,I4) = R8;
_ol_record_end: DM(I4,M6) = R4;
    R1 = DK_BLOCK;
    R0 = R0 + R1;
    R2 = 0;
    COMP(R0,R7);
    IF GE R0 = PASS R2;
    DM(OL_W,I5) = R0;
    R0 = DM(OL_VALID,I5);
    R0 = R0 + R1;
    R0 = MIN(R0,R7);
    DM(OL_VALID,I5) = R0;
    R0 = DM(OL_T,I5);
    R0 = R0 + R1;
    DM(OL_T,I5) = R0;

    // ---- The frames before the record's start. After a capture the record begins empty in this buffer, and what was
    // heard before lies in the other one, the loop's, which nothing writes. A block of it is copied each block, the newest
    // first, to the frames before the record's first one, until the record holds all of it or is full. R1 a block,
    // R7 and M2 the buffer's frames.
_ol_fill:
    R2 = DM(OL_FILL,I5);
    R2 = R2 + R1;
    R0 = DM(OL_FILL_MAX,I5);
    COMP(R2,R0);
    IF GT JUMP _ol_wet;
    R0 = DM(OL_VALID,I5);
    R0 = R0 + R1;
    COMP(R0,R7);
    IF GT JUMP _ol_wet;
    DM(OL_VALID,I5) = R0;
    DM(OL_FILL,I5) = R2;
    // from the block that ends OL_FILL frames before OL_FILL_END, to the block that ends as many before the buffer's end
    R0 = DM(OL_FILL_END,I5);
    R0 = R0 - R2;
    R4 = R0 + R7;
    R0 = PASS R0;
    IF LT R0 = PASS R4;
    R0 = LSHIFT R0 BY 2;
    R4 = DM(OL_LBUF,I5);
    R0 = R0 + R4;
    I0 = R0;
    R4 = DM(OL_ROLL,I5);
    R3 = DM(OL_BUF0,I5);
    R8 = DM(OL_BUF1,I5);
    R4 = PASS R4;
    IF NE R3 = PASS R8;
    R0 = R7 - R2;
    R0 = LSHIFT R0 BY 2;
    R0 = R0 + R3;
    I1 = R0;
    LCNTR = DK_BLOCK, DO _ol_fill_end UNTIL LCE;
        R8 = DM(M2,I0);
        R4 = DM(I0,M6);
        DM(M2,I1) = R8;
_ol_fill_end: DM(I1,M6) = R4;

    // ---- The send effect's output (the wet mailbox), behind the crossfader and after the record: what the master stage
    // would have made of it, its +10 dB and its volume ramp sample by sample (the gain before sample k is the ramp's start
    // plus k steps, added up as the stock code does). Its clip, compressor and overdrive are not applied. R7 is kept.
_ol_wet:
    R0 = DM(OL_WET_OK,I5);
    R0 = PASS R0;
    IF EQ JUMP _ol_publish;
    I4 = _fw_master_gain;
    R4 = DM(0,I4);
    I4 = _fw_master_step;
    R5 = DM(0,I4);
    R6 = OL_MASTER_BOOST;
    R11 = OL_WET_BOUND;
    R12 = OL_WET_FLOOR;
    R0 = _loop_wet;
    R1 = LW_LEFT * 4;
    R0 = R0 + R1;
    I0 = R0;
    I2 = DM(OL_OUT,I5);
    CALL _ol_wet_side;
    CALL _ol_wet_side;

    // ---- The tap's head and the status. The block-tail sender runs after this function on the same core.
_ol_publish:
    I4 = _loop_tap;
    R0 = LT_MAGIC_VALUE;
    DM(LT_MAGIC,I4) = R0;
    R0 = LT_VERSION_VALUE;
    DM(LT_VERSION,I4) = R0;
    R0 = DM(OL_BLOCK,I5);
    DM(LT_BLOCK,I4) = R0;
    R0 = DM(OL_TAP_SET,I5);
    R1 = DM(OL_COUNT,I5);
    R1 = LSHIFT R1 BY LT_COUNT_SHIFT;
    R0 = R0 OR R1;
    DM(LT_FLAGS,I4) = R0;
    I4 = _loop_ui_status;
    R0 = LS_MAGIC_VALUE;
    DM(LS_MAGIC,I4) = R0;
    R0 = DM(LS_HEARTBEAT,I4);
    R0 = R0 + 1;
    DM(LS_HEARTBEAT,I4) = R0;
    R1 = DM(OL_LOOP,I5);
    R0 = DM(OL_ARMED,I5);
    R0 = LSHIFT R0 BY 1;
    R1 = R1 OR R0;
    R0 = DM(OL_CUT,I5);
    R0 = LSHIFT R0 BY 2;
    R1 = R1 OR R0;
    R2 = DM(OL_NEXT,I5);
    R0 = 0;
    R3 = LS_TRACK;
    R2 = PASS R2;
    IF GT R0 = PASS R3;
    R1 = R1 OR R0;
    // the longest loop: a buffer less the seam and a block
    R4 = OL_SEAM + DK_BLOCK;
    R4 = R7 - R4;
    R0 = 0;
    R3 = LS_NEXT_CUT;
    COMP(R2,R4);
    IF GT R0 = PASS R3;
    R1 = R1 OR R0;
    R0 = DM(OL_HELD,I5);
    R0 = LSHIFT R0 BY 5;
    R1 = R1 OR R0;
    R0 = DM(OL_COUNT,I5);
    R0 = LSHIFT R0 BY LS_COUNT_SHIFT;
    R1 = R1 OR R0;
    DM(LS_STATE,I4) = R1;
    DM(LS_NEXT_FRAMES,I4) = R2;
    DM(LS_CAPACITY,I4) = R4;
    R0 = 0;
    DM(7,I4) = R0;
    DM(8,I4) = R0;
    DM(11,I4) = R0;
    DM(12,I4) = R0;
    DM(13,I4) = R0;
    DM(14,I4) = R0;
    DM(15,I4) = R0;
    R2 = 0;
    R3 = DM(OL_LOOP,I5);
    R3 = PASS R3;
    IF EQ JUMP _ol_status_loop;
    R0 = DM(OL_L,I5);
    R2 = DM(OL_STEPS,I5);
_ol_status_loop:
    DM(LS_LOOP_FRAMES,I4) = R0;
    R0 = DM(OL_PHASE,I5);
    DM(LS_PHASE,I4) = R0;
    R0 = DM(OL_GAIN,I5);
    DM(LS_GAIN,I4) = R0;
    R3 = DM(OL_RUN,I5);
    R3 = PASS R3;
    IF EQ JUMP _ol_status_steps;
    R0 = DM(OL_LEN,I5);
    R0 = LSHIFT R0 BY 8;
    R2 = R2 OR R0;
    R0 = DM(OL_STEP,I5);
    R0 = LSHIFT R0 BY 16;
    R2 = R2 OR R0;
_ol_status_steps:
    DM(LS_STEPS,I4) = R2;
    // this block is finished: the send effect sees it in its next block
    I4 = _loop_wet;
    R0 = DM(LW_TAKEN,I4);
    R0 = R0 + 1;
    DM(LW_TAKEN,I4) = R0;
    R0 = DK_FX_OK;
    JUMP (M13,I13);

_ol_bad:
    // the adapter's guard plays the input and resets the state
    R0 = -1;
    JUMP (M13,I13);

// A new engine (a load, a reset of the state, a lease that moved) goes on counting loops from the count its status mailbox
// shows, so a loop it takes has a count the home and the pages have not seen. Uses R0 to R2, I4.
_ol_count_on:
    I4 = _loop_ui_status;
    R0 = DM(LS_STATE,I4);
    R0 = LSHIFT R0 BY -LS_COUNT_SHIFT;
    R1 = DM(LS_MAGIC,I4);
    R2 = LS_MAGIC_VALUE;
    COMP(R1,R2);
    IF NE R0 = R0 - R0;
    DM(OL_COUNT,I5) = R0;
    RTS;

// The master stage's gain after its sum at the end of this block (OL_MASTER_BOOST times the volume ramp's end): its
// reciprocal, at most 1/OL_INV_FLOOR, is the tap's factor (OL_INV; 0 when the gain is not a number). Then the wet mailbox:
// OL_WET_OK is 1 when the send effect wrote a block since this engine's last one (not in the engine's first block) and every
// sample is finite and at most 8 in magnitude. Uses R0 to R4, I4.
_ol_master:
    I4 = _fw_master_gain;
    F0 = DM(0,I4);
    I4 = _fw_master_step;
    F1 = DM(0,I4);
    R2 = 0x42000000;
    F1 = F1 * F2;
    F0 = F0 + F1;
    R1 = OL_MASTER_BOOST;
    F0 = F0 * F1;
    R4 = 0;
    R1 = 0x7f800000;
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF EQ JUMP _ol_master_inv;
    R1 = OL_INV_FLOOR;
    COMP(R0,R1);
    IF LT R0 = PASS R1;
    CALL _ol_recip;
    R4 = PASS R0;
_ol_master_inv:
    DM(OL_INV,I5) = R4;
    R4 = 0;
    I4 = _loop_wet;
    R0 = DM(LW_MAGIC,I4);
    R1 = LW_MAGIC_VALUE;
    COMP(R0,R1);
    IF NE JUMP _ol_master_done;
    R0 = DM(LW_VERSION,I4);
    R1 = LW_VERSION_VALUE;
    COMP(R0,R1);
    IF NE JUMP _ol_master_done;
    R2 = DM(LW_SEQ,I4);
    R0 = DM(OL_WET_SEEN,I5);
    DM(OL_WET_SEEN,I5) = R2;
    R1 = DM(OL_WET_INIT,I5);
    R3 = 1;
    DM(OL_WET_INIT,I5) = R3;
    R1 = PASS R1;
    IF EQ JUMP _ol_master_done;
    COMP(R0,R2);
    IF EQ JUMP _ol_master_done;
    // the largest magnitude's bits: finite and at most 8.0
    R0 = _loop_wet;
    R1 = LW_LEFT * 4;
    R0 = R0 + R1;
    I4 = R0;
    R3 = 0;
    R1 = 0x7fffffff;
    LCNTR = 2 * DK_BLOCK, DO _ol_master_scan UNTIL LCE;
        R0 = DM(I4,M6);
        R0 = R0 AND R1;
_ol_master_scan: R3 = MAX(R3,R0);
    R0 = 0x41000000;
    COMP(R3,R0);
    IF GT JUMP _ol_master_done;
    R4 = 1;
_ol_master_done:
    DM(OL_WET_OK,I5) = R4;
    RTS;

// One side of the send effect's output: I0 the mailbox (read), I2 the output (read and write), each advanced by 32. F4 the
// ramp's gain before the first sample, F5 its step, F6 the +10 dB stage, F11 and F12 the output's bounds. Uses F0, F1, F3.
_ol_wet_side:
    F3 = PASS F4;
    LCNTR = DK_BLOCK, DO _ol_wet_sample UNTIL LCE;
        F0 = DM(I0,M6);
        F0 = F0 * F6;
        F0 = F0 * F3;
        F1 = DM(I2,M5);
        F1 = F1 + F0;
        F1 = MIN(F1,F11);
        F1 = MAX(F1,F12);
        DM(I2,M6) = R1;
_ol_wet_sample: F3 = F5 + F3;
    RTS;

// R0 a byte address, R8 bytes: R0 = 0 when [R0, R0 + R8) lies in external memory, else 1. Uses R1 and R4.
_ol_range:
    R1 = 0x80000000;
    R4 = R0 XOR R1;
    R0 = 1;
    R4 = PASS R4;
    IF LT RTS;
    R1 = 0x20000000;
    COMP(R4,R1);
    IF GE RTS;
    R1 = R1 - R4;
    COMP(R8,R1);
    IF GT RTS;
    R0 = 0;
    RTS;

// F0 = 1 / F0. Uses R1, R2 and R3.
_ol_recip:
    F1 = RECIPS F0;
    R3 = 0x40000000;
    F2 = F0 * F1;
    F2 = F3 - F2;
    F1 = F1 * F2;
    F2 = F0 * F1;
    F2 = F3 - F2;
    F0 = F1 * F2;
    RTS;

// OL_NEXT, what an armed change keeps as the run stands before this boundary is counted: the last whole cycle, or
// without one the frames a step has taken on average times the pattern's steps. Uses R0 to R4.
_ol_next:
    R0 = DM(OL_CYC_LEN,I5);
    R0 = PASS R0;
    IF NE JUMP _ol_next_set;
    R0 = DM(OL_RUN_N,I5);
    F0 = FLOAT R0;
    CALL _ol_recip;
    R1 = DM(OL_T,I5);
    R2 = DM(OL_RUN_T,I5);
    R1 = R1 - R2;
    F1 = FLOAT R1;
    F0 = F0 * F1;
    R1 = DM(OL_LEN,I5);
    F1 = FLOAT R1;
    F0 = F0 * F1;
    R1 = 0x3f000000;
    F0 = F0 + F1;
    R0 = TRUNC F0;
_ol_next_set:
    DM(OL_NEXT,I5) = R0;
    RTS;

// The loop is the last OL_NEXT frames of the rolling record, at most what a buffer holds with the seam before it. The
// crossfader stands at LOOP from this block on, the arm is used up, and the record goes on in the other buffer.
// Uses R0 to R6 and R9.
_ol_capture:
    R0 = DM(OL_NEXT,I5);
    R9 = DM(OL_N,I5);
    R1 = OL_SEAM + DK_BLOCK;
    R2 = R9 - R1;
    R3 = 0;
    R4 = 1;
    COMP(R0,R2);
    IF LE JUMP _ol_capture_fits;
    R0 = PASS R2;
    R3 = PASS R4;
_ol_capture_fits:
    DM(OL_CUT,I5) = R3;
    DM(OL_L,I5) = R0;
    R5 = PASS R0;
    R1 = LSHIFT R0 BY -2;
    R2 = OL_SEAM;
    R1 = MIN(R1,R2);
    R2 = 1;
    R1 = MAX(R1,R2);
    DM(OL_X,I5) = R1;
    F0 = FLOAT R1;
    CALL _ol_recip;
    DM(OL_XINV,I5) = R0;
    R2 = DM(OL_ROLL,I5);
    R3 = DM(OL_BUF0,I5);
    R4 = DM(OL_BUF1,I5);
    R2 = PASS R2;
    IF NE R3 = PASS R4;
    DM(OL_LBUF,I5) = R3;
    R0 = DM(OL_W,I5);
    R0 = R0 - R5;
    R0 = PASS R0;
    IF GE JUMP _ol_capture_base;
    R0 = R0 + R9;
_ol_capture_base:
    DM(OL_BASE,I5) = R0;
    // frames that were never recorded are silence, and without the frames before the start the seam fades out
    R6 = DM(OL_VALID,I5);
    R0 = R5 - R6;
    R1 = 0;
    R0 = MAX(R0,R1);
    DM(OL_SILENT,I5) = R0;
    R0 = DM(OL_X,I5);
    R0 = R0 + R5;
    R2 = 1;
    COMP(R6,R0);
    IF LT R2 = PASS R1;
    DM(OL_PRE,I5) = R2;
    // the record goes on in the other buffer, empty: what this one holds is filled in there (_ol_fill)
    R0 = DM(OL_W,I5);
    DM(OL_FILL_END,I5) = R0;
    DM(OL_FILL_MAX,I5) = R6;
    R0 = 0;
    DM(OL_FILL,I5) = R0;
    R0 = DM(OL_LEN,I5);
    DM(OL_STEPS,I5) = R0;
    R0 = DM(OL_COUNT,I5);
    R0 = R0 + 1;
    DM(OL_COUNT,I5) = R0;
    R0 = 1;
    DM(OL_LOOP,I5) = R0;
    DM(OL_HELD,I5) = R0;
    R0 = 0x3f800000;
    DM(OL_GAIN,I5) = R0;
    R0 = 0;
    DM(OL_PHASE,I5) = R0;
    DM(OL_DROP,I5) = R0;
    DM(OL_JUMP,I5) = R0;
    DM(OL_ARMED,I5) = R0;
    DM(OL_W,I5) = R0;
    DM(OL_VALID,I5) = R0;
    R0 = DM(OL_ROLL,I5);
    R1 = 1;
    R0 = R0 XOR R1;
    DM(OL_ROLL,I5) = R0;
    RTS;

.SECTION/DM seg_dmda;
.GLOBAL _bl_desc;
.VAR _bl_desc = DK_FX_MAGIC;
.VAR _bl_desc1 = DK_FX_V1;
.VAR _bl_desc2 = LP_COUNT;
.VAR _bl_desc3 = OL_STATE_BYTES;
.VAR _bl_desc4 = 2;
.VAR _bl_desc5 = 0;
