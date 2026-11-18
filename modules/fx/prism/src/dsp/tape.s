// PRISM tape delay: one stereo head on the rolling bank, which is the tape. The head reads behind the write position of the
// block about to be written, so every sample it reads was written by an earlier block. Its output is the delay signal of this
// block (PT_OUT) and, filtered and limited, the feedback that the write adds to the tape (PT_FEEDBACK).
//
// Modes: FORWARD reads TIME frames behind the write position, with the delay slewed by at most half a frame per sample (a
// change of TIME glides like tape). REVERSE plays the segment before each segment start backwards. HOLD stops the tape (the
// write skips, capture.s) and loops the last TIME frames. When the tape cannot be read (no rolling bank during a phrase
// handoff, not enough history yet), the head is silent.
//
// The output never jumps. Where it would (a change of mode, a tape that can no longer be read), the difference to the last
// sample is carried on and run out over 256 samples (PT_HEAL_*); a head that comes back after silence fades in over 256
// samples (PT_FADE). The feedback is always the filtered output, so it ends as smoothly as the output does.
#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_tape;
.GLOBAL _prism_mix;

_prism_tape:
    R15 = DM(PC_TAPE,I5);
    R0 = PT_FORWARD;
    R1 = PH_TAPE_FLIP;
    R2 = R15 AND R1;
    R3 = PT_REVERSE;
    R2 = PASS R2;
    IF NE R0 = PASS R3;
    R1 = PH_TAPE_HOLD;
    R2 = R15 AND R1;
    R2 = PASS R2;
    IF EQ JUMP _pt_mode_set;
    // A take in progress needs the tape to run: HOLD waits for it.
    R1 = DM(PS_MODE,I5);
    R1 = PASS R1;
    IF NE JUMP _pt_mode_set;
    R0 = PT_HOLD;
_pt_mode_set:
    R1 = DM(PT_MODE,I5);
    DM(PT_MODE,I5) = R0;
    // PS_TEMP1: the output may jump at the start of this block (a new mode here, or a tape that went away below).
    R2 = 0;
    DM(PS_TEMP1,I5) = R2;
    COMP(R0,R1);
    IF EQ JUMP _pt_mode_same;
    R1 = 0;
    DM(PT_COUNT_L,I5) = R1;
    DM(PT_COUNT_R,I5) = R1;
    DM(PT_LENGTH_L,I5) = R1;
    DM(PT_LENGTH_R,I5) = R1;
    R2 = 1;
    DM(PS_TEMP1,I5) = R2;
_pt_mode_same:
    // Feedback gain, 0..1.1.
    F0 = DM(PC_FDBK,I5);
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3f8ccccd;
    F0 = MIN(F0,F1);
    DM(PT_FDBK,I5) = R0;
    // The slow wow: a triangle of about 0.7 Hz, at most 0.3 % of the delay.
    F0 = DM(PT_LFO,I5);
    R1 = 0x39f4a8c1; // 32 * 0.7 / 48000
    F0 = F0 + F1;
    R1 = 0x3f800000;
    COMP(F0,F1);
    IF LT JUMP _pt_lfo_store;
    F0 = F0 - F1;
_pt_lfo_store:
    DM(PT_LFO,I5) = R0;
    R1 = 0x3f000000;
    F0 = F0 - F1;
    F1 = -F0;
    F0 = MAX(F0,F1);
    R1 = 0x40800000;
    F0 = F0 * F1;
    R1 = 0x3f800000;
    F4 = F0 - F1; // triangle -1..1
    R0 = LSHIFT R15 BY -12;
    CALL _pt_field;
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3bc49ba6; // 0.006: the field reaches 0.5, so 0.3 %
    F0 = F0 * F1;
    F0 = F0 * F4;
    R1 = 0x3f800000;
    F4 = F0 + F1; // 1 + wow
    // The delay in frames, between the shortest and half the tape (a reverse segment reads twice its length back).
    R0 = DM(PC_TIME,I5);
    F12 = FLOAT R0;
    F12 = F12 * F4;
    R0 = PRISM_TAPE_MIN;
    F0 = FLOAT R0;
    F12 = MAX(F12,F0);
    R0 = DM(PS_CAPACITY,I5);
    R1 = 128;
    R0 = R0 - R1;
    R0 = LSHIFT R0 BY -1;
    F5 = FLOAT R0; // the longest delay
    F12 = MIN(F12,F5);
    // SKEW lengthens one side by up to half: negative the left, positive the right.
    R0 = LSHIFT R15 BY -2;
    CALL _pt_field;
    R1 = 0x3f000000;
    F0 = F0 * F1;
    R1 = 0;
    F2 = MAX(F0,F1); // the right side's share
    F3 = MIN(F0,F1);
    F3 = -F3; // the left side's share
    R1 = 0x3f800000;
    F2 = F2 + F1;
    F3 = F3 + F1;
    F2 = F2 * F12;
    F2 = MIN(F2,F5);
    DM(PS_TEMP,I5) = R2; // right target
    F12 = F12 * F3;
    F12 = MIN(F12,F5); // left target
    // A fresh state starts at the target without a glide.
    R0 = DM(PT_DINT_L,I5);
    R0 = PASS R0;
    IF NE JUMP _pt_started;
    R0 = TRUNC F12;
    DM(PT_DINT_L,I5) = R0;
    R0 = TRUNC F2;
    DM(PT_DINT_R,I5) = R0;
    R0 = 0;
    DM(PT_DFRAC_L,I5) = R0;
    DM(PT_DFRAC_R,I5) = R0;
