// PRISM repeat path around the heads: the colour filter in front of HALO, and behind it the output, the feedback and the
// write to the tape.
//
// Colour (_prism_colour), on PS_Y in place, one chain at every position of COLR:
//   high pass   y = (1 - k / 2) (x - lp), then lp += k (x - lp): one pole, exactly 1 at the top of the band and less
//               below it, so a repeat at unity never grows through it (2 Hz where the repeats are uncoloured: only DC
//               goes)
//   low pass    one pole, lp += k (y - lp); k = 1 passes the signal as it is, and k above 1 lifts the top
//   saturation  y - w t^3 / 3 with t = y limited to +-1; none at w = 0
// The three numbers come from a table of nine rows over COLR (_prism_colours, below), straight lines between the rows.
// COLR moves smoothly (PRISM_SMOOTH), and the three numbers move through a block in 32 equal steps, so a change is no
// kink.
//
// Loop (_prism_loop): the output is the wet signal times MIX. The feedback is the wet signal times the gain of RPTS
// through a limiter that is exact up to PRISM_LIMIT_KNEE and bends to a ceiling of twice that, so a gain of 1 keeps a
// repeat below the knee for ever and a gain above 1 settles under the ceiling. The tape takes input + feedback, unless it
// stands (HOLD). After a release the write crossfades from what stands on the tape into the new signal.
#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_colour;
.GLOBAL _prism_loop;
.EXTERN _pr_smooth;

_prism_colour:
    F12 = DM(PC_COLR,I5);
    R1 = 0x3f800000;
    F12 = MIN(F12,F1);
    R1 = 0xbf800000;
    F12 = MAX(F12,F1);
    F11 = DM(PS_COLOUR,I5);
    R0 = R12;
    F1 = DM(PS_COLOUR_MID,I5);
    R2 = R11;
    CALL _pr_smooth;
    DM(PS_COLOUR_MID,I5) = R1;
    DM(PS_COLOUR,I5) = R0;
    // The numbers at the block's end, then at its start (COLR as the last block left it), and their steps.
    CALL _pc_numbers;
    R10 = R9;
    R12 = R5;
    R13 = R2;
    R0 = R11;
    CALL _pc_numbers;
    R1 = 0x3d000000; // 1/32
    F10 = F10 - F9;
    F10 = F10 * F1;
    F12 = F12 - F5;
    F12 = F12 * F1;
    F13 = F13 - F2;
    F13 = F13 * F1;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I0 = R0;
    R0 = I5;
    R1 = PS_FILT_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    R14 = 0x3f800000;
    R15 = 0xbf800000;
    CALL _pc_side;
    MODIFY(I3,PS_FILT_R - PS_FILT_L) (NW);
    CALL _pc_side;
    RTS;

// The numbers of COLR F0 (-1..1): F9 the low pass's coefficient, F5 the high pass's, F2 a third of the saturation.
// Uses F0, F1, F6, F7 and I4.
_pc_numbers:
    R1 = 0x3f800000;
    F0 = F0 + F1;
    R1 = 0x40800000;
    F0 = F0 * F1; // 0..8: the row and the way to the next one
    R1 = TRUNC F0;
    R6 = PRISM_COLOUR_ROWS - 2;
    COMP(R1,R6);
    IF GT R1 = PASS R6;
    F6 = FLOAT R1;
    F0 = F0 - F6;
    R6 = LSHIFT R1 BY 3;
    R1 = LSHIFT R1 BY 2;
    R1 = R1 + R6;
    R6 = _prism_colours;
    R1 = R1 + R6;
    I4 = R1;
    F6 = DM(0,I4);
    F7 = DM(3,I4);
    F7 = F7 - F6;
    F7 = F7 * F0;
    F9 = F6 + F7;
    F6 = DM(1,I4);
    F7 = DM(4,I4);
    F7 = F7 - F6;
    F7 = F7 * F0;
    F5 = F6 + F7;
    F6 = DM(2,I4);
    F7 = DM(5,I4);
    F7 = F7 - F6;
    F7 = F7 * F0;
    F2 = F6 + F7;
    RTS;

