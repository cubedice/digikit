// Internal single-instance V2 reset worker, not yet reachable from the mailbox.
// Caller owns/admitted the two ranges and has drained transfers into them:
// R4 state byte address, R5 state words (1..1024), R6/R7 state arena [start,end),
// R8 reason (CREATE=1/REPLACE=2/RECOVER=3/UNLOAD=4), R12 reset short-word entry,
// R9/R10 executable short-word interval [start,end). Ranges must be positive,
// non-wrapping signed-31-bit addresses. Bounds do not prove module ownership.
// Caller pins non-SIMD, linear L0, M5=0/M6=1/M13=0 and protects all callback
// clobbers: R0..15, I0..5/I12/I13, M1..4/B3 and arithmetic state. No reentrancy.
// Returns R0=0 only after callback success; -1 for invalid input/positive result;
// negative callback faults propagate. A callback must satisfy admitted budgets;
// this worker bounds clearing, but cannot preempt a callback that fails to return.
.SECTION/PM seg_pmco;
.GLOBAL _ir_reset_instance;
_ir_reset_instance:
    R0 = PASS R4;
    IF LE JUMP _irl_invalid;
    R0 = 3;
    R0 = R4 AND R0;
    IF NE JUMP _irl_invalid;
    R0 = PASS R6;
    IF LE JUMP _irl_invalid;
    COMP(R7, R6);
    IF LE JUMP _irl_invalid;
    COMP(R4, R6);
    IF LT JUMP _irl_invalid;
    R0 = PASS R5;
    IF LE JUMP _irl_invalid;
    R0 = 1024;
    COMP(R5, R0);
    IF GT JUMP _irl_invalid;
    R1 = LSHIFT R5 BY 2;
    R2 = R4 + R1;
    // R4 is positive and at most 4096 bytes are added: signed wrap is negative.
    R0 = PASS R2;
    IF LE JUMP _irl_invalid;
    COMP(R2, R7);
    IF GT JUMP _irl_invalid;
    R0 = PASS R9;
    IF LE JUMP _irl_invalid;
    COMP(R10, R9);
    IF LE JUMP _irl_invalid;
    COMP(R12, R9);
    IF LT JUMP _irl_invalid;
    COMP(R12, R10);
    IF GE JUMP _irl_invalid;
    R0 = PASS R8;
    IF LE JUMP _irl_invalid;
    R0 = 4;
    COMP(R8, R0);
    IF GT JUMP _irl_invalid;
    R0 = 1;
    COMP(R8, R0);
    IF NE JUMP _irl_call;
    I0 = R4;
    R2 = R5;
    R0 = 0;
_irl_clear:
    DM(I0, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _irl_clear;
_irl_call:
    I12 = R12;
    I13 = _irl_return;
    JUMP (M13, I12);
_irl_return:
    R0 = PASS R0;
    IF LE RTS;
_irl_invalid:
    R0 = -1;
    RTS;