_pt_started:
    // Can every read of this block reach written tape? The deepest read is the larger of the current and the target
    // delays (twice in REVERSE, and a block more), and the slew moves at most 16 frames in a block.
    R0 = DM(PS_ROLL_BANK,I5);
    R0 = PASS R0;
    IF EQ JUMP _pt_silent;
    R2 = DM(PT_DINT_L,I5);
    R3 = DM(PT_DINT_R,I5);
    COMP(R2,R3);
    IF LT R2 = PASS R3;
    R3 = TRUNC F12;
    COMP(R2,R3);
    IF LT R2 = PASS R3;
    R3 = DM(PS_TEMP,I5);
    R3 = TRUNC F3;
    COMP(R2,R3);
    IF LT R2 = PASS R3;
    R3 = DM(PT_LENGTH_L,I5);
    COMP(R2,R3);
    IF LT R2 = PASS R3;
    R3 = DM(PT_LENGTH_R,I5);
    COMP(R2,R3);
    IF LT R2 = PASS R3;
    R15 = DM(PT_MODE,I5);
    R3 = PT_REVERSE;
    COMP(R15,R3);
    IF NE JUMP _pt_need;
    R2 = LSHIFT R2 BY 1;
_pt_need:
    R3 = 80;
    R2 = R2 + R3;
    R1 = DM(PT_VALID,I5);
    R1 = LSHIFT R1 BY 5;
    COMP(R2,R1);
    IF GT JUMP _pt_silent;

    R10 = DM(PS_WRITE_BLOCK,I5);
    R10 = LSHIFT R10 BY 5;
    R13 = DM(PS_CAPACITY,I5);
    R11 = DM(PS_ROLL_BANK,I5);
    R0 = I5;
    R1 = PT_OUT * 4;
    R0 = R0 + R1;
    I1 = R0;
    R8 = DM(PT_DINT_L,I5);
    R9 = DM(PT_DFRAC_L,I5);
    R6 = DM(PT_COUNT_L,I5);
    R7 = DM(PT_LENGTH_L,I5);
    CALL _pt_channel;
    DM(PT_DINT_L,I5) = R8;
    DM(PT_DFRAC_L,I5) = R9;
    DM(PT_COUNT_L,I5) = R6;
    DM(PT_LENGTH_L,I5) = R7;
    R0 = LSHIFT R13 BY 2;
    R11 = R11 + R0;
    R12 = DM(PS_TEMP,I5);
    R8 = DM(PT_DINT_R,I5);
    R9 = DM(PT_DFRAC_R,I5);
    R6 = DM(PT_COUNT_R,I5);
    R7 = DM(PT_LENGTH_R,I5);
    CALL _pt_channel;
    DM(PT_DINT_R,I5) = R8;
    DM(PT_DFRAC_R,I5) = R9;
    DM(PT_COUNT_R,I5) = R6;
    DM(PT_LENGTH_R,I5) = R7;

    // A head that comes back after silence fades in over 256 samples.
    R0 = 1;
    DM(PT_READABLE,I5) = R0;
    F4 = DM(PT_FADE,I5);
    R5 = 0x3f800000;
    COMP(F4,F5);
    IF GE JUMP _pt_post;
    R0 = I5;
    R1 = PT_OUT * 4;
    R0 = R0 + R1;
    I1 = R0;
    R6 = R4;
    CALL _pt_fade_in;
    R4 = R6;
    CALL _pt_fade_in;
    DM(PT_FADE,I5) = R4;

