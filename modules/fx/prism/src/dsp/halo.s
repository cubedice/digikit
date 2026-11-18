// PRISM HALO: the four delay lines of 71, 113, 151 and 211 samples as all-pass filters inside the repeat path, on PS_Y in
// place. A line is a lattice all-pass
//
//   y = c x + s d,   line input = s x - c d,   d the line's output,   c = cos(theta), s = sin(theta)
//
// which is (c + z^-N) / (1 + c z^-N): the direct sound with gain c, then a tail. The sum and the difference of the two
// sides pass lines 0 and 1, then the sides themselves pass lines 2 and 3. Every step keeps the energy at every frequency,
// so HALO smears a repeat a little more each time it passes and never changes the loop's gain. HALO sets theta: 0 at 0
// (c = 1, s = 0: the signal itself) up to 60 degrees (c = 0.5). At HALO 0 the routine does not touch the signal.
#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_halo;
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
    // t = tan(theta / 2) = HALO * PRISM_HALO_MAX; c = (1 - t^2) / (1 + t^2), s = 2 t / (1 + t^2). HALO 0 is c = 1 and
    // s = 0 exactly.
    R3 = 0x3f800000;
    R4 = 0;
    COMP(F0,F4);
    IF LE JUMP _hl_move;
    R1 = PRISM_HALO_MAX;
    F0 = F0 * F1;
    F2 = F0 * F0;
    R1 = 0x3f800000;
    F3 = F1 - F2;
    F4 = F0 + F0;
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
    DM(PS_TEMP,I5) = R0;
    R1 = 1024;
    R0 = R0 + R1;
    DM(PS_TEMP1,I5) = R0;
    R0 = R0 + R1;
    DM(PS_TEMP2,I5) = R0;
    R0 = R0 + R1;
    DM(PS_TEMP3,I5) = R0;
    R12 = DM(PS_HALO_INDEX,I5);
    R14 = 32;
    R15 = 0x3f3504f3; // sqrt(1/2)
_hl_sample:
    F8 = F8 + F10;
    F9 = F9 + F11;
    F6 = DM(I0,M5);
    F7 = DM(I1,M5);
    F0 = F6 + F7;
    F0 = F0 * F15;
    F7 = F6 - F7;
    F7 = F7 * F15;
    R4 = 71;
    R5 = DM(PS_TEMP,I5);
    CALL _hl_line;
    R6 = R0;
    R0 = R7;
    R4 = 113;
    R5 = DM(PS_TEMP1,I5);
    CALL _hl_line;
    F7 = F6 - F0;
    F7 = F7 * F15;
    F0 = F6 + F0;
    F0 = F0 * F15;
    R4 = 151;
    R5 = DM(PS_TEMP2,I5);
    CALL _hl_line;
    DM(I0,M6) = R0;
    R0 = R7;
    R4 = 211;
    R5 = DM(PS_TEMP3,I5);
    CALL _hl_line;
    DM(I1,M6) = R0;
    R12 = R12 + 1;
    R0 = 255;
    R12 = R12 AND R0;
    R14 = R14 - 1;
    IF GT JUMP _hl_sample;
    DM(PS_HALO_INDEX,I5) = R12;
    RTS;

// One line. F0 the input and the output; R4 the line's length, R5 its address, R12 the write index, F8 and F9 c and s.
// Uses R1 to R3, I2.
_hl_line:
    R1 = R12 - R4;
    R2 = 255;
    R1 = R1 AND R2;
    R1 = LSHIFT R1 BY 2;
    R1 = R1 + R5;
    I2 = R1;
    F1 = DM(0,I2);
    F2 = F0 * F9;
    F3 = F1 * F8;
    F2 = F2 - F3;
    R3 = LSHIFT R12 BY 2;
    R3 = R3 + R5;
    I2 = R3;
    DM(0,I2) = R2;
    F0 = F0 * F8;
    F1 = F1 * F9;
    F0 = F0 + F1;
    RTS;
