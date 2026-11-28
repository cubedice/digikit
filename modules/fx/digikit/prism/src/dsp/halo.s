// PRISM HALO: the four delay lines of 97, 137, 191 and 251 samples as all-pass filters inside the repeat path, on PS_Y in
// place. A line is a lattice all-pass
//
//   y = c x + s d,   line input = s x - c d,   d the line's output,   c = cos(theta), s = sin(theta)
//
// which is (c + z^-N) / (1 + c z^-N): the direct sound with gain c, then a tail. The sum and the difference of the two
// sides pass lines 0 and 1, then the sides themselves pass lines 2 and 3. Their rotation preserves energy between the
// output and delay state. The stronger settings introduce slow motion in that rotation without moving the read heads.
// At HALO 0 the routine does not touch the signal.
#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_halo;
.GLOBAL _prism_halo_late;
.EXTERN _pr_recip;
.EXTERN _pr_smooth;

_prism_halo:
    F0 = DM(PC_HALO,I5);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    R1 = 0;
    F0 = MAX(F0,F1);
    // HALO moves smoothly (PRISM_SMOOTH) to what the control asks for.
    F1 = DM(PS_HALO_MID,I5);
    F2 = DM(PS_HALO_H,I5);
    CALL _pr_smooth;
    DM(PS_HALO_MID,I5) = R1;
    DM(PS_HALO_H,I5) = R0;
    R1 = 0;
    COMP(F0,F1);
    IF GT JUMP _hl_on;
    // HALO 0. If the lines still move toward it, this block takes them there; else nothing passes them.
    F2 = DM(PS_HALO_S,I5);
    COMP(F2,F1);
    IF NE JUMP _hl_on;
    R0 = DM(PS_HALO_ON,I5);
    R0 = PASS R0;
    IF EQ RTS;
    // Off for the first time: empty the lines, so that nothing old comes out when HALO is turned up again.
    R0 = 0;
    DM(PS_HALO_ON,I5) = R0;
    DM(PS_HALO_INDEX,I5) = R0;
    R1 = I5;
    R2 = PS_LINE0 * 4;
    R1 = R1 + R2;
    I2 = R1;
    LCNTR = 1024, DO _hl_clear UNTIL LCE;
_hl_clear: DM(I2,M6) = R0;
    RTS;
_hl_on:
    // t = tan(theta / 2); c = (1 - t^2) / (1 + t^2), s = 2 t / (1 + t^2).
    // HALO 0 is c = 1 and s = 0 exactly.
    R3 = 0x3f800000;
    R4 = 0;
    COMP(F0,F4);
    IF LE JUMP _hl_move;
    R1 = PRISM_HALO_MAX;
    F5 = F0 * F1;
    R1 = 0x3f000000;
    F6 = F0 - F1;
    IF LE JUMP _hl_coefficients;
    // Two triangular block phases at about 0.37 and 0.27 Hz at 48 kHz.
    // Only the upper half of HALO opens up the rotation and makes it wander.
    F6 = F0 * F6;
    R0 = DM(PS_BLOCK,I5);
    R2 = FEXT R0 BY 0:12;
    R1 = 2048;
    R2 = R2 - R1;
    R2 = ABS R2;
    R3 = LSHIFT R0 BY 1;
    R0 = R0 + R3;
    R3 = 3079;
    R0 = R0 + R3;
    R0 = FEXT R0 BY 0:14;
    R3 = 8192;
    R0 = R0 - R3;
    R0 = ABS R0;
    R0 = LSHIFT R0 BY -2;
    R0 = R0 + R2;
    R0 = R0 - R1;
    F2 = FLOAT R0;
    R1 = PRISM_HALO_DRIFT;
    F2 = F2 * F1;
    R1 = PRISM_HALO_OPEN;
    F2 = F2 + F1;
    F2 = F2 * F6;
    F5 = F5 + F2;
_hl_coefficients:
    F2 = F5 * F5;
    R1 = 0x3f800000;
    F3 = F1 - F2;
    F4 = F5 + F5;
    F0 = F1 + F2;
    CALL _pr_recip;
    F3 = F3 * F0;
    F4 = F4 * F0;