_pt_post:
    // At an event the output would jump from the last sample to this block's first: carry the difference.
    R0 = DM(PS_TEMP1,I5);
    R0 = PASS R0;
    IF EQ JUMP _pt_heal_run;
    R2 = 0x3b800000; // 1/256
    F0 = DM(PT_LAST_L,I5);
    F1 = DM(PT_OUT,I5);
    F0 = F0 - F1;
    DM(PT_HEAL_L,I5) = R0;
    F0 = F0 * F2;
    DM(PT_HEAL_STEP_L,I5) = R0;
    F0 = DM(PT_LAST_R,I5);
    F1 = DM(PT_OUT + 32,I5);
    F0 = F0 - F1;
    DM(PT_HEAL_R,I5) = R0;
    F0 = F0 * F2;
    DM(PT_HEAL_STEP_R,I5) = R0;
    R0 = 256;
    DM(PT_HEAL_COUNT,I5) = R0;
_pt_heal_run:
    R0 = DM(PT_HEAL_COUNT,I5);
    R0 = PASS R0;
    IF EQ JUMP _pt_healed;
    R1 = 32;
    R0 = R0 - R1;
    DM(PT_HEAL_COUNT,I5) = R0;
    R0 = I5;
    R1 = PT_OUT * 4;
    R0 = R0 + R1;
    I1 = R0;
    F4 = DM(PT_HEAL_L,I5);
    F5 = DM(PT_HEAL_STEP_L,I5);
    CALL _pt_heal;
    DM(PT_HEAL_L,I5) = R4;
    F4 = DM(PT_HEAL_R,I5);
    F5 = DM(PT_HEAL_STEP_R,I5);
    CALL _pt_heal;
    DM(PT_HEAL_R,I5) = R4;
_pt_healed:
    R0 = DM(PT_OUT + 31,I5);
    DM(PT_LAST_L,I5) = R0;
    R0 = DM(PT_OUT + 63,I5);
    DM(PT_LAST_R,I5) = R0;

    // Feedback: a DC blocker, a one-pole low pass near 3 kHz, the gain (ramped over the block from the last block's)
    // and a soft limit: 0.75 * (t - t^3/3) with t = 4/3 of the signal, which is unity for small signals and half of
    // full scale at most. Repeats at high feedback settle near the level of the input instead of building up to
    // several times it, and a feedback above 1 saturates smoothly.
    R0 = I5;
    R1 = PT_OUT * 4;
    R0 = R0 + R1;
    I0 = R0;
    R1 = (PT_FEEDBACK - PT_OUT) * 4;
    R0 = R0 + R1;
    I1 = R0;
    F10 = DM(PT_FDBK_NOW,I5);
    F0 = DM(PT_FDBK,I5);
    DM(PT_FDBK_NOW,I5) = R0;
    F1 = F0 - F10;
    R2 = 0x3d000000;
    F4 = F1 * F2; // the gain's step per sample
    R9 = R10; // the right side starts from the same gain
    R11 = 0x3f7eb852; // 0.995
    R12 = 0x3ea8f5c3; // 0.33: the low pass
    R13 = 0x3f800000;
    R14 = 0xbf800000;
    R15 = 0x3eaaaaab; // 1/3
    R2 = 0x3faaaaab; // 4/3
    R3 = 0x3f400000; // 0.75
    F6 = DM(PT_LP_L,I5);
    F7 = DM(PT_HP_X_L,I5);
    F8 = DM(PT_HP_Y_L,I5);
    CALL _pt_feedback;
    DM(PT_LP_L,I5) = R6;
    DM(PT_HP_X_L,I5) = R7;
    DM(PT_HP_Y_L,I5) = R8;
    R10 = R9;
    F6 = DM(PT_LP_R,I5);
    F7 = DM(PT_HP_X_R,I5);
    F8 = DM(PT_HP_Y_R,I5);
    CALL _pt_feedback;
    DM(PT_LP_R,I5) = R6;
    DM(PT_HP_X_R,I5) = R7;
    DM(PT_HP_Y_R,I5) = R8;
    RTS;

_pt_silent:
    // The head reads nothing. If it was playing, that is a jump from its last sample: the common tail runs it out and
    // keeps the feedback filters going on what is left.
    R0 = I5;
    R1 = PT_OUT * 4;
    R0 = R0 + R1;
    I1 = R0;
    R0 = 0;
    LCNTR = 64, DO _pt_silent_out UNTIL LCE;
