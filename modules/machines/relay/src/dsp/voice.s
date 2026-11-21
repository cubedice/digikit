// Track identity comes from the native voice ABI. The recorder owns transport
// and advances phase once per audio block, including while the AMP is closed.
#include "dk_machine.inc"
#include "dk_relay.inc"
#include "dk_relay_track.inc"
.EXTERN _rtm_tracks;
.EXTERN _rtm_tapes;
.SECTION/PM seg_pmco;
.EXTERN _relay_pool;
.GLOBAL _relay_note_on;
.GLOBAL _relay_render;
    NOP; // Offset zero is reserved for an absent note-on callback.
_relay_note_on:
    I0 = R8;
    R1 = DM(DK_IN_W_TRACK,I0);
    I1 = R4;
    R0 = 0;
    DM(5,I1) = R0; DM(6,I1) = R0;
    R2 = 15;
    COMP(R1,R2);
    IF GT JUMP _rv_note_done;
    R1 = PASS R1;
    IF LT JUMP _rv_note_done;
    R2 = LSHIFT R1 BY T_STRIDE_SHIFT; R3 = _rtm_tapes; R2 = R2 + R3; DM(6,I1) = R2;
    R1 = LSHIFT R1 BY 6;
    R2 = _rtm_tracks;
    R1 = R1 + R2;
    DM(5,I1) = R1;
    I0 = R1;
    // A panel launch already chose the loop phase (or slice offset).
    R1 = DM(RT_LAUNCH,I0);
    DM(RT_LAUNCH,I0) = R0;
    R1 = PASS R1;
    IF NE JUMP _rv_note_done;
    R1 = DM(RT_LENGTH,I0); R1 = PASS R1; IF EQ JUMP _rv_note_done;
    R2 = DM(6,I1); I2 = R2; R2 = DM(T_ACTIVE,I2); R2 = PASS R2; IF EQ JUMP _rv_legacy_note;
    R2 = DM(RT_PLAYING,I0); R2 = PASS R2; IF NE JUMP _rv_tape_reset;
    R2 = DM(T_STOPPED,I2); R2 = PASS R2; IF NE JUMP _rv_note_done;
_rv_tape_reset:
    DM(T_FRAC,I2) = R0; DM(T_SLIP,I2) = R0; DM(RT_PREVIEW_END,I0) = R0;
    R2 = DM(T_LAST_L,I2); DM(12,I2) = R2; R2 = DM(T_LAST_R,I2); DM(13,I2) = R2;
    R2 = 16; DM(T_RESET,I2) = R2; JUMP _rv_reset;
_rv_legacy_note:
    R1 = DM(RT_TRIG,I0);
    R1 = PASS R1;
    IF NE JUMP _rv_note_done;
_rv_reset:
    DM(RT_POS,I0) = R0;
    R1 = 1;
    DM(RT_PLAYING,I0) = R1;
_rv_note_done:
    R0 = 0;
    JUMP (M13,I13);

_relay_render:
    // The pool uses I2..I5 and R0..R4/R6..R8/R10/R12/R15.
    // Its bounded read calls leave R5 (state) and R9 (remaining) intact.
    R5 = R4;
    I0 = R8;
    R0 = DM(DK_IN_W_OUT,I0);
    R1 = DM(DK_IN_W_OUT_R,I0);
    I1 = R5;
    DM(2,I1) = R0;
    DM(3,I1) = R1;
    R0 = DM(5,I1);
    R0 = PASS R0;
    IF EQ JUMP _rv_empty;
    I0 = R0;
    R2 = DM(6,I1); I2 = R2; R2 = DM(T_ACTIVE,I2); R2 = PASS R2; IF NE JUMP _rv_tape;
    R0 = DM(RT_PLAYING,I0);
    R0 = PASS R0;
    IF EQ JUMP _rv_empty;
    R8 = DM(RT_SLOT,I0);
    DM(0,I1) = R8;
    R0 = DM(RT_POS,I0);
    DM(1,I1) = R0;
    R4 = RLY_LENGTH;
    CALL _relay_pool;
    R0 = PASS R0;
    IF LE JUMP _rv_empty;
    I1 = R5;
    DM(4,I1) = R0;
    R1 = DM(1,I1);
    COMP(R1,R0);
    IF GE R1 = R1 XOR R1;
    DM(1,I1) = R1;
    R9 = 32;
