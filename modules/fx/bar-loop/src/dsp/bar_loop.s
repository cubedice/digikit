// michael/bar-loop: eight-second stereo master-loop proof, DK_FX_V1.
// Requires the explicit _loop_buffer_info import from the DT2 1.17 storage
// provider. There is no fallback address and no module BSS audio allocation.
// The provider owns one buffer for this single FX row. Only the 64-byte
// metadata is cleared by core/fx. Audio is unreadable until fully captured.
// State: mode, written, phase, start, pending_start, gain, previous_rec,
// relocated audio byte address, frame count, canary initialized, six zero words.
// Parameters: REC, START, FADE, all normalized. MIX must remain at 127.

#include "dk_fx.inc"
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
#define BL_FRAMES 384000
#define BL_AUDIO_BYTES 3072000
#define BL_STORAGE_BYTES 3072032
#define BL_CANARY_WORD 0x5a17b00f
#define BL_STATE_BYTES 64

.SECTION/PM seg_pmco;
.GLOBAL _bl_process;
.EXTERN _loop_buffer_info;

_bl_process:
    I5 = R4;
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
    R0 = BL_FRAMES;
    DM(BL_FRAME_COUNT, I5) = R0;
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

    R7 = 0;
    R11 = 0x3f800000;
    R0 = DM(1, I4);
    F1 = MAX(F0, F7);
    F0 = MIN(F1, F11);
    R2 = 0x48bb7fe0;                     // 383999.0f
    F1 = F0 * F2;
    R2 = TRUNC F1;
    DM(BL_PENDING, I5) = R2;
    R0 = DM(2, I4);
    F1 = MAX(F0, F7);
    F14 = MIN(F1, F11);

    R0 = DM(0, I4);
    R1 = 0x3f000000;
    R2 = 0;
    R3 = 1;
    COMP(F0, F1);
    IF GE R2 = R3;
    R3 = DM(BL_REC, I5);
    DM(BL_REC, I5) = R2;
    R0 = DM(BL_MODE, I5);
    R0 = PASS R0;
    IF NE JUMP _bl_dispatch;
    R2 = PASS R2;
    IF EQ JUMP _bl_live;
    R3 = PASS R3;
    IF NE JUMP _bl_live;
    R0 = 1;
    DM(BL_MODE, I5) = R0;
_bl_dispatch:
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _bl_play;

    // Write both channels before making any audio readable. 384000 is a whole
    // number of 32-frame blocks, so the final capture stays inside the lease.
    R0 = DM(BL_WRITTEN, I5);
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
    R1 = BL_FRAMES;
    COMP(R2, R1);
    IF LT JUMP _bl_done;
    R0 = 2;
    DM(BL_MODE, I5) = R0;
    R0 = DM(BL_PENDING, I5);
    DM(BL_START, I5) = R0;
    JUMP _bl_done;

_bl_live:
    LCNTR = DK_BLOCK, DO _bl_live_end UNTIL LCE;
        R0 = DM(I0, M6);
        R1 = DM(I1, M6);
        DM(I2, M6) = R0;
_bl_live_end: DM(I3, M6) = R1;
    JUMP _bl_done;

_bl_play:
    R5 = DM(BL_PHASE, I5);
    R6 = DM(BL_START, I5);
    R15 = DM(BL_GAIN, I5);
    R7 = BL_FRAMES;
    R13 = 0x3b800000;                    // gain step 1/256 per stereo frame
    M2 = BL_FRAMES;
    LCNTR = DK_BLOCK, DO _bl_play_end UNTIL LCE;
        R0 = R5 + R6;
        R1 = R0 - R7;
        COMP(R0, R7);
        IF GE R0 = R1;
        R12 = R0;
        R1 = LSHIFT R0 BY 2;
        R3 = DM(BL_AUDIO_ADDRESS, I5);
        R0 = R3 + R1;
        I4 = R0;
        R9 = DM(I4, M5);
        R10 = DM(M2, I4);

        // Smooth the phrase boundary and the rotated physical capture seam.
        R1 = 383999;
        R0 = R1 - R5;
        R2 = MIN(R0, R5);
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
        R6 = DM(BL_PENDING, I5);
_bl_phase_ready:
        DM(I2, M6) = R9;
_bl_play_end: DM(I3, M6) = R10;
    DM(BL_PHASE, I5) = R5;
    DM(BL_START, I5) = R6;
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
.VAR _bl_desc2 = 3;
.VAR _bl_desc3 = BL_STATE_BYTES;
.VAR _bl_desc4 = 3500;
.VAR _bl_desc5 = 0;
.VAR _bl_desc6 = 0;
