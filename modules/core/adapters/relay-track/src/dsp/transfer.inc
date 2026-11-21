// One outstanding file transaction. A SAVE pin remains valid across a track edit.
.EXTERN _relay_request;
.EXTERN _relay_response;
#include "dk_relay_transfer.inc"
_rt_transfer:
    R0 = DM(_rt_peak_bins); R0 = PASS R0; IF NE JUMP _rt_peak_slice;
    I4 = _relay_request; R0 = DM(0,I4); R1 = RX_MAGIC; COMP(R0,R1); IF NE RTS;
    R1 = DM(1,I4); R0 = DM(_re_xseq); COMP(R0,R1); IF EQ RTS;
    DM(_re_xseq) = R1;
    R5 = DM(2,I4); R8 = DM(3,I4); R12 = DM(4,I4);
    R0 = RX_PEAKS; COMP(R5,R0); IF EQ JUMP _rt_peak_batch;
    R0 = RX_PEAK; COMP(R5,R0); IF EQ JUMP _rt_xpeak;
    R0 = RX_READ; COMP(R5,R0); IF EQ JUMP _re_xread;
    R0 = RX_WRITE; COMP(R5,R0); IF EQ JUMP _re_xwrite;
    R0 = RX_UNPIN; COMP(R5,R0); IF EQ JUMP _re_xunpin;
    R0 = RX_ABORT; COMP(R5,R0); IF EQ JUMP _re_xabort;
    R0 = RX_COMMIT; COMP(R5,R0); IF EQ JUMP _re_xcommit;
    R0 = 15; R1 = RX_PIN; COMP(R5,R1); IF NE JUMP _rt_xrange;
    R0 = 47; // MAIN, T1 and T2 are consecutive spans of 16 roots.
_rt_xrange: COMP(R8,R0); IF GT JUMP _re_xfail;
    R8 = PASS R8; IF LT JUMP _re_xfail;
    DM(_re_xbank) = R8;
    R0 = RX_PIN; COMP(R5,R0); IF EQ JUMP _re_xpin;
    R0 = RX_LOAD; COMP(R5,R0); IF NE JUMP _re_xfail;
    R0 = DM(_rt_mode); R0 = PASS R0; IF NE JUMP _re_xfail;
    R0 = DM(_rt_load); R0 = PASS R0; IF GE JUMP _re_xfail;
    R12 = PASS R12; IF LE JUMP _re_xfail;
    DM(_re_xgoal) = R12; R8 = R12; R4 = RLY_BEGIN; CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _re_xfail;
    DM(_re_xpos) = R0; R1 = DM(_re_xbank); DM(_rt_load) = R1; JUMP _re_xreply;
_re_xpin:
    R0 = DM(_rt_mode); R0 = PASS R0; IF EQ JUMP _rt_pin_ready;
    R1 = DM(_rt_writer); COMP(R8,R1); IF EQ JUMP _re_xfail;
_rt_pin_ready:
    R1 = RLY_TRACK_MAIN; R8 = R1 + R8; R4 = RLY_PIN; CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _re_xfail;
    R8 = DM(_re_xbank); DM(_rt_pin) = R8; R1 = RLY_TRACK_MAIN; R8 = R1 + R8;
    R4 = RLY_LENGTH; CALL _relay_pool; DM(_re_pin_length) = R0;
    R8 = DM(_re_xbank); R1 = 15; R8 = R8 AND R1; CALL _rt_row; CALL _tape_row;
    R1 = DM(RT_REVISION,I4); DM(_re_pin_revision) = R1; I3 = _relay_response; DM(3,I3) = R1;
    R1 = DM(T_GRID,I5); DM(4,I3) = R1; R0 = DM(_re_pin_length); JUMP _re_xreply;
_re_xunpin:
    R8 = DM(_rt_pin); R0 = 16; COMP(R8,R0); IF GE JUMP _rt_unpin_done;
    R8 = PASS R8; IF LT JUMP _rt_unpin_done;
    R12 = PASS R12; IF EQ JUMP _rt_unpin_done;
    CALL _rt_row; R1 = DM(RT_REVISION,I4); R0 = DM(_re_pin_revision); COMP(R0,R1); IF NE JUMP _rt_unpin_done;
    R0 = 0; DM(RT_DIRTY,I4) = R0;
_rt_unpin_done:
    R4 = RLY_UNPIN; CALL _relay_pool; R1 = -1; DM(_rt_pin) = R1; JUMP _re_xreply;
// One bounded pool span per audio block. Accumulate into private storage until the
// whole reply is ready: the response mailbox can still be serving the previous id.
_rt_peak_batch:
    R0 = DM(_rt_pin); R0 = PASS R0; IF LT JUMP _re_xfail;
    R0 = 10; COMP(R12,R0); IF GT JUMP _re_xfail;
    R12 = PASS R12; IF LE JUMP _re_xfail;
    DM(_rt_peak_count) = R12;
    R0 = 0; DM(_rt_peak_index) = R0; DM(_rt_peak_max) = R0;
    R0 = DM(_re_pin_length); DM(_rt_peak_pos) = R8;
    R1 = I4; R2 = 20; R1 = R1 + R2; I1 = R1; I2 = _rt_peak_ends;
    R3 = R12;