// One side, 32 samples at I0 in place (advanced), I3 its filters. F9, F5 and F2 are the numbers before the block and F10,
// F12 and F13 their steps (kept for the other side); F14 and F15 are +-1. Uses F0, F1, F3, F4, F6, F7, F8 and F11.
_pc_side:
    F7 = DM(FL_HIGH,I3);
    F8 = DM(FL_LOW,I3);
    R4 = R9;
    R3 = R5;
    R11 = R2;
    R6 = 0x3f000000;
    LCNTR = 32, DO _pc_sample UNTIL LCE;
        F0 = DM(I0,M5);
        F3 = F3 + F12;
        F1 = F0 - F7;
        F0 = F1 * F3;
        F7 = F7 + F0;
        F0 = F0 * F6;
        F0 = F1 - F0;
        F4 = F4 + F10;
        F1 = F0 - F8;
        F1 = F1 * F4;
        F8 = F8 + F1;
        F11 = F11 + F13;
        F0 = MIN(F8,F14);
        F0 = MAX(F0,F15);
        F1 = F0 * F0;
        F1 = F1 * F0;
        F1 = F1 * F11;
        F0 = F8 - F1;
_pc_sample: DM(I0,M6) = R0;
    DM(FL_HIGH,I3) = R7;
    DM(FL_LOW,I3) = R8;
    RTS;

_prism_loop:
    // RPTS: 0 .. 0.75 is a gain of 0 .. 1, 0.75 .. 1 a gain of 1 .. 1.1. The gain and MIX move smoothly (PRISM_SMOOTH).
    F0 = DM(PC_RPTS,I5);
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    R1 = 0x3f400000; // 0.75
    COMP(F0,F1);
    IF GE JUMP _lp_above;
    R1 = 0x3faaaaab; // 4/3
    F0 = F0 * F1;
    JUMP _lp_gain;
_lp_above:
    F0 = F0 - F1;
    R1 = 0x3ecccccd; // 0.4
    F0 = F0 * F1;
    R1 = 0x3f800000;
    F0 = F0 + F1;
_lp_gain:
    F1 = DM(PS_GAIN_MID,I5);
    F2 = DM(PS_GAIN,I5);
    DM(PS_TEMP,I5) = R2;
    CALL _pr_smooth;
    DM(PS_GAIN_MID,I5) = R1;
    DM(PS_GAIN,I5) = R0;
    F1 = DM(PS_TEMP,I5);
    R2 = 0x3d000000;
    F0 = F0 - F1;
    F4 = F0 * F2;
    F0 = DM(PC_MIX,I5);
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    F1 = DM(PS_MIX_MID,I5);
    F2 = DM(PS_MIX,I5);
    DM(PS_TEMP1,I5) = R2;
    CALL _pr_smooth;
    DM(PS_MIX_MID,I5) = R1;
    DM(PS_MIX,I5) = R0;
    F1 = DM(PS_TEMP1,I5);
    R2 = 0x3d000000;
    F0 = F0 - F1;
    F5 = F0 * F2;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I0 = R0;
    R1 = (PS_FB - PS_Y) * 4;
    R0 = R0 + R1;
    I1 = R0;
    I2 = DM(PS_OUTPUT,I5);
    R6 = 0x40fccccd; // 7.9: below the framework's bound of 8
    R7 = 0xc0fccccd;
    R12 = PRISM_LIMIT_KNEE;
    R13 = PRISM_LIMIT_SLOPE;
    R14 = PRISM_LIMIT_SPAN;
    R15 = 0x3eaaaaab; // 1/3
    CALL _lp_side;
    CALL _lp_side;
    // Rest (prism.s): the frames in a row whose wet signal was silence. They are counted only while the whole written tape
    // is silence, so a block that plays does not pay for the look at its wet signal.
    R2 = 0;
    R0 = DM(PS_TAPE_QUIET,I5);
    R1 = DM(PS_VALID,I5);
    COMP(R0,R1);
    IF LT JUMP _lp_quiet;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I0 = R0;
    R3 = 0;
    LCNTR = 64, DO _lp_quiet_last UNTIL LCE;
        F0 = DM(I0,M6);
        F0 = ABS F0;
