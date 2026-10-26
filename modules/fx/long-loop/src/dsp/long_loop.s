// michael/long-loop: one-second stereo master-loop proof, DK_FX_V1.
// One FX row owns _ll_audio in module BSS. Only the 64-byte metadata is cleared
// by core/fx. BSS is neither initialized nor read before a complete capture.
// This experimental module deliberately uses module-owned mutable BSS instead
// of putting the audio in descriptor state. Do not share it between FX rows.
// State: mode, written, phase, start, pending_start, gain, previous_rec,
// relocated audio byte address, frame count, then seven reserved zero words.
// Parameters: REC, START, FADE, all normalized. MIX must remain at 127.

#include "dk_fx.inc"
#define LL_MODE 0
#define LL_WRITTEN 1
#define LL_PHASE 2
#define LL_START 3
#define LL_PENDING 4
#define LL_GAIN 5
#define LL_REC 6
#define LL_AUDIO_ADDRESS 7
#define LL_FRAME_COUNT 8
#define LL_FRAMES 48000
#define LL_AUDIO_BYTES 384000
#define LL_STATE_BYTES 64

.SECTION/PM seg_pmco;
.GLOBAL _ll_process;

_ll_process:
    I5 = R4;
    // Diagnostics describe our relocated allocation; they never control reads.
    R0 = _ll_audio;
    DM(LL_AUDIO_ADDRESS, I5) = R0;
    R0 = LL_FRAMES;
    DM(LL_FRAME_COUNT, I5) = R0;
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
    R2 = 0x473b7f00;                     // 47999.0f
    F1 = F0 * F2;
    R2 = TRUNC F1;
    DM(LL_PENDING, I5) = R2;
    R0 = DM(2, I4);
    F1 = MAX(F0, F7);
    F14 = MIN(F1, F11);

    R0 = DM(0, I4);
    R1 = 0x3f000000;
    R2 = 0;
    R3 = 1;
    COMP(F0, F1);
    IF GE R2 = R3;
    R3 = DM(LL_REC, I5);
    DM(LL_REC, I5) = R2;
    R0 = DM(LL_MODE, I5);
    R0 = PASS R0;
    IF NE JUMP _ll_dispatch;
    R2 = PASS R2;
    IF EQ JUMP _ll_live;
    R3 = PASS R3;
    IF NE JUMP _ll_live;
    R0 = 1;
    DM(LL_MODE, I5) = R0;
_ll_dispatch:
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ll_play;

    // Write both channels before making any audio readable. 48000 is a whole
    // number of 32-frame blocks, so the final capture cannot overrun the BSS.
    R0 = DM(LL_WRITTEN, I5);
    R1 = LSHIFT R0 BY 2;
    R4 = _ll_audio;
    R2 = R4 + R1;
    I4 = R2;
    M2 = LL_FRAMES;
    LCNTR = DK_BLOCK, DO _ll_record_end UNTIL LCE;
        R4 = DM(I0, M6);
        R8 = DM(I1, M6);
        DM(M2, I4) = R8;
        DM(I4, M6) = R4;
        DM(I2, M6) = R4;
_ll_record_end: DM(I3, M6) = R8;
    R1 = DK_BLOCK;
    R2 = R0 + R1;
    DM(LL_WRITTEN, I5) = R2;
    R1 = LL_FRAMES;
    COMP(R2, R1);
    IF LT JUMP _ll_done;
    R0 = 2;
    DM(LL_MODE, I5) = R0;
    R0 = DM(LL_PENDING, I5);
    DM(LL_START, I5) = R0;
    JUMP _ll_done;

_ll_live:
    LCNTR = DK_BLOCK, DO _ll_live_end UNTIL LCE;
        R0 = DM(I0, M6);
        R1 = DM(I1, M6);
        DM(I2, M6) = R0;
_ll_live_end: DM(I3, M6) = R1;
    JUMP _ll_done;

_ll_play:
    R5 = DM(LL_PHASE, I5);
    R6 = DM(LL_START, I5);
    R15 = DM(LL_GAIN, I5);
    R7 = LL_FRAMES;
    R13 = 0x3b800000;                    // gain step 1/256 per stereo frame
    M2 = LL_FRAMES;
    LCNTR = DK_BLOCK, DO _ll_play_end UNTIL LCE;
        R0 = R5 + R6;
        R1 = R0 - R7;
        COMP(R0, R7);
        IF GE R0 = R1;
        R12 = R0;
        R1 = LSHIFT R0 BY 2;
        R3 = _ll_audio;
        R0 = R3 + R1;
        I4 = R0;
        R9 = DM(I4, M5);
        R10 = DM(M2, I4);

        // Smooth the phrase boundary and the rotated physical capture seam.
        R1 = 47999;
        R0 = R1 - R5;
        R2 = MIN(R0, R5);
        R0 = R1 - R12;
        R3 = MIN(R0, R12);
        R0 = MIN(R2, R3);
        R3 = R11;
        R2 = 16;
        COMP(R0, R2);
        IF GE JUMP _ll_seam_ready;
        F1 = FLOAT R0;
        R2 = 0x3d800000;
        F3 = F1 * F2;
_ll_seam_ready:
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
        IF EQ JUMP _ll_dry_sample;
        COMP(F15, F11);
        IF EQ JUMP _ll_sample_ready;
        F0 = F9 - F4;
        F1 = F0 * F15;
        F9 = F4 + F1;
        F0 = F10 - F8;
        F1 = F0 * F15;
        F10 = F8 + F1;
        JUMP _ll_sample_ready;
_ll_dry_sample:
        R9 = R4;
        R10 = R8;
_ll_sample_ready:
        R5 = R5 + 1;
        COMP(R5, R7);
        IF LT JUMP _ll_phase_ready;
        R5 = 0;
        R6 = DM(LL_PENDING, I5);
_ll_phase_ready:
        DM(I2, M6) = R9;
_ll_play_end: DM(I3, M6) = R10;
    DM(LL_PHASE, I5) = R5;
    DM(LL_START, I5) = R6;
    DM(LL_GAIN, I5) = R15;
_ll_done:
    R0 = DK_FX_OK;
    JUMP (M13, I13);

.SECTION/DM seg_dmda;
.GLOBAL _ll_desc;
.VAR _ll_desc = DK_FX_MAGIC;
.VAR _ll_desc1 = DK_FX_V1;
.VAR _ll_desc2 = 3;
.VAR _ll_desc3 = LL_STATE_BYTES;
.VAR _ll_desc4 = 3500;
.VAR _ll_desc5 = 0;
.VAR _ll_desc6 = 0;
.PKBSS _ll_audio, LL_AUDIO_BYTES;
