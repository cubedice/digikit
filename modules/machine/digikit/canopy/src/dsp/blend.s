// Linear PAD/ARP crossfade. R4=pad/output[32], R8=arp[32], R12=state[2].
// State is {current,target} floats in [0,1]. R9 is zero only when both lanes
// rendered no samples and pad/output is clear. Caller validates audio before/after
// this operation. R0=1 success, 0 invalid state (no writes). This helper owns no
// voice state: muted groups must still receive gates and advance envelopes.
.SECTION/PM seg_pmco;
.GLOBAL _cp_blend;
.EXTERN _cp_post_output;
.EXTERN _cp_post_pad;
.EXTERN _cp_post_arp;
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
    R9 = PASS R9;
    IF EQ JUMP _cp_blend_quiet;
    I0 = R4;
    I1 = R8;
    R10 = 32;
    F4 = 0.00104112432; // 1-exp(-1/(48000*.020))
    F5 = 1.0;
    F6 = 0.000030517578125; // snap below -90 dB; float rounding must not leak a lane
    R0 = DM(_cp_post_output);
    R0 = PASS R0;
    IF NE JUMP _cp_post_blend_setup;
    COMP(R2, R3);
    IF EQ JUMP _cp_blend_settled;
_cp_blend_sample:
    F0 = F3 - F2;
    F7 = ABS F0;
    COMP(F7, F6);
    IF LE JUMP _cp_blend_exact;
    F0 = F0 * F4;
    F2 = F2 + F0;
    JUMP _cp_blend_output;
_cp_blend_exact:
    F2 = PASS F3;
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
_cp_blend_quiet:
    // Run the same gain recurrence even when there is nothing to mix, so the
    // next sounding block uses the same crossfade. The pad/output is clear.
    COMP(R2, R3);
    IF EQ JUMP _cp_blend_quiet_post;
    F4 = 0.00104112432;
    F6 = 0.000030517578125;
    LCNTR = 32, DO _cp_blend_quiet_ramp UNTIL LCE;
    F0 = F3 - F2;
    F7 = ABS F0;
    F0 = F0 * F4;
    F2 = F2 + F0;
    COMP(F7, F6);
_cp_blend_quiet_ramp:
    IF LE F2 = PASS F3;
    DM(0,I2) = F2;
_cp_blend_quiet_post:
    R0 = DM(_cp_post_output);
    R0 = PASS R0;
    IF EQ JUMP _cp_blend_quiet_done;
    I1 = R0;
    R0 = 0;
    LCNTR = 32, DO _cp_blend_quiet_last UNTIL LCE;
_cp_blend_quiet_last:
    DM(I1,M6) = R0;
_cp_blend_quiet_done:
    R0 = 1;
    RTS;
// The mix at its target: every sample of the loop above snaps to it and weighs with the same two factors, so they are
// taken once (the same operations on the same operands).
_cp_blend_settled:
    F0 = F5 - F2;
    LCNTR = 32, DO _cp_blend_settled_last UNTIL LCE;
    F1 = DM(I0, M5);
    F7 = F0 * F1;
    F1 = DM(I1, M6);
    F1 = F1 * F2;
    F7 = F7 + F1;
_cp_blend_settled_last:
    DM(I0, M6) = F7;
    DM(0, I2) = F2;
    R0 = 1;
    RTS;

// Keep the legacy loop unchanged. V7 uses the same floating-point operations
// and snap decisions for PCM and for its lane-weighted common AMP envelope.
_cp_post_blend_setup:
    R1 = I3;
    DM(_cp_post_save_i3) = R1;
    R1 = I4;
    DM(_cp_post_save_i4) = R1;
    R1 = I5;
    DM(_cp_post_save_i5) = R1;
    R1 = L3;
    DM(_cp_post_save_l3) = R1;
    R1 = L4;
    DM(_cp_post_save_l4) = R1;
    R1 = L5;
    DM(_cp_post_save_l5) = R1;
    R1 = 0;
    L3 = R1;
    L4 = R1;
    L5 = R1;
    I3 = _cp_post_pad;
    I4 = _cp_post_arp;
    I5 = R0;
    COMP(R2, R3);
    IF EQ JUMP _cp_post_blend_settled;
_cp_post_blend_sample:
    F0 = F3 - F2;
    F7 = ABS F0;
    COMP(F7,F6);
    IF LE JUMP _cp_post_blend_exact;
    F0 = F0 * F4;
    F2 = F2 + F0;
    JUMP _cp_post_blend_output;
_cp_post_blend_exact:
    F2 = PASS F3;
_cp_post_blend_output:
    F0 = F5 - F2;
    F1 = DM(I0,M5);
    F0 = F0 * F1;
    F1 = DM(I1,M6);
    F1 = F1 * F2;
    F0 = F0 + F1;
    DM(I0,M6) = F0;
    F7 = F5 - F2;
    F0 = DM(I3,M6);
    F0 = F0 * F7, F1 = DM(I4,M6);
    F1 = F1 * F2;
    F0 = F0 + F1;
    DM(I5,M6) = F0;
    R10 = R10 - 1;
    IF NE JUMP _cp_post_blend_sample;
    JUMP _cp_post_blend_restore;
// The mix at its target, as _cp_blend_settled: PCM and the common envelope with the same two factors.
_cp_post_blend_settled:
    F7 = F5 - F2;
    LCNTR = 32, DO _cp_post_blend_settled_last UNTIL LCE;
    F1 = DM(I0,M5);
    F0 = F7 * F1;
    F1 = DM(I1,M6);
    F1 = F1 * F2;
    F0 = F0 + F1;
    DM(I0,M6) = F0;
    F0 = DM(I3,M6);
    F0 = F0 * F7, F1 = DM(I4,M6);
    F1 = F1 * F2;
    F0 = F0 + F1;
_cp_post_blend_settled_last:
    DM(I5,M6) = F0;
_cp_post_blend_restore:
    DM(0,I2) = F2;
    R0 = DM(_cp_post_save_i3);
    I3 = R0;
    R0 = DM(_cp_post_save_i4);
    I4 = R0;
    R0 = DM(_cp_post_save_i5);
    I5 = R0;
    R0 = DM(_cp_post_save_l3);
    L3 = R0;
    R0 = DM(_cp_post_save_l4);
    L4 = R0;
    R0 = DM(_cp_post_save_l5);
    L5 = R0;
    R0 = 1;
    RTS;
.SECTION/DM seg_dmda;
.VAR _cp_post_save_i3 = 0;
.VAR _cp_post_save_i4 = 0;
.VAR _cp_post_save_i5 = 0;
.VAR _cp_post_save_l3 = 0;
.VAR _cp_post_save_l4 = 0;
.VAR _cp_post_save_l5 = 0;
