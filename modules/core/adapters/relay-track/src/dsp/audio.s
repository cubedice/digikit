// Planar float audio at the track input; signed PCM16 stereo in the pool.
// All addresses outside DM displacement operands are byte addresses. M6 is +1.
.SECTION/PM seg_pmco;
.GLOBAL _relay_pack_span;
// R4 left input, R8 right input, R12 destination, R9 frames, R10 previous PCM
// or zero. Overdub reads the old word before writing, including in-place passes.
_relay_pack_span:
    I0 = R4;
    I1 = R8;
    I2 = R12;
    I3 = R10;
    R4 = 0x47000000;      // 32768
    R5 = 0x46fffe00;      // 32767
    R6 = 0xc7000000;      // -32768
    R7 = 65535;
    R10 = PASS R10;
    IF NE JUMP _ra_add;
    LCNTR = R9, DO _ra_first UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I1,M6);
        F0 = F0 * F4;
        F1 = F1 * F4;
        F0 = MAX(F0,F6);
        F1 = MAX(F1,F6);
        F0 = MIN(F0,F5);
        F1 = MIN(F1,F5);
        R0 = FIX F0;
        R1 = FIX F1;
        R0 = LSHIFT R0 BY 16;
        R1 = R1 AND R7;
        R0 = R0 OR R1;
_ra_first: DM(I2,M6) = R0;
    RTS;
_ra_add:
    LCNTR = R9, DO _ra_dub UNTIL LCE;
        R2 = DM(I3,M6);
        R3 = ASHIFT R2 BY -16;
        R2 = LSHIFT R2 BY 16;
        R2 = ASHIFT R2 BY -16;
        F2 = FLOAT R2;
        F3 = FLOAT R3;
        F0 = DM(I0,M6);
        F1 = DM(I1,M6);
        F0 = F0 * F4;
        F1 = F1 * F4;
        F0 = F0 + F3;
        F1 = F1 + F2;
        F0 = MAX(F0,F6);
        F1 = MAX(F1,F6);
        F0 = MIN(F0,F5);
        F1 = MIN(F1,F5);
        R0 = FIX F0;
        R1 = FIX F1;
        R0 = LSHIFT R0 BY 16;
        R1 = R1 AND R7;
        R0 = R0 OR R1;
_ra_dub: DM(I2,M6) = R0;
    RTS;