_lp_quiet_last: F3 = MAX(F3,F0);
    R0 = PRISM_IDLE_LEVEL;
    COMP(F3,F0);
    IF GE JUMP _lp_quiet;
    R2 = DM(PS_QUIET,I5);
    R0 = 32;
    R2 = R2 + R0;
    R0 = PRISM_IDLE_LATE;
    R2 = MIN(R2,R0);
_lp_quiet:
    DM(PS_QUIET,I5) = R2;

    // ---- the tape
    R0 = DM(PS_HOLD,I5);
    R1 = 2;
    COMP(R0,R1);
    IF EQ RTS;
    CALL _lp_ping_route;
    R10 = DM(PS_WPOS,I5);
    R0 = LSHIFT R10 BY 2;
    R1 = DM(PS_BANK,I5);
    R0 = R0 + R1;
    I1 = R0;
    R1 = DM(PS_CAPACITY,I5);
    R1 = LSHIFT R1 BY 2;
    R0 = R0 + R1;
    I2 = R0;
    I0 = DM(PS_INPUT,I5);
    R0 = I5;
    R1 = PS_FB * 4;
    R0 = R0 + R1;
    I3 = R0;
    R9 = DM(PS_WFADE,I5);
    R9 = PASS R9;
    IF NE JUMP _lp_write_fade;
    // F3: the largest magnitude written (rest, prism.s)
    R3 = 0;
    LCNTR = 32, DO _lp_write_left UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
        F2 = ABS F0;
        F3 = MAX(F3,F2);
_lp_write_left: DM(I1,M6) = R0;
    LCNTR = 32, DO _lp_write_right UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
        F2 = ABS F0;
        F3 = MAX(F3,F2);
_lp_write_right: DM(I2,M6) = R0;
    // the frames written in a row that were silence, up to the tape's length
    R0 = DM(PS_TAPE_QUIET,I5);
    R1 = 32;
    R0 = R0 + R1;
    R1 = DM(PS_CAPACITY,I5);
    R0 = MIN(R0,R1);
    R2 = PRISM_IDLE_LEVEL;
    COMP(F3,F2);
    IF GE R0 = R0 - R0;
    DM(PS_TAPE_QUIET,I5) = R0;
    JUMP _lp_advance;
_lp_write_fade:
    // the crossfade writes a mix with what stood on the tape: not counted as silence
    R0 = 0;
    DM(PS_TAPE_QUIET,I5) = R0;
    // The new signal's share rises by 1 / PRISM_WFADE a frame; what stands on the tape gives way to it.
    R0 = PRISM_WFADE;
    R0 = R0 - R9;
    F8 = FLOAT R0;
    R4 = PRISM_WFADE_STEP;
    F8 = F8 * F4;
    R5 = R8;
    LCNTR = 32, DO _lp_fade_left UNTIL LCE;
        F8 = F8 + F4;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
        F2 = DM(I1,M5);
        F0 = F0 - F2;
        F0 = F0 * F8;
        F0 = F0 + F2;
_lp_fade_left: DM(I1,M6) = R0;
    R8 = R5;
    LCNTR = 32, DO _lp_fade_right UNTIL LCE;
        F8 = F8 + F4;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
        F2 = DM(I2,M5);
        F0 = F0 - F2;
        F0 = F0 * F8;
        F0 = F0 + F2;
_lp_fade_right: DM(I2,M6) = R0;
    R0 = 32;
    R9 = R9 - R0;
    DM(PS_WFADE,I5) = R9;