_rt_peak_copy:
    R1 = DM(I1,M6); COMP(R1,R8); IF LE JUMP _rt_peak_fail;
    COMP(R1,R0); IF GT JUMP _rt_peak_fail;
    R8 = R1; DM(I2,M6) = R1; R3 = R3 - 1; IF NE JUMP _rt_peak_copy;
    DM(_rt_peak_bins) = R12;
_rt_peak_slice:
    R12 = DM(_rt_peak_pos); R8 = RLY_PINNED; R4 = RLY_READ_SPAN; CALL _relay_pool;
    R2 = -1; COMP(R0,R2); IF EQ JUMP _rt_peak_fail;
    I0 = R0; R2 = DM(_rt_peak_index); R2 = LSHIFT R2 BY 2; R3 = _rt_peak_ends; R2 = R2 + R3; I2 = R2;
    R2 = DM(0,I2); R12 = DM(_rt_peak_pos); R2 = R2 - R12;
    R2 = MIN(R1,R2); R3 = RX_PEAK_FRAMES; R2 = MIN(R2,R3);
    R3 = DM(_rt_peak_max); CALL _rt_max_stereo;
    R12 = R12 + R2; DM(_rt_peak_pos) = R12;
    R0 = DM(0,I2); COMP(R12,R0); IF EQ JUMP _rt_peak_finish_bin;
    DM(_rt_peak_max) = R3; RTS;
_rt_peak_finish_bin:
    R0 = DM(_rt_peak_index); R1 = LSHIFT R0 BY 2; R2 = _rt_peak_result; R1 = R1 + R2; I1 = R1;
    DM(0,I1) = R3; R0 = R0 + 1; DM(_rt_peak_index) = R0;
    R0 = 0; DM(_rt_peak_max) = R0;
    R1 = DM(_rt_peak_bins); R1 = R1 - 1; DM(_rt_peak_bins) = R1; IF NE RTS;
    I0 = _rt_peak_result; I1 = _relay_response; R2 = I1; R3 = 12; R2 = R2 + R3; I1 = R2;
    R0 = DM(_rt_peak_count); LCNTR = R0, DO _rt_peak_publish UNTIL LCE;
        R1 = DM(I0,M6);
_rt_peak_publish: DM(I1,M6) = R1;
    JUMP _re_xreply;
_rt_peak_fail:
    R0 = 0; DM(_rt_peak_bins) = R0; JUMP _re_xfail;
_rt_xpeak:
    R0 = DM(_rt_pin); R0 = PASS R0; IF LT JUMP _re_xfail;
    R0 = RX_PEAK_FRAMES; COMP(R12,R0); IF GT JUMP _re_xfail;
    R12 = PASS R12; IF LE JUMP _re_xfail;
    DM(_re_xcount) = R12;
    R12 = R8; R8 = RLY_PINNED; R4 = RLY_READ_SPAN; CALL _relay_pool;
    R2 = -1; COMP(R0,R2); IF EQ JUMP _re_xfail;
    I0 = R0; R2 = DM(_re_xcount); R2 = MIN(R1,R2); R3 = 0;
    CALL _rt_max_stereo;
    I3 = _relay_response; DM(3,I3) = R3; R0 = R2; JUMP _re_xreply;
_rt_max_stereo:
    LCNTR = R2, DO _rt_xpeak_word UNTIL LCE;
        R0 = DM(I0,M6);
        R1 = ASHIFT R0 BY -16;
        R0 = LSHIFT R0 BY 16;
        R0 = ASHIFT R0 BY -16;
        R1 = ABS R1;
        R0 = ABS R0;
        R3 = MAX(R3,R1);
_rt_xpeak_word: R3 = MAX(R3,R0);
    RTS;
_re_xread:
    R0 = DM(_rt_pin);
    R0 = PASS R0;
    IF LT JUMP _re_xfail;
    DM(_re_xbase) = R8;
    R0 = 10;
    R12 = MIN(R12,R0);
    R12 = PASS R12;
    IF LE JUMP _re_xfail;
    DM(_re_xcount) = R12;
    R0 = 0;
    DM(_re_xidx) = R0;
_re_xread_word:
    R4 = RLY_READ_SPAN;
    R8 = RLY_PINNED;
    R12 = DM(_re_xbase);
    R1 = DM(_re_xidx);
    R12 = R12 + R1;
    CALL _relay_pool;
    R1 = -1;
    COMP(R0,R1);
    IF EQ JUMP _re_xfail;
    I0 = R0;
    R2 = DM(0,I0);
    R0 = DM(_re_xidx);
    R1 = LSHIFT R0 BY 2;
    R3 = _relay_response;
    R1 = R1 + R3;
    I0 = R1;
    DM(3,I0) = R2;
    R0 = R0 + 1;
    DM(_re_xidx) = R0;
    R1 = DM(_re_xcount);
    COMP(R0,R1);
    IF LT JUMP _re_xread_word;
    JUMP _re_xreply;