_pt_silent_out: DM(I1,M6) = R0;
    DM(PT_COUNT_L,I5) = R0;
    DM(PT_COUNT_R,I5) = R0;
    DM(PT_LENGTH_L,I5) = R0;
    DM(PT_LENGTH_R,I5) = R0;
    DM(PT_FADE,I5) = R0;
    R1 = DM(PT_READABLE,I5);
    DM(PT_READABLE,I5) = R0;
    R1 = PASS R1;
    IF EQ JUMP _pt_post;
    R1 = 1;
    DM(PS_TEMP1,I5) = R1;
    JUMP _pt_post;

// I1 one channel of 32 samples (advanced), F4 the gain before its first sample, F5 one: the gain rises by 1/256 a sample
// to 1. Returns the gain in F4.
_pt_fade_in:
    R2 = 0x3b800000;
    LCNTR = 32, DO _pt_fade_sample UNTIL LCE;
        F4 = F4 + F2;
        F4 = MIN(F4,F5);
        F0 = DM(I1,M5);
        F0 = F0 * F4;
_pt_fade_sample: DM(I1,M6) = R0;
    RTS;

// I1 one channel of 32 samples (advanced), F4 the carried difference and F5 its step: added, and stepped down.
_pt_heal:
    LCNTR = 32, DO _pt_heal_sample UNTIL LCE;
        F0 = DM(I1,M5);
        F0 = F0 + F4;
        F4 = F4 - F5;
_pt_heal_sample: DM(I1,M6) = R0;
    RTS;

// R0 a ten-bit signed field in its low bits (higher bits ignored): R2 = the field, F0 = field / 512, -1 <= F0 < 1.
_pt_field:
    R1 = 0x3ff;
    R0 = R0 AND R1;
    R1 = 512;
    COMP(R0,R1);
    IF LT JUMP _pt_field_positive;
    R1 = 1024;
    R0 = R0 - R1;
_pt_field_positive:
    R2 = R0;
    F0 = FLOAT R0;
    R1 = 0x3b000000;
    F0 = F0 * F1;
    RTS;

// One channel of the head. R8 delay frames, R9 its fraction (float), R6 segment counter, R7 segment length, R10 the write
// position in frames, R11 the channel's plane, R12 the target delay (float), R13 the tape's frames, R15 the mode, I1 the
// output (advanced by 32). R6 to R9 return the state. Uses R0 to R5, R14, I2, I3.
_pt_channel:
    R14 = 0;
_pt_sample:
    F0 = FLOAT R8;
    F0 = F0 + F9;
    F1 = F12 - F0;
    R2 = 0x3a800000; // 1/1024 of the distance per sample
    F1 = F1 * F2;
    R2 = 0x3f000000;
    F1 = MIN(F1,F2);
    R2 = 0xbf000000;
    F1 = MAX(F1,F2);
    F9 = F9 + F1;
    R2 = 0x3f800000;
    COMP(F9,F2);
    IF LT JUMP _pt_no_carry;
    F9 = F9 - F2;
    R8 = R8 + 1;
_pt_no_carry:
    R2 = 0;
    COMP(F9,F2);
    IF GE JUMP _pt_no_borrow;
    R2 = 0x3f800000;
    F9 = F9 + F2;
    R8 = R8 - 1;
_pt_no_borrow:
    R2 = (PRISM_TAPE_MIN - 8);
    COMP(R8,R2);
    IF LT R8 = PASS R2;
    R0 = PT_FORWARD;
    COMP(R15,R0);
    IF EQ JUMP _pt_forward;
    // REVERSE and HOLD read whole frames of a segment of R7 frames; a new segment takes the current delay.
    R7 = PASS R7;
    IF NE JUMP _pt_segment;
    R7 = R8;
    R6 = 0;
_pt_segment:
    R0 = PT_HOLD;
    COMP(R15,R0);
    IF EQ JUMP _pt_hold;
    // REVERSE: counter c reads 2c + 34 frames behind this sample's write position: one block late, so that no read
    // falls into the block that is written after the head ran.
    R0 = LSHIFT R6 BY 1;
    R1 = 34;
    R0 = R0 + R1;
    R1 = R10 + R14;
    R0 = R1 - R0;
    JUMP _pt_window;