_hl_move:
    // c and s move over the block from where the last block left them.
    F8 = DM(PS_HALO_C,I5);
    F9 = DM(PS_HALO_S,I5);
    DM(PS_HALO_C,I5) = R3;
    DM(PS_HALO_S,I5) = R4;
    R1 = 0x3d000000;
    F3 = F3 - F8;
    F10 = F3 * F1;
    F4 = F4 - F9;
    F11 = F4 * F1;
    R0 = 1;
    DM(PS_HALO_ON,I5) = R0;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I0 = R0;
    R1 = 128;
    R0 = R0 + R1;
    I1 = R0;
    R0 = I5;
    R1 = PS_LINE0 * 4;
    R0 = R0 + R1;
    R4 = R0;
    R12 = DM(PS_HALO_INDEX,I5);
    R13 = 1020;
    R15 = 0x3f3504f3; // sqrt(1/2)
    // One sample. F8 and F9 are c and s; R12 the write index of the four lines and R14 the same in bytes; R13 the mask of
    // a line's 256 words in bytes. The four lines share a write index and occupy consecutive 256-word planes.
    // A line, F0 its input and its output: its delayed word d lies its length before the write index (both in bytes, the
    // difference masked); the line takes s x - c d, and the output is c x + s d. I2 reads and I3 writes.
    LCNTR = 32, DO _hl_sample UNTIL LCE;
        F8 = F8 + F10, F6 = DM(I0,M5);
        F9 = F9 + F11, F7 = DM(I1,M5);
        F0 = F6 + F7;
        F0 = F0 * F15;
        F7 = F6 - F7;
        F7 = F7 * F15;
        R14 = LSHIFT R12 BY 2;
        R3 = R14 + R4;
        I3 = R3;
        // the line of 97 samples
        R1 = 388;
        R1 = R14 - R1;
        R1 = R1 AND R13;
        R1 = R1 + R4;
        I2 = R1;
        F1 = DM(0,I2);
        F2 = F0 * F9;
        F3 = F1 * F8;
        F2 = F2 - F3;
        DM(0,I3) = R2;
        F0 = F0 * F8;
        F1 = F1 * F9;
        F0 = F0 + F1;
        R6 = R0;
        R0 = R7;
        // the line of 137 samples
        R1 = 548;
        R1 = R14 - R1;
        R1 = R1 AND R13;
        R1 = R1 + R4;
        I2 = R1;
        F1 = DM(256,I2);
        F2 = F0 * F9;
        F3 = F1 * F8;
        F2 = F2 - F3;
        DM(256,I3) = R2;
        F0 = F0 * F8;
        F1 = F1 * F9;
        F0 = F0 + F1;
        F7 = F6 - F0;
        F7 = F7 * F15;
        F0 = F6 + F0;
        F0 = F0 * F15;
        // the line of 191 samples
        R1 = 764;
        R1 = R14 - R1;
        R1 = R1 AND R13;
        R1 = R1 + R4;
        I2 = R1;
        F1 = DM(512,I2);
        F2 = F0 * F9;
        F3 = F1 * F8;
        F2 = F2 - F3;
        DM(512,I3) = R2;
        F0 = F0 * F8;
        F1 = F1 * F9;
        F0 = F0 + F1;
        DM(I0,M6) = R0;
        R0 = R7;
        // the line of 251 samples
        R1 = 1004;
        R1 = R14 - R1;
        R1 = R1 AND R13;
        R1 = R1 + R4;
        I2 = R1;
        F1 = DM(768,I2);
        F2 = F0 * F9;
        F3 = F1 * F8;
        F2 = F2 - F3;
        DM(768,I3) = R2;
        F0 = F0 * F8;
        F1 = F1 * F9;
        F0 = F0 + F1;
        DM(I1,M6) = R0;
        R12 = R12 + 1;
        R0 = 255;
_hl_sample: R12 = R12 AND R0;
    DM(PS_HALO_INDEX,I5) = R12;
    RTS;