_rv_span:
    I1 = R5;
    R8 = DM(0,I1);
    R12 = DM(1,I1);
    R4 = RLY_READ_SPAN;
    CALL _relay_pool;
    R2 = -1;
    COMP(R0,R2);
    IF EQ JUMP _rv_silence_tail;
    R2 = MIN(R1,R9);
    I0 = R0;
    I1 = R5;
    R0 = DM(2,I1);
    I2 = R0;
    R0 = DM(3,I1);
    I3 = R0;
    R4 = 0x38000000; // signed PCM16 to unity float
    LCNTR = R2, DO _rv_sample UNTIL LCE;
        R0 = DM(I0,M6);
        R1 = ASHIFT R0 BY -16;
        R0 = LSHIFT R0 BY 16;
        R0 = ASHIFT R0 BY -16;
        F1 = FLOAT R1;
        F0 = FLOAT R0;
        F1 = F1 * F4;
        F0 = F0 * F4;
        DM(I2,M6) = R1;
_rv_sample: DM(I3,M6) = R0;
    R0 = I2; DM(2,I1) = R0;
    R0 = I3; DM(3,I1) = R0;
    R0 = DM(1,I1);
    R0 = R0 + R2;
    R1 = DM(4,I1);
    COMP(R0,R1);
    IF GE R0 = R0 XOR R0;
    DM(1,I1) = R0;
    R9 = R9 - R2;
    IF NE JUMP _rv_span;
    R0 = DK_STEREO;
    JUMP (M13,I13);
_rv_tape:
    // The motor can stop inside this block. Deliver its last samples and fade.
    R0 = DM(RT_PLAYING,I0); R1 = DM(T_MONITOR,I2); R0 = R0 OR R1;
    R1 = DM(T_TAIL_BLOCK,I2); R0 = R0 OR R1; IF EQ JUMP _rv_empty;
    R0 = I2; R1 = 128; R0 = R0 + R1; I0 = R0; R0 = R0 + R1; I2 = R0;
    R0 = DM(2,I1); I3 = R0; R0 = DM(3,I1); I1 = R0;
    R2 = 32; LCNTR = R2, DO _rv_tape_sample UNTIL LCE;
        R0 = DM(I0,M6); R1 = DM(I2,M6); DM(I3,M6) = R0;
_rv_tape_sample: DM(I1,M6) = R1;
    R0 = DK_STEREO; JUMP (M13,I13);
_rv_empty:
    R9 = 32;
_rv_silence_tail:
    I1 = R5;
    R0 = DM(2,I1);
    I2 = R0;
    R0 = DM(3,I1);
    I3 = R0;
    R0 = 0;
    LCNTR = R9, DO _rv_clear UNTIL LCE;
        DM(I2,M6) = R0;
_rv_clear: DM(I3,M6) = R0;
    R0 = 3; // DK_DONE | DK_STEREO
    JUMP (M13,I13);

.SECTION/DM seg_dmda;
.GLOBAL _relay_desc;
.VAR _relay_desc = DK_MAGIC;
.VAR _rv_contract = 257; // DK_MACHINE_V1 | DK_MACHINE_STEREO
.VAR _rv_params = 0;
.VAR _rv_voices = DK_MAX_VOICES;
.VAR _rv_state_bytes = 28;
.VAR _rv_clear_bytes = 28;
.VAR _rv_depth = 2;
.VAR _rv_stack = 0;
