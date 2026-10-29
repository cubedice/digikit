// Armed stereo loop: REC edge, next START boundary, N steps, then hold.
#include "dk_fx.inc"
#include "dk_loop_controls.inc"
#define BL_MODE 0
#define BL_WRITTEN 1
#define BL_PHASE 2
#define BL_START 3
#define BL_PENDING 4
#define BL_GAIN 5
#define BL_REC 6
#define BL_AUDIO_ADDRESS 7
#define BL_FRAME_COUNT 8
#define BL_CANARY_INITIALIZED 9
#define BL_FRAMES 1048544
#define BL_AUDIO_BYTES 8388352
#define BL_STORAGE_BYTES 8388384
#define BL_CANARY_WORD 0x5a17b00f
#define BL_STATE_BYTES 644

.SECTION/PM seg_pmco;
.GLOBAL _bl_process;
.EXTERN _loop_buffer_info;
.EXTERN _pattern_position_info;

_bl_process:
    I5 = R4;
    I4 = _loop_controls_info;
    R0 = DM(0, I4);
    R1 = DK_LOOP_CONTROLS_MAGIC;
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    // Validate the immutable provider record on every call, before any audio
    // access. Its address is a required runtime import, never a guessed RAM
    // location. Words: magic, version, DSP byte base, owned bytes.
    I4 = _loop_buffer_info;
    R0 = DM(0, I4);
    R1 = 0x4c425546;
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(1, I4);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R2 = DM(2, I4);
    R3 = DM(3, I4);
    R0 = 3;
    R1 = R2 AND R0;
    R1 = PASS R1;
    IF NE JUMP _bl_invalid_provider;
    R1 = R3 AND R0;
    R1 = PASS R1;
    IF NE JUMP _bl_invalid_provider;
    R3 = PASS R3;
    IF LT JUMP _bl_invalid_provider;
    R1 = BL_STORAGE_BYTES;
    COMP(R3, R1);
    IF LT JUMP _bl_invalid_provider;

    // XOR maps the external byte range [0x80000000,0xa0000000) to positive
    // offsets [0,0x20000000). Reject a base outside it, then compare capacity
    // to (range size - offset). No overflowing base+size addition is needed.
    R1 = 0x80000000;
    R4 = R2 XOR R1;
    R4 = PASS R4;
    IF LT JUMP _bl_invalid_provider;
    R1 = 0x20000000;
    COMP(R4, R1);
    IF GE JUMP _bl_invalid_provider;
    R6 = R1 - R4;
    COMP(R3, R6);
    IF GT JUMP _bl_invalid_provider;

    // Own a 32-byte canary immediately after audio. Initialize only these
    // eight words on reset, then verify them before every later audio access.
    R0 = BL_AUDIO_BYTES;
    R6 = R2 + R0;
    I4 = R6;
    R0 = DM(BL_CANARY_INITIALIZED, I5);
    R0 = PASS R0;
    IF NE JUMP _bl_check_canary;
    R0 = BL_CANARY_WORD;
    LCNTR = 8, DO _bl_init_canary_end UNTIL LCE;
_bl_init_canary_end: DM(I4, M6) = R0;
    R0 = 1;
    DM(BL_CANARY_INITIALIZED, I5) = R0;
    JUMP _bl_provider_ready;