_lp_advance:
    R0 = 32;
    R10 = R10 + R0;
    R1 = DM(PS_CAPACITY,I5);
    COMP(R10,R1);
    IF GE R10 = R10 - R1;
    DM(PS_WPOS,I5) = R10;
    R2 = DM(PS_VALID,I5);
    R2 = R2 + R0;
    COMP(R2,R1);
    IF GT R2 = PASS R1;
    DM(PS_VALID,I5) = R2;
    // HOLD taken: these were frames behind the loop; after the last of them the tape stands.
    R2 = DM(PS_HOLD,I5);
    R2 = PASS R2;
    IF EQ RTS;
    R2 = DM(PS_POST,I5);
    R2 = R2 - R0;
    IF LE JUMP _lp_stand;
    DM(PS_POST,I5) = R2;
    RTS;
_lp_stand:
    R2 = 0;
    DM(PS_POST,I5) = R2;
    R2 = 2;
    DM(PS_HOLD,I5) = R2;
    RTS;

// PONG sums the input to mono at the left head. Explicit SWAP preserves
// stereo input at any skew. A legacy PONG flag with a nonzero skew also
// preserves stereo. Both modes cross feedback over a 256-frame smoothstep.
_lp_ping_route:
    R0 = DM(PC_FLAGS,I5);
    R1 = PH_F_PING_PONG + PH_F_SWAP;
    R0 = R0 AND R1;
    R6 = 0;
    R7 = 0;
    R0 = PASS R0;
    IF EQ JUMP _lp_ping_feedback;
    R6 = 0x3f800000;
    R1 = PH_F_PING_PONG;
    COMP(R0,R1);
    IF NE JUMP _lp_ping_feedback;
    F0 = DM(PC_SKEW,I5);
    COMP(F0,F7);
    IF EQ R7 = PASS R6;
_lp_ping_feedback:
    R0 = R6;
    F1 = DM(PS_PING_FB,I5);
    CALL _lp_ping_ramp;
    DM(PS_PING_FB,I5) = R0;
    R3 = R1 OR R2;
    R3 = PASS R3;
    IF EQ JUMP _lp_ping_mono;
    R3 = I5;
    R4 = PS_FB * 4;
    R3 = R3 + R4;
    I0 = R3;
    R4 = 128;
    R3 = R3 + R4;
    I1 = R3;
    R2 = PASS R2;
    IF EQ JUMP _lp_ping_swap;
    R8 = 0x40400000;
    LCNTR = 32, DO _lp_ping_fb_last UNTIL LCE;
        F1 = F1 + F2;
        F3 = F1 + F1;
        F3 = F8 - F3;
        F4 = F1 * F1;
        F4 = F4 * F3;
        F3 = DM(I0,M5);
        F5 = DM(I1,M5);
        F0 = F5 - F3;
        F0 = F0 * F4;
        F3 = F3 + F0;
        F5 = F5 - F0;
        DM(I0,M6) = R3;
_lp_ping_fb_last: DM(I1,M6) = R5;
    JUMP _lp_ping_mono;
_lp_ping_swap:
    LCNTR = 32, DO _lp_ping_swap_last UNTIL LCE;
        R0 = DM(I0,M5);
        R3 = DM(I1,M5);
        DM(I0,M6) = R3;
_lp_ping_swap_last: DM(I1,M6) = R0;

_lp_ping_mono:
    R0 = R7;
    F1 = DM(PS_PING_MONO,I5);
    CALL _lp_ping_ramp;
    DM(PS_PING_MONO,I5) = R0;
    R3 = R1 OR R2;
    R3 = PASS R3;
    IF EQ RTS;
    // The source can be PS_IN already after the INPT crossfade. Read both
    // channels of each frame before writing that frame's two outputs.
    I0 = DM(PS_INPUT,I5);
    R0 = I0;
    R3 = 128;
    R0 = R0 + R3;
    I1 = R0;
    R0 = I5;
    R4 = PS_IN * 4;
    R0 = R0 + R4;
    DM(PS_INPUT,I5) = R0;
    I2 = R0;
    R0 = R0 + R3;
    I3 = R0;
    R9 = 0x3f000000;
    R2 = PASS R2;
    IF EQ JUMP _lp_ping_left;
    R8 = 0x40400000;
    R6 = 0x3f800000;
    LCNTR = 32, DO _lp_ping_mono_last UNTIL LCE;
        F1 = F1 + F2;
        F3 = F1 + F1;
        F3 = F8 - F3;
        F4 = F1 * F1;
        F4 = F4 * F3;
        F3 = DM(I0,M6);
        F5 = DM(I1,M6);
        F0 = F5 - F3;
        F0 = F0 * F9;
        F0 = F0 * F4;
        F3 = F3 + F0;
        F0 = F6 - F4;
        F5 = F5 * F0;
        DM(I2,M6) = R3;
