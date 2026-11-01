// Relocation-free callback copied by the emulator into the test DKDS6 image.
// It writes the lifecycle reason to instance state, then returns success.
.SECTION/PM seg_pmco;
.GLOBAL _ilp_reset;
_ilp_reset:
    I0 = R4;
    DM(0, I0) = R8;
    R0 = 0;
    JUMP (M13, I13);

// Independent process entry: observable audio output and per-instance call count.
.GLOBAL _ilp_process;
_ilp_process:
    I0 = R4;
    R0 = DM(1, I0);
    R0 = R0 + 1;
    DM(1, I0) = R0;
    I1 = R8;
    R0 = DM(5, I1);
    I1 = R0;
    R0 = 0x3e800000;
    DM(0, I1) = R0;
    R0 = 0;
    JUMP (M13, I13);
