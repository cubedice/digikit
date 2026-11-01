// Relocation-free callback copied by the emulator into the test DKDS6 image.
// It writes the lifecycle reason to instance state, then returns success.
.SECTION/PM seg_pmco;
.GLOBAL _ilp_reset;
_ilp_reset:
    I0 = R4;
    DM(0, I0) = R8;
    R0 = 0;
    JUMP (M13, I13);
