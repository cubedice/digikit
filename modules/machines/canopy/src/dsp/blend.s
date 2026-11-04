// Linear PAD/ARP crossfade. R4=pad/output[32], R8=arp[32], R12=state[2].
// State is {current,target} floats in [0,1]. Caller validates audio before/after
// this operation. R0=1 success, 0 invalid state (no writes). This helper owns no
// voice state: muted groups must still receive gates and advance envelopes.
.SECTION/PM seg_pmco;
.GLOBAL _cp_blend;
_cp_blend:
    I2 = R12;
    R0 = DM(0, I2);
    R1 = 0x3f800000;
    COMP(R0, R1);
    IF GT JUMP _cp_blend_bad;
    R0 = PASS R0;
    IF LT JUMP _cp_blend_bad;
    R0 = DM(1, I2);
    COMP(R0, R1);
    IF GT JUMP _cp_blend_bad;
    R0 = PASS R0;
    IF LT JUMP _cp_blend_bad;
    F2 = DM(0, I2);
    F3 = DM(1, I2);
    I0 = R4;
    I1 = R8;
    R10 = 32;
    F4 = 0.00104112432; // 1-exp(-1/(48000*.020))
    F5 = 1.0;
    F6 = 0.000030517578125; // snap below -90 dB; float rounding must not leak a lane
_cp_blend_sample:
    F0 = F3 - F2;
    F7 = ABS F0;
    COMP(F7, F6);
    IF LE JUMP _cp_blend_exact;
    F0 = F0 * F4;
    F2 = F2 + F0;
    JUMP _cp_blend_output;
_cp_blend_exact:
    F2 = F3;
_cp_blend_output:
    F0 = F5 - F2;
    F1 = DM(I0, M5);
    F0 = F0 * F1;
    F1 = DM(I1, M6);
    F1 = F1 * F2;
    F0 = F0 + F1;
    DM(I0, M6) = F0;
    R10 = R10 - 1;
    IF NE JUMP _cp_blend_sample;
    DM(0, I2) = F2;
    R0 = 1;
    RTS;
_cp_blend_bad:
    R0 = 0;
    RTS;
