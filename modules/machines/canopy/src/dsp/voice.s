// CANOPY stage-1 sine kernel. Internal ABI, not a loadable V1 machine.
// Bank: four foreground + two tail slots, each 32 bytes:
// {token, gate, x, y, sin(w), cos(w), gain, envelope}; then four 16-byte
// pending replacements {token,note,velocity,reserved}. 256 bytes per track.
// R4=bank for all calls; note_on: R8=nonzero token,R12=MIDI note,R9=velocity.
// note_off: R8=token. render: R8=32-float output. Returns R0=1 accepted/active,
// 0 rejected/quiet. Scratch R0..15,I0..2; L0..2=0,M5=0,M6=1, non-SIMD floats.
// Gate 2 is the reset/steal ramp (at most 64 samples). Reclaim the quietest
// release first, otherwise the oldest token (CF tokens increase per epoch).
// A silent new onset needs no
// tail. Both tails busy => queue a replacement behind its own 64-sample ramp.
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
    R2 = 6;
_cp_unique_token:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cp_reject;
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_unique_token;
    R2 = 4;
_cp_unique_pending:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cp_reject;
    MODIFY(I0, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_unique_pending;
    I0 = R4;
    R0 = 192;
    R0 = R4 + R0;
    I1 = R0;
    R2 = 4;
    R3 = 0;
    R6 = 0;
    R5 = 0;
_cp_find_slot:
    R0 = DM(0, I0);
    R1 = DM(0, I1);
    R1 = PASS R1;
    IF NE R0 = R1; // pending replacement is the current foreground owner
    R0 = PASS R0;
    IF NE JUMP _cp_oldest;
    R3 = I0;
    JUMP _cp_next_slot;
_cp_oldest:
    R1 = 0x80000000;
    R10 = R0 XOR R1;
    R15 = 0;
    R1 = DM(0, I1);
    R1 = PASS R1;
    IF NE JUMP _cp_compare_candidate; // queued onset remains a held owner
    R0 = DM(1, I0);
    R1 = 1;
    COMP(R0, R1);
    IF NE R15 = R1;
_cp_compare_candidate:
    R14 = DM(7, I0); // nonnegative float bits sort by envelope magnitude
    R6 = PASS R6;
    IF EQ JUMP _cp_choose_oldest;
    COMP(R15, R5);
    IF GT JUMP _cp_choose_oldest; // releasing before held
    IF LT JUMP _cp_next_slot;
    R15 = PASS R15;
    IF EQ JUMP _cp_compare_age;
    COMP(R14, R13);
    IF LT JUMP _cp_choose_oldest;
    IF GT JUMP _cp_next_slot;
_cp_compare_age:
    COMP(R10, R7);
    IF GE JUMP _cp_next_slot;
_cp_choose_oldest:
    R6 = I0;
    R7 = R10;
    R5 = R15;
    R13 = R14;
_cp_next_slot:
    MODIFY(I0, 8) (NW);
    MODIFY(I1, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_find_slot;
    R3 = PASS R3;
    IF NE JUMP _cp_start;
    R3 = R6;
    R0 = R3 - R4;
    R0 = LSHIFT R0 BY -1;
    R1 = 192;
    R0 = R0 + R1;
    R0 = R4 + R0;
    I2 = R0;
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF NE JUMP _cp_queue_replacement;
    I0 = R3;
    R0 = DM(7, I0);
    R0 = PASS R0;
    IF EQ JUMP _cp_start; // an unrendered onset is silent; no tail required
    R0 = 128;
    R0 = R4 + R0;
    I2 = R0;
    R2 = 2;
_cp_find_tail:
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF EQ JUMP _cp_save_tail;
    MODIFY(I2, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_find_tail;
    R0 = R3 - R4;
    R0 = LSHIFT R0 BY -1;
    R1 = 192;
    R0 = R0 + R1;
    R0 = R4 + R0;
    I2 = R0;
_cp_queue_replacement:
    DM(0, I2) = R8;
    DM(1, I2) = R12;
    DM(2, I2) = R9;
    I0 = R3;
    R0 = 2;
    DM(1, I0) = R0;
    R0 = 1;
    RTS;
_cp_save_tail:
    R11 = I2;
    I1 = R3;
    R2 = 8;
_cp_copy_tail:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _cp_copy_tail;
    I2 = R11;
    R0 = 2;
    DM(1, I2) = R0;
_cp_start:
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
    R0 = 192;
    R0 = R4 + R0;
    I0 = R0;
    R2 = 4;
_cp_release_pending:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cp_cancel_pending;
    MODIFY(I0, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_release_pending;
    I0 = R4;
    R2 = 6;
_cp_find_release:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cp_release;
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cp_find_release;
    JUMP _cp_reject;
_cp_cancel_pending:
    R0 = 0;
    DM(0, I0) = R0;
    R0 = 1;
    RTS;
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
    R11 = R4;
    R14 = 6;
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
    R0 = 2;
    COMP(R14, R0);
    IF LE JUMP _cp_advance_slot;
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF NE JUMP _cp_advance_slot;
    R3 = I0;
    R0 = R3 - R11;
    R0 = LSHIFT R0 BY -1;
    R1 = 192;
    R0 = R0 + R1;
    R0 = R11 + R0;
    I1 = R0;
    R0 = DM(0, I1);
    R0 = PASS R0;
    IF EQ JUMP _cp_advance_slot;
    R13 = R8;
    R8 = R0;
    R12 = DM(1, I1);
    R9 = DM(2, I1);
    R0 = 0;
    DM(0, I1) = R0;
    CALL _cp_start;
    R8 = R13;
    R15 = 1;
_cp_advance_slot:
    MODIFY(I0, 8) (NW);
    R14 = R14 - 1;
    IF NE JUMP _cp_render_slot;
    R0 = R15;
    RTS;