_pt_hold:
    // HOLD: the tape stands, so the write position does too: the loop is the R7 frames before it.
    R0 = R10 - R7;
    R0 = R0 + R6;
_pt_window:
    // min(c, length - 1 - c) / edge, at most 1: the first and last frame of a segment are silent. The edge is a
    // quarter of the segment in powers of two, 16 to 256 frames: about 5 ms for all but short delays.
    R1 = R7 - R6;
    R1 = R1 - 1;
    COMP(R1,R6);
    IF GT R1 = PASS R6;
    R2 = -8;
    R3 = 1024;
    COMP(R7,R3);
    IF GE JUMP _pt_edge;
    R2 = -7;
    R3 = 512;
    COMP(R7,R3);
    IF GE JUMP _pt_edge;
    R2 = -6;
    R3 = 256;
    COMP(R7,R3);
    IF GE JUMP _pt_edge;
    R2 = -5;
    R3 = 128;
    COMP(R7,R3);
    IF GE JUMP _pt_edge;
    R2 = -4;
_pt_edge:
    F4 = FLOAT R1 BY R2;
    R2 = 0x3f800000;
    F4 = MIN(F4,F2);
    R5 = 0;
    R6 = R6 + 1;
    COMP(R6,R7);
    IF LT JUMP _pt_index;
    R6 = 0;
    R2 = PT_REVERSE;
    COMP(R15,R2);
    IF EQ R7 = PASS R8;
    JUMP _pt_index;
_pt_forward:
    R0 = R10 + R14;
    R0 = R0 - R8;
    R0 = R0 - 1;
    R4 = 0x3f800000;
    F5 = F4 - F9; // the weight of the later frame
_pt_index:
    R0 = PASS R0;
    IF GE JUMP _pt_index_low;
    R0 = R0 + R13;
_pt_index_low:
    R1 = R0 + 1;
    COMP(R1,R13);
    IF LT JUMP _pt_index_high;
    R1 = 0;
_pt_index_high:
    // A whole-frame read (weight 0) takes no neighbour: the frame after a loop's last one may not be written yet.
    R5 = PASS R5;
    IF EQ R1 = PASS R0;
    R2 = LSHIFT R0 BY 2;
    R3 = R11 + R2;
    I2 = R3;
    R2 = LSHIFT R1 BY 2;
    R3 = R11 + R2;
    I3 = R3;
    F0 = DM(0,I2);
    F1 = DM(0,I3);
    F2 = F1 - F0;
    F1 = F2 * F5;
    F0 = F0 + F1;
    F0 = F0 * F4;
    DM(I1,M6) = R0;
    R14 = R14 + 1;
    R2 = 32;
    COMP(R14,R2);
    IF LT JUMP _pt_sample;
    RTS;

// I0 the head's output, I1 the feedback (both advanced by 32). F6 low pass state, F7 and F8 the DC blocker's last input
// and output, F10 the gain and F4 its step per sample; R11 to R15 hold 0.995, the low pass coefficient, 1, -1 and 1/3,
// F2 4/3 and F3 0.75.
_pt_feedback:
    LCNTR = 32, DO _pt_feedback_sample UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = F0 - F7;
        R7 = R0;
        F8 = F8 * F11;
        F8 = F8 + F1;
        F1 = F8 - F6;
        F1 = F1 * F12;
        F6 = F6 + F1;
        F10 = F10 + F4;
        F0 = F6 * F10;
        F0 = F0 * F2;
        F0 = MIN(F0,F13);
        F0 = MAX(F0,F14);
        F1 = F0 * F0;
        F1 = F1 * F0;
        F1 = F1 * F15;
        F0 = F0 - F1;
        F0 = F0 * F3;
_pt_feedback_sample: DM(I1,M6) = R0;
    RTS;