_lp_ping_mono_last: DM(I3,M6) = R5;
    RTS;
_lp_ping_left:
    R4 = 0;
    LCNTR = 32, DO _lp_ping_left_last UNTIL LCE;
        F0 = DM(I0,M6);
        F3 = DM(I1,M6);
        F0 = F0 + F3;
        F0 = F0 * F9;
        DM(I2,M6) = R0;
_lp_ping_left_last: DM(I3,M6) = R4;
    RTS;

// F0 target and F1 prior blend. Return F0 end, F1 start and F2 per-sample
// step. Both controls move in exact dyadic steps and reach their endpoints.
_lp_ping_ramp:
    R2 = 0;
    R3 = DM(PS_FIRST,I5);
    R3 = PASS R3;
    IF NE R1 = PASS R0;
    COMP(F0,F1);
    IF EQ RTS;
    R2 = 0x3b800000; // 1 / 256
    IF LT F2 = -F2;
    R3 = 0x42000000; // 32 samples
    F3 = F2 * F3;
    F0 = F1 + F3;
    RTS;

// One side: I0 the wet signal, I1 the feedback, I2 the output (all advanced by 32). PS_TEMP and PS_TEMP1 the gain and MIX
// before the block, F4 and F5 their steps; F6 and F7 the output's bounds; F12 the limiter's knee, F13 its slope, F14 its
// span, F15 a third.
_lp_side:
    F10 = DM(PS_TEMP,I5);
    F11 = DM(PS_TEMP1,I5);
    R8 = 32;
_lp_sample:
    F0 = DM(I0,M6);
    F10 = F10 + F4;
    F11 = F11 + F5;
    F1 = F0 * F11;
    F1 = MIN(F1,F6);
    F1 = MAX(F1,F7);
    DM(I2,M6) = R1;
    F0 = F0 * F10;
    F1 = ABS F0;
    COMP(F1,F12);
    IF LE JUMP _lp_store;
    // Above the knee: knee + span * (u - u^3 / 3), u = (|x| - knee) * slope up to 1, with the sign of x.
    F1 = F1 - F12;
    F1 = F1 * F13;
    R2 = 0x3f800000;
    F1 = MIN(F1,F2);
    F2 = F1 * F1;
    F2 = F2 * F1;
    F2 = F2 * F15;
    F1 = F1 - F2;
    F1 = F1 * F14;
    F1 = F1 + F12;
    R0 = PASS R0;
    IF LT F1 = -F1;
    R0 = R1;
_lp_store:
    DM(I1,M6) = R0;
    R8 = R8 - 1;
    IF GT JUMP _lp_sample;
    RTS;

// COLR's table: the low pass's coefficient, the high pass's and a third of the saturation, three floats a row, nine rows
// from COLR -1 to 1 in steps of 0.25 (tests/engine.rig.test.ts checks it against the list in tests/engine-rig.ts).
.PKHEX _prism_colours, f6b19a3ddb479f3ccdcc4c3e6052093e79ad2a3cabaa2a3ed5dc6b3edc1fab3babaa2a3e611c9d3e886c373dabaaaa3e4c89d83e64c3be3cefee6e3e1f2a263f08c4d53bcdcccc3d0000803f853d893900000000295c8f3f72bb1d3d000000000000a03fa99ae33d00000000;
