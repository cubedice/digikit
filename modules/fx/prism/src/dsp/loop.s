// PRISM repeat path around the heads: the colour filter in front of HALO, and behind it the output, the feedback and the
// write to the tape.
//
// Colour (_prism_colour), on PS_Y in place: a DC blocker (2 Hz, unity above it), then one of
//   dark    y = x + w (lp(x) - x), lp a one-pole low pass at 800 Hz: the highs fall by 1 - w a pass, and at w = 1 it is
//           the low pass. Then a gentle saturation, t - w t^3 / 3 with t = y limited to +-1.
//   bright  y = x + w (hp(x) - x), hp a one-pole high pass at 1 kHz: the lows fall by 1 - w a pass, and at w = 1 it is
//           the high pass.
//   centre  nothing.
// w is the amount on COLR's side, the square of COLR. COLR moves smoothly (PRISM_SMOOTH) and passes the centre at a block's
// end; there w has no slope, so the change of filter is no kink.
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
    R12 = R0;
    // From one side to the other: to the centre in this block.
    F0 = F11 * F12;
    R1 = 0;
    COMP(F0,F1);
    IF GE JUMP _pc_step;
    R12 = 0;
_pc_step:
    DM(PS_COLOUR,I5) = R12;
    // The amount is the square of COLR: at the centre, where the filter changes sides, it has no slope.
    F0 = ABS F11;
    F11 = F11 * F0;
    F0 = ABS F12;
    F12 = F12 * F0;
    F13 = F12 - F11;
    R1 = 0x3d000000;
    F13 = F13 * F1;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I0 = R0;
    R0 = I5;
    R1 = PS_FILT_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    R5 = PRISM_DC_COEFF;
    R3 = PRISM_DC_GAIN;
    // The side is that of whichever of the two amounts is not 0.
    F0 = F11 + F12;
    R1 = 0;
    COMP(F0,F1);
    IF LT JUMP _pc_dark;
    IF GT JUMP _pc_bright;
    CALL _pc_centre_side;
    MODIFY(I3,PS_FILT_R - PS_FILT_L) (NW);
    CALL _pc_centre_side;
    RTS;
_pc_dark:
    R9 = PRISM_DARK_COEFF;
    R14 = 0x3f800000;
    R15 = 0xbf800000;
    R2 = 0x3eaaaaab; // 1/3
    F4 = -F13;
    CALL _pc_dark_side;
    MODIFY(I3,PS_FILT_R - PS_FILT_L) (NW);
    CALL _pc_dark_side;
    RTS;
_pc_bright:
    R9 = PRISM_BRIGHT_COEFF;
    R2 = PRISM_BRIGHT_GAIN;
    CALL _pc_bright_side;
    MODIFY(I3,PS_FILT_R - PS_FILT_L) (NW);
    CALL _pc_bright_side;
    RTS;

// One side, 32 samples at I0 in place (advanced), I3 its filters, F5 and F3 the DC blocker's coefficient and gain:
// y = gain (x - x1) + coefficient y1. The colour filters are left on the signal, so that they start from it when the
// colour comes on.
_pc_centre_side:
    F6 = DM(FL_DC_X,I3);
    F7 = DM(FL_DC_Y,I3);
    LCNTR = 32, DO _pc_centre UNTIL LCE;
        F0 = DM(I0,M5);
        F1 = F0 - F6;
        R6 = R0;
        F1 = F1 * F3;
        F7 = F7 * F5;
        F7 = F7 + F1;
_pc_centre: DM(I0,M6) = R7;
    DM(FL_DC_X,I3) = R6;
    DM(FL_DC_Y,I3) = R7;
    DM(FL_DARK,I3) = R7;
    DM(FL_BRIGHT_X,I3) = R7;
    R0 = 0;
    DM(FL_BRIGHT_Y,I3) = R0;
    RTS;

// As above with the dark filter: F9 its coefficient, F11 the amount before the block (negative), F4 the step of the
// amount, F14 and F15 +-1, F2 a third. F11 is the signed amount before the block.
_pc_dark_side:
    F6 = DM(FL_DC_X,I3);
    F7 = DM(FL_DC_Y,I3);
    F8 = DM(FL_DARK,I3);
    F10 = -F11;
    LCNTR = 32, DO _pc_dark_sample UNTIL LCE;
        F0 = DM(I0,M5);
        F1 = F0 - F6;
        R6 = R0;
        F1 = F1 * F3;
        F7 = F7 * F5;
        F7 = F7 + F1;
        F1 = F7 - F8;
        F1 = F1 * F9;
        F8 = F8 + F1;
        F10 = F10 + F4;
        F1 = F8 - F7;
        F1 = F1 * F10;
        F0 = F7 + F1;
        F0 = MIN(F0,F14);
        F0 = MAX(F0,F15);
        F1 = F0 * F0;
        F1 = F1 * F0;
        F1 = F1 * F2;
        F1 = F1 * F10;
        F0 = F0 - F1;
_pc_dark_sample: DM(I0,M6) = R0;
    DM(FL_DC_X,I3) = R6;
    DM(FL_DC_Y,I3) = R7;
    DM(FL_DARK,I3) = R8;
    DM(FL_BRIGHT_X,I3) = R7;
    R0 = 0;
    DM(FL_BRIGHT_Y,I3) = R0;
    RTS;

// As above with the bright filter, hp = gain (x - x1) + coefficient hp1: F9 its coefficient and F2 its gain, F11 the
// amount before the block, F13 its step.
_pc_bright_side:
    F6 = DM(FL_DC_X,I3);
    F7 = DM(FL_DC_Y,I3);
    F12 = DM(FL_BRIGHT_X,I3);
    F8 = DM(FL_BRIGHT_Y,I3);
    R10 = R11;
    LCNTR = 32, DO _pc_bright_sample UNTIL LCE;
        F0 = DM(I0,M5);
        F1 = F0 - F6;
        R6 = R0;
        F1 = F1 * F3;
        F7 = F7 * F5;
        F7 = F7 + F1;
        F1 = F7 - F12;
        R12 = R7;
        F1 = F1 * F2;
        F8 = F8 * F9;
        F8 = F8 + F1;
        F10 = F10 + F13;
        F1 = F8 - F7;
        F1 = F1 * F10;
        F0 = F7 + F1;
_pc_bright_sample: DM(I0,M6) = R0;
    DM(FL_DC_X,I3) = R6;
    DM(FL_DC_Y,I3) = R7;
    DM(FL_BRIGHT_X,I3) = R12;
    DM(FL_BRIGHT_Y,I3) = R8;
    DM(FL_DARK,I3) = R7;
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

    // ---- the tape
    R0 = DM(PS_HOLD,I5);
    R1 = 2;
    COMP(R0,R1);
    IF EQ RTS;
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
    LCNTR = 32, DO _lp_write_left UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
_lp_write_left: DM(I1,M6) = R0;
    LCNTR = 32, DO _lp_write_right UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
_lp_write_right: DM(I2,M6) = R0;
    JUMP _lp_advance;
_lp_write_fade:
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
