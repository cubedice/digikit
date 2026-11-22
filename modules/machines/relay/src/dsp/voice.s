// Track identity comes from the native voice ABI. The recorder owns transport
// and advances phase once per audio block, including while the AMP is closed.
#include "dk_machine.inc"
#include "dk_relay.inc"
#include "dk_relay_track.inc"
.EXTERN _rtm_tracks;
.EXTERN _rtm_tapes;
.EXTERN _rtm_read_head_owner;
.EXTERN _rtm_group_amp_done;
.SECTION/PM seg_pmco;
.EXTERN _relay_pool;
.EXTERN _dv_exp2;
.GLOBAL _relay_note_on;
.GLOBAL _relay_render;
#define RV_OWNER 7
#define RV_FRACTION 8
#define RV_REVISION 9
#define RV_RATIO 10
#define RV_STEP 11
#define RV_GAIN 12

    NOP; // Offset zero is reserved for an absent note-on callback.
_relay_note_on:
    I0 = R8;
    R1 = DM(DK_IN_W_TRACK,I0);
    I1 = R4;
    R0 = 0;
    DM(5,I1) = R0; DM(6,I1) = R0; DM(RV_OWNER,I1) = R0;
    R2 = 15;
    COMP(R1,R2);
    IF GT JUMP _rv_note_done;
    R1 = PASS R1;
    IF LT JUMP _rv_note_done;
    R9 = R1;
    CALL _rtm_read_head_owner;
    R0 = PASS R0;
    IF GE JUMP _rv_group_note;
    R1 = R9;
_rv_bind_motor:
    R0 = 0;
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
    // During a punch the recorder alone advances the writer's position.
    R1 = DM(RT_SLOT,I0); R2 = RLY_WRITER; COMP(R1,R2);
    IF EQ JUMP _rv_note_done;
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

_rv_group_note:
    R6 = R0;
    R2 = LSHIFT R6 BY 6;
    R3 = _rtm_tracks;
    R2 = R2 + R3;
    I0 = R2;
    COMP(R6,R9);
    IF NE JUMP _rv_start_head;
    // Only the real owner can acknowledge a panel or recorder gate.
    R0 = DM(RT_LAUNCH,I0);
    R0 = PASS R0;
    IF EQ JUMP _rv_start_head;
    R1 = R9;
    JUMP _rv_bind_motor;
_rv_start_head:
    DM(5,I1) = R2;
    R2 = LSHIFT R6 BY T_STRIDE_SHIFT;
    R3 = _rtm_tapes;
    R2 = R2 + R3;
    DM(6,I1) = R2;
    R2 = R6 + 1;
    DM(RV_OWNER,I1) = R2;
    R2 = DM(RT_REVISION,I0);
    DM(RV_REVISION,I1) = R2;
    CALL _rv_head_slot;
    DM(0,I1) = R0;
    R0 = 0;
    DM(1,I1) = R0;
    DM(RV_FRACTION,I1) = R0;
    I0 = R8;
    R0 = DM(DK_IN_W_PITCH,I0);
    R1 = 15360; // C5 is the native neutral pitch, in 1/256 semitones.
    R0 = R0 - R1;
    F0 = FLOAT R0;
    R1 = 0x39aaaaab; // 1 / (12 * 256)
    F0 = F0 * F1;
    R1 = 0x40000000;
    F0 = MIN(F0,F1);
    R1 = 0xc0000000;
    F0 = MAX(F0,F1);
    CALL _dv_exp2;
    DM(RV_RATIO,I1) = R0;
    JUMP _rv_note_done;

// In: I0 owner row, R6 zero-based owner. Select the committed, audible
// bank belonging to this track. While punching, MAIN is still immutable.
_rv_head_slot:
    R0 = DM(RT_SLOT,I0);
    R1 = RLY_WRITER;
    COMP(R0,R1);
    IF NE JUMP _rv_check_bank;
    R1 = RLY_TRACK_MAIN;
    R0 = R6 + R1;
    RTS;
_rv_check_bank:
    R1 = RLY_TRACK_MAIN;
    R1 = R6 + R1;
    R1 = R0 - R1;
    IF EQ RTS;
    R2 = 16;
    COMP(R1,R2);
    IF EQ RTS;
    R2 = 32;
    COMP(R1,R2);
    IF EQ RTS;
    R0 = -1;
    RTS;

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
    R0 = DM(RV_OWNER,I1);
    R0 = PASS R0;
    IF NE JUMP _rv_render_head;
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