// Late stereo room: two downsampled delays, one pole per return and a damped
// orthogonal feedback matrix. Excite it from the early diffuser's PS_Y after the
// tape writer has used it, then add it to the existing MIX-scaled FX output.
// The reverb never reaches the feedback tape, so RPTS and HOLD retain their gain.
_prism_halo_late:
    F0 = DM(PS_HALO_H,I5);
    R2 = I5;
    R1 = PS_LATE * 4;
    R2 = R2 + R1;
    I4 = R2;
    R1 = 0;
    COMP(F0,F1);
    IF GT JUMP _hl_late_on;
    R0 = DM(PL_VALID,I4);
    R0 = PASS R0;
    IF EQ RTS;
    // Leave the delay memory alone. The valid count silences each stale read
    // until its own delay has been filled again, including a quick re-enable.
    DM(PL_INDEX,I4) = R1;
    DM(PL_VALID,I4) = R1;
    DM(PL_DAMP_L,I4) = R1;
    DM(PL_DAMP_R,I4) = R1;
    DM(PL_GAIN,I4) = R1;
    DM(PL_PRIOR_L,I4) = R1;
    DM(PL_PRIOR_R,I4) = R1;
    RTS;

_hl_late_on:
    R1 = PRISM_LATE_SLOPE;
    F1 = F0 * F1;
    R2 = PRISM_LATE_BASE;
    F1 = F1 + F2;
    F0 = F0 * F1;
    F1 = DM(PS_MIX,I5);
    F0 = F0 * F1;
    F6 = DM(PL_GAIN,I4);
    DM(PL_GAIN,I4) = R0;
    F0 = F0 - F6;
    R1 = 0x3d800000; // one sixteenth of the gain difference per pair
    F7 = F0 * F1;
    R1 = 0x3f000000;
    F0 = F7 * F1;
    DM(PS_TEMP4,I5) = R0;
    F4 = DM(PL_DAMP_L,I4);
    F5 = DM(PL_DAMP_R,I4);
    R11 = DM(PL_INDEX,I4);
    R12 = DM(PL_VALID,I4);
    // The network starts, empty. Silence up to here stays silence behind it: the count of silent frames (rest, prism.s) moves
    // up by what the network adds to the frames asked for, so the engine waits no longer than without it.
    R12 = PASS R12;
    IF NE JUMP _hl_late_run;
    R0 = DM(PS_QUIET,I5);
    R0 = PASS R0;
    IF EQ JUMP _hl_late_run;
    R1 = PRISM_IDLE_LATE - PRISM_IDLE_QUIET;
    R0 = R0 + R1;
    R1 = PRISM_IDLE_LATE;
    R0 = MIN(R0,R1);
    DM(PS_QUIET,I5) = R0;