_bl_check_canary:
    // Unrolled so a failed comparison never jumps out of an active DO loop.
    R1 = BL_CANARY_WORD;
    R0 = DM(0, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(1, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(2, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(3, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(4, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(5, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(6, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
    R0 = DM(7, I4);
    COMP(R0, R1);
    IF NE JUMP _bl_invalid_provider;
_bl_provider_ready:
    DM(BL_AUDIO_ADDRESS, I5) = R2;
    I4 = R8;
    I0 = DM(DK_FX_IN_W_IN, I4);
    I2 = DM(DK_FX_IN_W_OUT, I4);
    R0 = I0;
    R1 = 128;
    R2 = R0 + R1;
    I1 = R2;
    R0 = I2;
    R2 = R0 + R1;
    I3 = R2;
    I4 = R12;

    // State 10 TX, 11 boundary seq, 12 step, 13 generation, 14 pattern,
    // 15 pattern length, 16 have header, 17 arm TX, 18 start TX, 19 end TX,
    // 20 requested steps (0 = pattern), 21 latched steps, 22 progress,
    // 23 error, 24 fresh boundary, 25 continuity, 31 parameter pointer.
    DM(31, I5) = R12;
    R0 = DM(0, I4);
    R1 = 0x3f000000;
    R2 = 0;
    R3 = 1;
    COMP(F0, F1);
    IF GE R2 = R3;
    R3 = DM(BL_REC, I5);
    DM(BL_REC, I5) = R2;
    R0 = DM(BL_MODE, I5);
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _bl_timing;
    R2 = PASS R2;
    IF EQ JUMP _bl_timing;
    R3 = PASS R3;
    IF NE JUMP _bl_timing;
    // REC edge arms; knobs use integer units despite the 0..127 UI.
    R7 = 0;
    R11 = 0x42fe0000;               // 127.0
    R0 = DM(1, I4);
    F1 = MAX(F0, F7);
    F0 = MIN(F1, F11);
    R2 = TRUNC F0;
    DM(BL_START, I5) = R2;
    R0 = DM(3, I4);
    F1 = MAX(F0, F7);
    F0 = MIN(F1, F11);
    R2 = TRUNC F0;
    DM(20, I5) = R2;
    R0 = 3;
    DM(BL_MODE, I5) = R0;
    I4 = _pattern_position_info;
    R0 = DM(2, I4);
    DM(17, I5) = R0;
    R0 = 0;
    DM(23, I5) = R0;
_bl_timing:
    I4 = _pattern_position_info;
    R0 = DM(0, I4);
    R1 = 0x50504f53;
    COMP(R0, R1);
    IF NE JUMP _bl_no_header;
    R0 = DM(1, I4);
    R1 = 2;
    COMP(R0, R1);
    IF NE JUMP _bl_no_header;
    R0 = 0;
    DM(24, I5) = R0;
    DM(25, I5) = R0;
    R0 = DM(16, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_save_timing;
    R0 = DM(2, I4);
    R1 = DM(10, I5);
    R2 = R0 - R1;
    R1 = 1;
    COMP(R2, R1);
    IF NE JUMP _bl_save_timing;
    R0 = DM(3, I4);
    R1 = DM(11, I5);
    R2 = R0 - R1;
    R1 = 1;
    COMP(R2, R1);
    IF GT JUMP _bl_save_timing;
    R2 = PASS R2;
    IF LT JUMP _bl_save_timing;
    DM(24, I5) = R2;
    R0 = DM(4, I4);
    R1 = DM(13, I5);
    COMP(R0, R1);
    IF NE JUMP _bl_save_timing;
    R0 = DM(6, I4);
    R1 = DM(14, I5);
    COMP(R0, R1);
    IF NE JUMP _bl_switch_boundary;
    R0 = DM(8, I4);
    R1 = DM(15, I5);
    COMP(R0, R1);
    IF NE JUMP _bl_save_timing;
    R3 = DM(12, I5);
    R3 = R3 + R2;
    R0 = 0;
    COMP(R3, R1);
    IF GE R3 = R0;
    R0 = DM(7, I4);
    COMP(R0, R3);
    IF NE JUMP _bl_save_timing;
    R0 = 1;
    DM(25, I5) = R0;
    JUMP _bl_save_timing;
_bl_switch_boundary:
    // A take ending exactly on a pattern switch is complete, not an abort.
    // Only accept a contiguous final source step -> destination step zero.
    R0 = DM(24, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_save_timing;
    R0 = DM(12, I5);
    R0 = R0 + 1;
    R1 = DM(15, I5);
    COMP(R0, R1);
    IF NE JUMP _bl_save_timing;
    R0 = DM(7, I4);
    R0 = PASS R0;
    IF NE JUMP _bl_save_timing;
    R0 = DM(22, I5);
    R0 = R0 + 1;
    R1 = DM(21, I5);
    COMP(R0, R1);
    IF NE JUMP _bl_save_timing;
    R0 = 1;
    DM(25, I5) = R0;
_bl_save_timing:
    // A source cue cannot start an unwanted new take in the destination.
    R0 = DM(16, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_store_timing;
    R0 = DM(6, I4);
    R1 = DM(14, I5);
    COMP(R0, R1);
    IF EQ JUMP _bl_store_timing;
    R0 = DM(BL_MODE, I5);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _bl_store_timing;
    R0 = 0;
    DM(BL_MODE, I5) = R0;
_bl_store_timing:
    R0 = DM(2, I4);
    DM(10, I5) = R0;
    R0 = DM(3, I4);
    DM(11, I5) = R0;
    R0 = DM(7, I4);
    DM(12, I5) = R0;
    R0 = DM(4, I4);
    DM(13, I5) = R0;
    R0 = DM(6, I4);
    DM(14, I5) = R0;
    R0 = DM(8, I4);
    DM(15, I5) = R0;
    R0 = 1;
    DM(16, I5) = R0;
    R0 = DM(5, I4);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _bl_bad_timing;
    R0 = DM(8, I4);
    R1 = 1;
    COMP(R0, R1);
    IF LE JUMP _bl_bad_timing;
    R1 = DM(7, I4);
    R1 = PASS R1;
    IF LT JUMP _bl_bad_timing;
    COMP(R1, R0);
    IF GE JUMP _bl_bad_timing;
    R0 = DM(BL_MODE, I5);
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _bl_capturing;
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _bl_output;
    R0 = DM(24, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_output;
    R0 = DM(2, I4);
    R1 = DM(17, I5);
    COMP(R0, R1);
    IF EQ JUMP _bl_output;
    R0 = DM(7, I4);
    R1 = DM(BL_START, I5);
    COMP(R0, R1);
    IF NE JUMP _bl_output;
    R0 = DM(20, I5);
    R1 = DM(8, I4);
    R0 = PASS R0;
    IF EQ R0 = R1;
    DM(21, I5) = R0;
    R2 = 128;
    COMP(R0, R2);
    IF GT JUMP _bl_bad_timing;
    DM(28, I5) = R0;              // recorded steps, independent of later arming
    R0 = DM(BL_START, I5);
    DM(26, I5) = R0;              // capture origin on the source pattern grid
    DM(27, I5) = R1;              // source pattern length
    R0 = 0;
    DM(32, I5) = R0;              // boundary offsets: 0..128, in audio frames
    DM(BL_WRITTEN, I5) = R0;
    DM(BL_FRAME_COUNT, I5) = R0;
    DM(BL_PHASE, I5) = R0;
    DM(BL_GAIN, I5) = R0;
    DM(22, I5) = R0;
    DM(19, I5) = R0;
    R0 = DM(2, I4);
    DM(18, I5) = R0;
    R0 = 1;
    DM(BL_MODE, I5) = R0;
    JUMP _bl_record;
_bl_capturing:
    R0 = DM(25, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_bad_timing;
    R0 = DM(24, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_record;
    R0 = DM(22, I5);
    R0 = R0 + 1;
    DM(22, I5) = R0;
    R2 = 32;
    R2 = R0 + R2;
    R2 = LSHIFT R2 BY 2;
    R3 = I5;
    R2 = R2 + R3;
    I4 = R2;
    R2 = DM(BL_WRITTEN, I5);
    DM(0, I4) = R2;
    I4 = _pattern_position_info;
    R1 = DM(21, I5);
    COMP(R0, R1);
    IF LT JUMP _bl_record;
    // Close before copying this block: exactly N whole nominal steps.
    R0 = DM(BL_WRITTEN, I5);
    DM(BL_FRAME_COUNT, I5) = R0;
    R0 = DM(2, I4);
    DM(19, I5) = R0;
    R0 = 2;
    DM(BL_MODE, I5) = R0;
    JUMP _bl_output;
_bl_no_header:
    R0 = 0;
    DM(16, I5) = R0;
_bl_bad_timing:
    R0 = DM(BL_MODE, I5);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _bl_output;
    R0 = 1;
    DM(23, I5) = R0;
_bl_abort:
    R0 = 0;
    DM(BL_MODE, I5) = R0;
    DM(BL_FRAME_COUNT, I5) = R0;
    DM(BL_WRITTEN, I5) = R0;
    DM(BL_GAIN, I5) = R0;
    JUMP _bl_live;
_bl_record:
    R0 = DM(BL_WRITTEN, I5);
    R1 = BL_FRAMES;
    COMP(R0, R1);
    IF LT JUMP _bl_record_room;
    R0 = 3;
    DM(23, I5) = R0;
    JUMP _bl_abort;
_bl_record_room:
    R1 = LSHIFT R0 BY 2;
    R4 = DM(BL_AUDIO_ADDRESS, I5);
    R2 = R4 + R1;
    I4 = R2;
    M2 = BL_FRAMES;
    LCNTR = DK_BLOCK, DO _bl_record_end UNTIL LCE;
        R4 = DM(I0, M6);
        R8 = DM(I1, M6);
        DM(M2, I4) = R8;
        DM(I4, M6) = R4;
        DM(I2, M6) = R4;
_bl_record_end: DM(I3, M6) = R8;
    R1 = DK_BLOCK;
    R2 = R0 + R1;
    DM(BL_WRITTEN, I5) = R2;
    JUMP _bl_done;
_bl_output:
    R0 = DM(BL_FRAME_COUNT, I5);
    R0 = PASS R0;
    IF EQ JUMP _bl_live;
    I4 = DM(31, I5);
    R7 = 0;
    R11 = 0x3f800000;
    R0 = DM(2, I4);
    F1 = MAX(F0, F7);
    F14 = MIN(F1, F11);
    JUMP _bl_play;

_bl_live:
    LCNTR = DK_BLOCK, DO _bl_live_end UNTIL LCE;
        R0 = DM(I0, M6);
        R1 = DM(I1, M6);
        DM(I2, M6) = R0;
_bl_live_end: DM(I3, M6) = R1;
    JUMP _bl_done;

_bl_play:
    // Resolve the live START against the recorded source grid. Wrap inside
    // the captured phrase; LENGTH=0 uses all captured steps. Exact offsets
    // preserve unequal step durations (tempo changes during capture).
    I4 = DM(31, I5);
    R7 = 0;
    R11 = 0x42fe0000;
    R0 = DM(1, I4);
    F1 = MAX(F0, F7);
    F0 = MIN(F1, F11);
    R0 = TRUNC F0;
    R1 = DM(26, I5);
    R0 = R0 - R1;
    R1 = DM(27, I5);
_bl_start_positive:
    R0 = PASS R0;
    IF GE JUMP _bl_start_grid;
    R0 = R0 + R1;
    JUMP _bl_start_positive;
_bl_start_grid:
    COMP(R0, R1);
    IF LT JUMP _bl_start_capture;
    R0 = R0 - R1;
    JUMP _bl_start_grid;
_bl_start_capture:
    R1 = DM(28, I5);
_bl_start_wrap:
    COMP(R0, R1);
    IF LT JUMP _bl_window;
    R0 = R0 - R1;
    JUMP _bl_start_wrap;
_bl_window:
    R4 = R0;
    R2 = 32;
    R2 = R0 + R2;
    R2 = LSHIFT R2 BY 2;
    R3 = I5;
    R2 = R2 + R3;
    I4 = R2;
    R6 = DM(0, I4);
    DM(29, I5) = R6;
    I4 = DM(31, I5);
    R0 = DM(3, I4);
    F2 = MAX(F0, F7);
    F0 = MIN(F2, F11);
    R0 = TRUNC F0;
    R0 = PASS R0;
    IF EQ R0 = R1;
    R0 = MIN(R0, R1);
    R0 = R0 + R4;
    R3 = 0;
    COMP(R0, R1);
    IF LE JUMP _bl_window_end;
    R0 = R0 - R1;
    R3 = DM(BL_FRAME_COUNT, I5);
_bl_window_end:
    R2 = 32;
    R2 = R0 + R2;
    R2 = LSHIFT R2 BY 2;
    R8 = I5;
    R2 = R2 + R8;
    I4 = R2;
    R7 = DM(0, I4);
    R7 = R7 + R3;
    R7 = R7 - R6;
    DM(30, I5) = R7;
    R5 = DM(BL_PHASE, I5);
    // A shorter window takes effect on this block, without stale reads.
    COMP(R5, R7);
    IF LT JUMP _bl_window_ready;
    R5 = 0;
_bl_window_ready:
    R15 = DM(BL_GAIN, I5);
    R11 = 0x3f800000;
    R13 = 0x3b800000;                    // gain step 1/256 per stereo frame
    M2 = BL_FRAMES;
    LCNTR = DK_BLOCK, DO _bl_play_end UNTIL LCE;
        R0 = R5 + R6;
        R2 = DM(BL_FRAME_COUNT, I5);
        R1 = R0 - R2;
        COMP(R0, R2);
        IF GE R0 = R1;
        R12 = R0;
        R1 = LSHIFT R0 BY 2;
        R3 = DM(BL_AUDIO_ADDRESS, I5);
        R0 = R3 + R1;
        I4 = R0;
        R9 = DM(I4, M5);
        R10 = DM(M2, I4);

        // Smooth the phrase boundary and the rotated physical capture seam.
        R1 = R7 - 1;
        R0 = R1 - R5;
        R2 = MIN(R0, R5);
        R1 = DM(BL_FRAME_COUNT, I5);
        R1 = R1 - 1;
        R0 = R1 - R12;
        R3 = MIN(R0, R12);
        R0 = MIN(R2, R3);
        R3 = R11;
        R2 = 16;
        COMP(R0, R2);
        IF GE JUMP _bl_seam_ready;
        F1 = FLOAT R0;
        R2 = 0x3d800000;
        F3 = F1 * F2;
_bl_seam_ready:
        F9 = F9 * F3;
        F10 = F10 * F3;

        F0 = F14 - F15;
        F1 = -F13;
        F2 = MAX(F0, F1);
        F0 = MIN(F2, F13);
        F15 = F15 + F0;
        R4 = DM(I0, M6);
        R8 = DM(I1, M6);
        R0 = 0;
        COMP(F15, F0);
        IF EQ JUMP _bl_dry_sample;
        COMP(F15, F11);
        IF EQ JUMP _bl_sample_ready;
        F0 = F9 - F4;
        F1 = F0 * F15;
        F9 = F4 + F1;
        F0 = F10 - F8;
        F1 = F0 * F15;
        F10 = F8 + F1;
        JUMP _bl_sample_ready;
_bl_dry_sample:
        R9 = R4;
        R10 = R8;
_bl_sample_ready:
        R5 = R5 + 1;
        COMP(R5, R7);
        IF LT JUMP _bl_phase_ready;
        R5 = 0;
_bl_phase_ready:
        DM(I2, M6) = R9;
_bl_play_end: DM(I3, M6) = R10;
    DM(BL_PHASE, I5) = R5;
    DM(BL_GAIN, I5) = R15;
_bl_done:
    R0 = DK_FX_OK;
    JUMP (M13, I13);

_bl_invalid_provider:
    R0 = -1;                            // real FX guard plays dry and resets state
    JUMP (M13, I13);

.SECTION/DM seg_dmda;
.GLOBAL _bl_desc;
.VAR _bl_desc = DK_FX_MAGIC;
.VAR _bl_desc1 = DK_FX_V1;
.VAR _bl_desc2 = 4;
.VAR _bl_desc3 = BL_STATE_BYTES;
.VAR _bl_desc4 = 5000;
.VAR _bl_desc5 = 0;
.VAR _bl_desc6 = 0;