_re_xwrite:
    R0 = DM(_rt_load);
    R0 = PASS R0;
    IF LT JUMP _re_xfail;
    R1 = DM(_re_xpos);
    COMP(R1,R8);
    IF NE JUMP _re_xfail;
    R0 = 10;
    COMP(R12,R0);
    IF GT JUMP _re_xfail;
    R12 = PASS R12;
    IF LE JUMP _re_xfail;
    R0 = R8 + R12;
    R1 = DM(_re_xgoal);
    COMP(R0,R1);
    IF GT JUMP _re_xfail;
    DM(_re_xcount) = R12;
    R0 = 0;
    DM(_re_xidx) = R0;
_re_xwrite_word:
    R0 = DM(_re_xidx);
    R0 = LSHIFT R0 BY 2;
    R1 = _relay_request;
    R0 = R0 + R1;
    I0 = R0;
    R8 = DM(5,I0);
    R4 = RLY_APPEND;
    CALL _relay_pool;
    R0 = PASS R0;
    IF LT JUMP _re_xabort_fail;
    DM(_re_xpos) = R0;
    R0 = DM(_re_xidx);
    R0 = R0 + 1;
    DM(_re_xidx) = R0;
    R1 = DM(_re_xcount);
    COMP(R0,R1);
    IF LT JUMP _re_xwrite_word;
    JUMP _re_xreply;

_re_xcommit:
    R8 = DM(_rt_load); R8 = PASS R8; IF LT JUMP _re_xfail;
    R0 = DM(_re_xpos); R1 = DM(_re_xgoal); COMP(R0,R1); IF NE JUMP _re_xabort_fail;
    R1 = RLY_TRACK_MAIN; R8 = R1 + R8; R4 = RLY_COMMIT; CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _re_xabort_fail;
    R8 = DM(_rt_load); R1 = RLY_TRACK_T1; R8 = R1 + R8; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_load); R1 = RLY_TRACK_T2; R8 = R1 + R8; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_load); R1 = RLY_TRACK_UNDO; R8 = R1 + R8; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_load); CALL _rt_row;
    R0 = RLY_TRACK_MAIN; R0 = R0 + R8; DM(RT_SLOT,I4) = R0;
    R0 = DM(_re_xgoal); DM(RT_LENGTH,I4) = R0;
    R0 = DM(RT_REVISION,I4); R0 = R0 + 1; DM(RT_REVISION,I4) = R0;
    R0 = 0; DM(RT_POS,I4) = R0; DM(RT_PLAYING,I4) = R0; DM(RT_LANES,I4) = R0;
    DM(RT_UNDONE,I4) = R0; DM(RT_UNDOABLE,I4) = R0; DM(RT_ARM_PLAY,I4) = R0;
    DM(RT_DIRTY,I4) = R0; DM(RT_AUDITION,I4) = R0; DM(RT_PREVIEW_END,I4) = R0;
    CALL _tape_row; DM(T_SLIP,I5) = R0; DM(T_FRAC,I5) = R0;
    R1 = -1; DM(_rt_load) = R1; JUMP _re_xreply;
_re_xabort:
    R0 = DM(_rt_load);
    R0 = PASS R0;
    IF LT JUMP _re_xfail;
    R4 = RLY_ABORT;
    CALL _relay_pool;
    R1 = -1;
    DM(_rt_load) = R1;
    JUMP _re_xreply;
_re_xabort_fail:
    R4 = RLY_ABORT;
    CALL _relay_pool;
    R1 = -1;
    DM(_rt_load) = R1;
_re_xfail:
    R0 = -1;
_re_xreply:
    I3 = _relay_response;
    DM(2,I3) = R0;
    R1 = DM(_re_xseq);
    DM(1,I3) = R1;
    R2 = I3; R3 = 8; R2 = R2 + R3; I0 = R2;
    LCNTR = 11, DO _rt_xcheck UNTIL LCE;
        R2 = LSHIFT R1 BY 5;
        R2 = R2 - R1;
        R3 = DM(I0,M6);
_rt_xcheck: R1 = R2 + R3;
    DM(13,I3) = R1;
    R1 = RX_REPLY;
    DM(0,I3) = R1;
    RTS;


.SECTION/DM seg_dmda;
.VAR _re_pin_length;
.VAR _re_pin_revision;
.VAR _rt_peak_bins;
.VAR _rt_peak_count;
.VAR _rt_peak_index;
.VAR _rt_peak_pos;
.VAR _rt_peak_max;
.VAR _rt_peak_ends[10];
.VAR _rt_peak_result[10];
.VAR _re_xseq;
.VAR _re_xbank;
.VAR _re_xpos;
.VAR _re_xgoal;
.VAR _re_xbase;
.VAR _re_xcount;
.VAR _re_xidx;