_hl_late_run:
    R13 = PRISM_LATE_RING - 1;
    R0 = I5;
    R1 = PS_LATE_LINE_L * 4;
    R0 = R0 + R1;
    DM(PS_TEMP2,I5) = R0;
    R1 = PRISM_LATE_RING * 4;
    R0 = R0 + R1;
    DM(PS_TEMP3,I5) = R0;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I0 = R0;
    R1 = 128;
    R0 = R0 + R1;
    I1 = R0;
    I2 = DM(PS_OUTPUT,I5);
    R0 = I2;
    R0 = R0 + R1;
    I3 = R0;
    R8 = PRISM_LATE_DAMP;
    R9 = PRISM_LATE_FEEDBACK;
    R10 = 0x3f000000;
    R14 = 0x40fccccd;
    R15 = 0xc0fccccd;
    LCNTR = 16, DO _hl_late_pair UNTIL LCE;
        R1 = PRISM_LATE_DELAY_L;
        R0 = R11 - R1;
        R0 = R0 AND R13;
        R0 = LSHIFT R0 BY 2;
        R2 = DM(PS_TEMP2,I5);
        R0 = R0 + R2;
        I4 = R0;
        COMP(R12,R1), R0 = DM(I4,M5);
        IF LT R0 = R0 - R0;
        R2 = R4;
        F0 = F0 - F4;
        F0 = F0 * F8;
        F4 = F4 + F0;
        R1 = PRISM_LATE_DELAY_R;
        R0 = R11 - R1;
        R0 = R0 AND R13;
        R0 = LSHIFT R0 BY 2;
        R3 = DM(PS_TEMP3,I5);
        R0 = R0 + R3;
        I4 = R0;
        COMP(R12,R1), R0 = DM(I4,M5);
        IF LT R0 = R0 - R0;
        R3 = R5;
        F0 = F0 - F5;
        F0 = F0 * F8;
        F5 = F5 + F0;

        // The output's half-step uses quadratic interpolation through the
        // last three samples. The gain ramps at both output samples.
        F6 = F6 + F7;
        F0 = F2 + F2;
        F0 = F0 - F4;
        F1 = DM(PS_LATE + PL_PRIOR_L,I5);
        F0 = F0 - F1;
        DM(PS_LATE + PL_PRIOR_L,I5) = R2;
        R1 = 0x3e000000; // 1/8
        F0 = F0 * F1;
        F2 = F2 + F4;
        F2 = F2 * F10;
        F2 = F2 + F0;
        F0 = DM(PS_TEMP4,I5);
        F0 = F6 - F0;
        DM(PS_TEMP5,I5) = R0;
        F2 = F2 * F0;
        F0 = DM(I2,M5);
        F0 = F0 + F2;
        F0 = MIN(F0,F14);
        F0 = MAX(F0,F15);
        DM(I2,M6) = R0;
        F2 = F4 * F6, F0 = DM(I2,M5);
        F0 = F0 + F2;
        F0 = MIN(F0,F14);
        F0 = MAX(F0,F15);
        DM(I2,M6) = R0;
        F0 = F3 + F3;
        F0 = F0 - F5;
        F1 = DM(PS_LATE + PL_PRIOR_R,I5);
        F0 = F0 - F1;
        DM(PS_LATE + PL_PRIOR_R,I5) = R3;
        R1 = 0x3e000000;
        F0 = F0 * F1;
        F3 = F3 + F5;
        F3 = F3 * F10;
        F3 = F3 + F0;
        F0 = DM(PS_TEMP5,I5);
        F3 = F3 * F0;
        F0 = DM(I3,M5);
        F0 = F0 + F3;
        F0 = MIN(F0,F14);
        F0 = MAX(F0,F15);
        DM(I3,M6) = R0;
        F3 = F5 * F6, F0 = DM(I3,M5);
        F0 = F0 + F3;
        F0 = MIN(F0,F14);
        F0 = MAX(F0,F15);
        DM(I3,M6) = R0;

        // Box-filter the stereo input to half rate. The two linear combinations
        // of the damped output constitute a normalized Hadamard matrix times g.
        F0 = DM(I0,M6);
        F1 = DM(I0,M6);
        F2 = F0 + F1;
        F2 = F2 * F10, F0 = DM(I1,M6);
        F1 = DM(I1,M6);
        F3 = F0 + F1;
        F3 = F3 * F10;
        // Start new excitation at zero with zero slope. The first reading of
        // either refilled delay must not expose a full-amplitude sample.
        // After PRISM_LATE_START pairs the weight is exactly 1.
        R0 = PRISM_LATE_START;
        COMP(R12,R0);
        IF LT CALL _hl_late_rise;
        F0 = F4 + F5;
        F1 = F4 - F5;
        F0 = F0 * F9;
        F1 = F1 * F9;
        F2 = F2 + F0;
        F3 = F3 + F1;
        R0 = LSHIFT R11 BY 2;
        R1 = DM(PS_TEMP2,I5);
        R1 = R1 + R0;
        I4 = R1;
        DM(0,I4) = R2;
        R1 = DM(PS_TEMP3,I5);
        R1 = R1 + R0;
        I4 = R1;
        DM(0,I4) = R3;
        R11 = R11 + 1;
        R11 = R11 AND R13;
_hl_late_pair: R12 = R12 + 1;

    R2 = I5;
    R1 = PS_LATE * 4;
    R2 = R2 + R1;
    I4 = R2;
    COMP(R12,R13);
    IF GT R12 = PASS R13;
    DM(PL_INDEX,I4) = R11;
    DM(PL_VALID,I4) = R12;
    DM(PL_DAMP_L,I4) = R4;
    DM(PL_DAMP_R,I4) = R5;
    RTS;
// The excitation F2 and F3 of pair R12 of a network that starts (R12 below PRISM_LATE_START), times
// w (2 - w), w = (R12 / PRISM_LATE_START)^2. Uses F0, F1.
_hl_late_rise:
    F0 = FLOAT R12;
    R1 = 0x3b800000; // 1 / PRISM_LATE_START
    F0 = F0 * F1;
    F0 = F0 * F0;
    R1 = 0x40000000;
    F1 = F1 - F0;
    F0 = F0 * F1;
    F2 = F2 * F0;
    F3 = F3 * F0;
    RTS;