// The effect's output before SPACE: the grains (already in the output) and the head, by BALANCE, then DRIVE. The two gains
// and the drive move from the last block's values to this block's over its 32 samples.
_prism_mix:
    R0 = DM(PC_TAPE,I5);
    R0 = LSHIFT R0 BY -22;
    CALL _pt_field;
    // Both are full in the middle; the balance takes one side down, and its end values switch that side off exactly.
    R6 = R2;
    R1 = 0x3f800000;
    F2 = F1 - F0;
    F2 = MIN(F2,F1);
    R3 = 0;
    F2 = MAX(F2,F3); // the head
    F4 = F1 + F0;
    F4 = MIN(F4,F1);
    F4 = MAX(F4,F3); // the grains
    R0 = 511;
    COMP(R6,R0);
    IF GE R2 = PASS R3;
    R0 = -511;
    COMP(R6,R0);
    IF LE R4 = PASS R3;
    F5 = DM(PC_DRIVE,I5);
    F5 = MAX(F5,F3);
    F5 = MIN(F5,F1);
    // PS_TEMP and PS_TEMP1: the gains of the head and of the grains before this block; F9 and F11 their steps.
    R7 = 0x3d000000;
    F8 = DM(PT_GAIN_HEAD,I5);
    DM(PT_GAIN_HEAD,I5) = R2;
    DM(PS_TEMP,I5) = R8;
    F2 = F2 - F8;
    F9 = F2 * F7;
    F10 = DM(PT_GAIN_GRAIN,I5);
    DM(PT_GAIN_GRAIN,I5) = R4;
    DM(PS_TEMP1,I5) = R10;
    F4 = F4 - F10;
    F11 = F4 * F7;
    F12 = DM(PT_DRIVE,I5);
    DM(PT_DRIVE,I5) = R5;
    I0 = DM(PS_OUTPUT,I5);
    R0 = I5;
    R1 = PT_OUT * 4;
    R0 = R0 + R1;
    I1 = R0;
    // No drive now and none before: the plain sum.
    F0 = F12 + F5;
    COMP(F0,F3);
    IF GT JUMP _pm_drive;
    CALL _pm_clean_channel;
    CALL _pm_clean_channel;
    RTS;
_pm_clean_channel:
    F2 = DM(PS_TEMP,I5);
    F4 = DM(PS_TEMP1,I5);
    LCNTR = 32, DO _pm_clean UNTIL LCE;
        F2 = F2 + F9;
        F4 = F4 + F11;
        F0 = DM(I0,M5);
        F0 = F0 * F4;
        F1 = DM(I1,M6);
        F1 = F1 * F2;
        F0 = F0 + F1;
_pm_clean: DM(I0,M6) = R0;
    RTS;
_pm_drive:
    // Gain 1..8 into the same soft limit as the feedback; half the gain goes in and the limit's output is doubled.
    // PS_TEMP2: that half gain before this block, F7 its step. With no drive the curve still bends (x - x^3/12), so
    // the driven signal is blended in: PS_TEMP3 is the blend before this block (1 where the drive was on), F12 its
    // step. It moves only in a block where the drive goes on or off.
    R13 = 0x3f800000;
    R14 = 0xbf800000;
    R15 = 0x3eaaaaab;
    R1 = 0x40e00000;
    R0 = 0x3f000000;
    F6 = F12 * F1;
    F6 = F6 + F13;
    F6 = F6 * F0;
    DM(PS_TEMP2,I5) = R6;
    F2 = F5 * F1;
    F2 = F2 + F13;
    F2 = F2 * F0;
    F2 = F2 - F6;
    F7 = F2 * F7;
    R2 = 0;
    COMP(F12,F3);
    IF GT R2 = PASS R13;
    DM(PS_TEMP3,I5) = R2;
    R4 = 0;
    COMP(F5,F3);
    IF GT R4 = PASS R13;
    F4 = F4 - F2;
    R0 = 0x3d000000;
    F12 = F4 * F0;
    CALL _pm_driven_channel;
    CALL _pm_driven_channel;
    RTS;
_pm_driven_channel:
    F2 = DM(PS_TEMP,I5);
    F4 = DM(PS_TEMP1,I5);
    F5 = DM(PS_TEMP2,I5);
    F3 = DM(PS_TEMP3,I5);
    LCNTR = 32, DO _pm_driven UNTIL LCE;
        F2 = F2 + F9;
        F4 = F4 + F11;
        F5 = F5 + F7;
        F3 = F3 + F12;
        F0 = DM(I0,M5);
        F0 = F0 * F4;
        F1 = DM(I1,M6);
        F1 = F1 * F2;
        F0 = F0 + F1;
        F1 = F0 * F5;
        F1 = MIN(F1,F13);
        F1 = MAX(F1,F14);
        F6 = F1 * F1;
        F6 = F6 * F1;
        F6 = F6 * F15;
        F1 = F1 - F6;
        F1 = F1 + F1;
        F1 = F1 - F0;
        F1 = F1 * F3;
        F0 = F0 + F1;
_pm_driven: DM(I0,M6) = R0;
    RTS;
