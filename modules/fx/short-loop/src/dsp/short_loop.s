// michael/short-loop: bounded stereo master-loop proof, DK_FX_V1.
// All mutable data is descriptor-owned state. No firmware addresses or sample RAM.
// 12000 frames / 48000 Hz = 250 ms. Capture once per module activation.
// State words: mode, written, phase, start, pending_start, gain, previous_rec,
// reserved (9 words); then 12000 left floats and 12000 right floats.
// Parameters: REC, START, FADE, all normalized. FADE 0 = live, 1 = frozen loop.
// MIX belongs to core/fx: keep it at 127. These page controls are kit-local.

#include "dk_fx.inc"
#define SL_MODE 0
#define SL_WRITTEN 1
#define SL_PHASE 2
#define SL_START 3
#define SL_PENDING 4
#define SL_GAIN 5
#define SL_REC 6
#define SL_FRAMES 12000
#define SL_BUFFER_BYTES 64
#define SL_STATE_BYTES 96064

.SECTION/PM seg_pmco;
.GLOBAL _sl_process;

_sl_process:
    I5 = R4;
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

    // Saturate the controls. Every position is inside the captured buffer.
    R7 = 0;
    R11 = 0x3f800000;
    R0 = DM(1, I4);
    F1 = MAX(F0, F7);
    F0 = MIN(F1, F11);
    R2 = 0x463b7c00;                     // 11999.0f
    F1 = F0 * F2;
    R2 = TRUNC F1;
    DM(SL_PENDING, I5) = R2;
    R0 = DM(2, I4);
    F1 = MAX(F0, F7);
    F14 = MIN(F1, F11);                  // requested loop gain

    R0 = DM(0, I4);
    R1 = 0x3f000000;
    R2 = 0;
    R3 = 1;
    COMP(F0, F1);
    IF GE R2 = R3;
    R3 = DM(SL_REC, I5);
    DM(SL_REC, I5) = R2;
    R0 = DM(SL_MODE, I5);
    R0 = PASS R0;
    IF NE JUMP _sl_dispatch;
    R2 = PASS R2;
    IF EQ JUMP _sl_live;
    R3 = PASS R3;
    IF NE JUMP _sl_live;
    R0 = 1;
    DM(SL_MODE, I5) = R0;
_sl_dispatch:
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _sl_play;

    // Capture input, never the effect's blended output. Both channels share
    // the same frame counter. 12000 is divisible by the 32-frame block size.
    R0 = DM(SL_WRITTEN, I5);
    R1 = LSHIFT R0 BY 2;
    R2 = I5;
    R3 = SL_BUFFER_BYTES;
    R4 = R2 + R3;
    R2 = R4 + R1;
    I4 = R2;
    M2 = SL_FRAMES;
    LCNTR = DK_BLOCK, DO _sl_record_end UNTIL LCE;
        R4 = DM(I0, M6);
        R8 = DM(I1, M6);
        DM(M2, I4) = R8;
        DM(I4, M6) = R4;
        DM(I2, M6) = R4;
_sl_record_end: DM(I3, M6) = R8;
    R1 = DK_BLOCK;
    R2 = R0 + R1;
    DM(SL_WRITTEN, I5) = R2;
    R1 = SL_FRAMES;
    COMP(R2, R1);
    IF LT JUMP _sl_done;
    R0 = 2;
    DM(SL_MODE, I5) = R0;
    R0 = DM(SL_PENDING, I5);
    DM(SL_START, I5) = R0;
    JUMP _sl_done;

_sl_live:
    LCNTR = DK_BLOCK, DO _sl_live_end UNTIL LCE;
        R0 = DM(I0, M6);
        R1 = DM(I1, M6);
        DM(I2, M6) = R0;
_sl_live_end: DM(I3, M6) = R1;
    JUMP _sl_done;

_sl_play:
    R5 = DM(SL_PHASE, I5);
    R6 = DM(SL_START, I5);
    R15 = DM(SL_GAIN, I5);
    R7 = SL_FRAMES;
    R13 = 0x3b800000;                    // gain step 1/256 per stereo frame
    M2 = SL_FRAMES;
    LCNTR = DK_BLOCK, DO _sl_play_end UNTIL LCE;
        R0 = R5 + R6;
        R1 = R0 - R7;
        COMP(R0, R7);
        IF GE R0 = R1;
        R12 = R0;                       // physical capture index, for its seam
        R1 = LSHIFT R0 BY 2;
        R2 = I5;
        R0 = SL_BUFFER_BYTES;
        R3 = R2 + R0;
        R0 = R3 + R1;
        I4 = R0;
        R9 = DM(I4, M5);
        R10 = DM(M2, I4);

        // Smooth both the phrase boundary (where START changes) and the
        // physical capture seam, which rotates inside a phrase for START>0.
        // No frames are removed: the loop period remains exactly 12000.
        R1 = 11999;
        R0 = R1 - R5;
        R2 = MIN(R0, R5);
        R0 = R1 - R12;
        R3 = MIN(R0, R12);
        R0 = MIN(R2, R3);
        R3 = R11;
        R2 = 16;
        COMP(R0, R2);
        IF GE JUMP _sl_seam_ready;
        F1 = FLOAT R0;
        R2 = 0x3d800000;                 // 1/16
        F3 = F1 * F2;
_sl_seam_ready:
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
        IF EQ JUMP _sl_dry_sample;
        COMP(F15, F11);
        IF EQ JUMP _sl_sample_ready;
        F0 = F9 - F4;
        F1 = F0 * F15;
        F9 = F4 + F1;
        F0 = F10 - F8;
        F1 = F0 * F15;
        F10 = F8 + F1;
        JUMP _sl_sample_ready;
_sl_dry_sample:
        R9 = R4;
        R10 = R8;
_sl_sample_ready:
        R5 = R5 + 1;
        COMP(R5, R7);
        IF LT JUMP _sl_phase_ready;
        R5 = 0;
        R6 = DM(SL_PENDING, I5);
_sl_phase_ready:
        DM(I2, M6) = R9;
_sl_play_end: DM(I3, M6) = R10;
    DM(SL_PHASE, I5) = R5;
    DM(SL_START, I5) = R6;
    DM(SL_GAIN, I5) = R15;
_sl_done:
    R0 = DK_FX_OK;
    JUMP (M13, I13);

.SECTION/DM seg_dmda;
.GLOBAL _sl_desc;
.VAR _sl_desc = DK_FX_MAGIC;
.VAR _sl_desc1 = DK_FX_V1;
.VAR _sl_desc2 = 3;
.VAR _sl_desc3 = SL_STATE_BYTES;
.VAR _sl_desc4 = 3500;
.VAR _sl_desc5 = 0;
.VAR _sl_desc6 = 0;