_rv_render_head:
    R6 = R0 - 1;
    I2 = R8;
    R0 = DM(DK_IN_W_GATE,I2);
    R0 = PASS R0;
    IF NE JUMP _rv_head_validate;
    R1 = DM(DK_IN_W_TRACK,I2);
    CALL _rtm_group_amp_done;
    R0 = PASS R0;
    IF NE JUMP _rv_empty;
_rv_head_validate:
    R1 = DM(RV_REVISION,I1);
    R0 = DM(RT_REVISION,I0);
    COMP(R0,R1);
    IF NE JUMP _rv_empty;
    CALL _rv_head_slot;
    R8 = DM(0,I1);
    COMP(R0,R8);
    IF NE JUMP _rv_empty;
    R0 = PASS R0;
    IF LT JUMP _rv_empty;
    R4 = RLY_LENGTH;
    CALL _relay_pool;
    R1 = 4; // A bounded ±4-frame increment needs a single wrap at most.
    COMP(R0,R1);
    IF LT JUMP _rv_empty;
    I1 = R5;
    DM(4,I1) = R0;
    R0 = DM(6,I1);
    I2 = R0;
    R0 = 65536;
    R1 = 0x3f800000;
    R2 = DM(T_ACTIVE,I2);
    R2 = PASS R2;
    IF EQ JUMP _rv_head_scalars;
    R0 = DM(T_EFF_RATE,I2);
    R1 = DM(T_EFF_MIX,I2);
_rv_head_scalars:
    F0 = FLOAT R0;
    F2 = DM(RV_RATIO,I1);
    F0 = F0 * F2;
    R2 = 0xc8800000; // -4.0 frames/sample in Q16.16
    F0 = MAX(F0,F2);
    R2 = 0x48800000;
    F0 = MIN(F0,F2);
    R0 = FIX F0;
    DM(RV_STEP,I1) = R0;
    R2 = 0x37800000; // PCM16 to float, with -6 dB for two heads.
    F1 = F1 * F2;
    DM(RV_GAIN,I1) = R1;
    R9 = 32;
    R13 = -1; // Resolve read-only pointers afresh every block.
_rv_head_sample:
    I1 = R5;
    R12 = DM(1,I1);
    R0 = LSHIFT R12 BY -8;
    COMP(R0,R13);
    IF EQ JUMP _rv_head_read;
    R13 = R0;
    R12 = LSHIFT R13 BY 8;
    R8 = DM(0,I1);
    R4 = RLY_PEEK_SPAN;
    CALL _relay_pool;
    R2 = -1;
    COMP(R0,R2);
    IF EQ JUMP _rv_silence_tail;
    R11 = R0;
    I1 = R5;
    R12 = DM(1,I1);
_rv_head_read:
    R0 = PASS R11;
    IF EQ JUMP _rv_head_pcm;
    R0 = FEXT R12 BY 0:8;
    R0 = LSHIFT R0 BY 2;
    R0 = R11 + R0;
    I0 = R0;
    R0 = DM(0,I0);
_rv_head_pcm:
    R1 = ASHIFT R0 BY -16;
    R0 = LSHIFT R0 BY 16;
    R0 = ASHIFT R0 BY -16;
    F1 = FLOAT R1;
    F0 = FLOAT R0;
    F4 = DM(RV_GAIN,I1);
    F1 = F1 * F4;
    F0 = F0 * F4;
    R2 = DM(2,I1);
    I2 = R2;
    R2 = DM(3,I1);
    I3 = R2;
    DM(I2,M6) = R1;
    DM(I3,M6) = R0;
    R0 = I2; DM(2,I1) = R0;
    R0 = I3; DM(3,I1) = R0;
    R0 = DM(RV_FRACTION,I1);
    R1 = DM(RV_STEP,I1);
    R0 = R0 + R1;
    R1 = FEXT R0 BY 0:16;
    DM(RV_FRACTION,I1) = R1;
    R0 = ASHIFT R0 BY -16;
    R12 = R12 + R0;
    R0 = DM(4,I1);
    R12 = PASS R12;
    IF LT R12 = R12 + R0;
    COMP(R12,R0);
    IF GE R12 = R12 - R0;
    DM(1,I1) = R12;
    R9 = R9 - 1;
    IF NE JUMP _rv_head_sample;
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
.VAR _rv_contract = 769; // DK_MACHINE_V1 | DK_MACHINE_STEREO | DK_MACHINE_GATE
.VAR _rv_params = 0;
.VAR _rv_voices = DK_MAX_VOICES;
.VAR _rv_state_bytes = 52;
.VAR _rv_clear_bytes = 52;
.VAR _rv_depth = 3;
.VAR _rv_stack = 0;
