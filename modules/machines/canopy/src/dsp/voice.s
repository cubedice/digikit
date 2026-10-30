// CANOPY stage-1 sine kernel. Internal ABI, not a loadable V1 machine.
// Bank: four 32-byte slots {token, gate, x, y, sin(w), cos(w), gain, envelope}.
// Caller supplies a zeroed, isolated 128-byte bank. No firmware pointers.
// R4=bank for all calls; note_on: R8=nonzero token,R12=MIDI note,R9=velocity.
// note_off: R8=token. render: R8=32-float output. Returns R0=1 accepted/active,
// 0 rejected/quiet. Scratch R0..15,I0..2; L0..2=0,M5=0,M6=1, non-SIMD floats.
// Gate 2 is the reset ramp (at most 64 samples). No stealing yet: a full bank
// rejects without changing any voice. Pedal/bend follow the lifetime proof.
.SECTION/PM seg_pmco;
.GLOBAL _cp_note_on;
.GLOBAL _cp_note_off;
.GLOBAL _cp_render;
.EXTERN _cp_tuning;
_cp_note_on:
    R8 = PASS R8;
    IF EQ JUMP _cp_reject;
    R0 = LSHIFT R12 BY -7;
    R0 = PASS R0;
    IF NE JUMP _cp_reject;
    R0 = LSHIFT R9 BY -7;
    R0 = PASS R0;
    IF NE JUMP _cp_reject;
    R9 = PASS R9;
    IF EQ JUMP _cp_reject;
    I0 = R4;
    R2 = 4;
    R3 = 0;
_cp_find_slot:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cp_reject; // duplicate token cannot restart its oscillator
    R0 = PASS R0;
    IF NE JUMP _cp_next_slot;
    R3 = I0;
_cp_next_slot:
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_find_slot;
    R3 = PASS R3;
    IF EQ JUMP _cp_reject;
    I0 = R3;
    DM(0, I0) = R8;
    R0 = 1;
    DM(1, I0) = R0;
    R0 = 0;
    DM(2, I0) = R0;
    DM(7, I0) = R0;
    F0 = 1.0;
    DM(3, I0) = R0;
    R0 = LSHIFT R12 BY 3;
    R1 = _cp_tuning;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(0, I1);
    DM(4, I0) = R0;
    R0 = DM(1, I1);
    DM(5, I0) = R0;
    F0 = FLOAT R9;
    F1 = 0.007874015748;
    F0 = F0 * F1;
    DM(6, I0) = R0;
    R0 = 1;
    RTS;
_cp_note_off:
    R8 = PASS R8;
    IF EQ JUMP _cp_reject;
    I0 = R4;
    R2 = 4;
_cp_find_release:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cp_release;
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_find_release;
    JUMP _cp_reject;
_cp_release:
    R0 = DM(1, I0);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _cp_release_done; // do not lengthen a reset ramp
    R0 = 0;
    DM(1, I0) = R0;
_cp_release_done:
    R0 = 1;
    RTS;
_cp_reject:
    R0 = 0;
    RTS;
_cp_render:
    I2 = R8;
    R0 = 0;
    R1 = 32;
_cp_clear_output:
    DM(I2, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _cp_clear_output;
    I0 = R4;
    R14 = 4;
    R15 = 0;
_cp_render_slot:
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF EQ JUMP _cp_render_next;
    F2 = DM(2, I0); // oscillator x,y
    F3 = DM(3, I0);
    F4 = DM(4, I0); // rotation sin,cos
    F5 = DM(5, I0);
    F6 = DM(6, I0);
    F7 = DM(7, I0);
    R0 = DM(1, I0);
    F10 = 0.0002604166667; // 80 ms linear attack at 48 kHz
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _cp_reset_ramp;
    R0 = PASS R0;
    IF NE JUMP _cp_render_samples;
    F10 = -0.00002083333333; // one second release from full amplitude
    JUMP _cp_render_samples;
_cp_reset_ramp:
    F10 = -0.015625; // bounded de-click ramp on lifecycle reset
_cp_render_samples:
    I2 = R8;
    R13 = 32;
_cp_sample:
    F7 = F7 + F10;
    F0 = 0.0;
    F7 = MAX(F7, F0);
    F0 = 1.0;
    F7 = MIN(F7, F0);
    F0 = F2 * F5;
    F1 = F3 * F4;
    F9 = F0 + F1;
    F0 = F3 * F5;
    F1 = F2 * F4;
    F3 = F0 - F1;
    F2 = F9;
    F0 = F2 * F6;
    F0 = F0 * F7;
    F1 = 0.125;
    F0 = F0 * F1;
    F1 = DM(I2, M5);
    F0 = F0 + F1;
    DM(I2, M6) = F0;
    R13 = R13 - 1;
    IF NE JUMP _cp_sample;
    // One Newton normalization step per block bounds float32 rotation drift.
    F0 = F2 * F2;
    F1 = F3 * F3;
    F0 = F0 + F1;
    F1 = 0.5;
    F0 = F0 * F1;
    F1 = 1.5;
    F0 = F1 - F0;
    F2 = F2 * F0;
    F3 = F3 * F0;
    DM(2, I0) = F2;
    DM(3, I0) = F3;
    DM(7, I0) = F7;
    F0 = 0.0;
    COMP(F7, F0);
    IF GT JUMP _cp_active;
    R0 = 0;
    DM(0, I0) = R0;
    JUMP _cp_render_next;
_cp_active:
    R15 = 1;
_cp_render_next:
    MODIFY(I0, 8) (NW);
    R14 = R14 - 1;
    IF NE JUMP _cp_render_slot;
    R0 = R15;
    RTS;
