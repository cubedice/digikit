// CANOPY factory wavetable renderer. All persistent history belongs to the
// instance. The existing rotating oscillator remains the phase authority, so
// note stealing, bending and same-pitch pad/arp alignment retain their ABI.
// Added state: words200..231 eight {token,env,route,block-start-env} records;
// 232..263 shared scan/transition history;264..295 USB/DIN wheel routes.
// CPSB PM reuses words41..46 in each of the eight FM histories296..679.
.SECTION/PM seg_pmco;
.GLOBAL _wt_prepare;
.GLOBAL _wt_begin_slot;
.GLOBAL _wt_render_block;
.GLOBAL _wt_pm_block;
.GLOBAL _wt_validate;
.EXTERN _wt_factory;
.EXTERN _cp_envelopes;
.EXTERN _fm_bank;
.EXTERN _fm_ratios;
.EXTERN _fm_sines;
.GLOBAL _wt_atan;

// R4 original 32-word CPS9 bank. Return R0=1 and I0 a low-word validation
// copy, or R0=0. No instance or user input is written during validation.
_wt_validate:
    I0 = R4;
    I1 = _wt_low_controls;
    R2 = 16;
_wt_copy_header:
    R0 = DM(I0, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_copy_header;
    I2 = R4;
    R5 = DM(0,I2);
    R5 = FEXT R5 BY 0:8;
    I2 = _wt_limits;
    R2 = 16;
_wt_validate_control:
    R0 = DM(I0, M6);
    R1 = LSHIFT R0 BY -16;
    R3 = DM(I2, M6);
    R6 = 4;
    COMP(R2,R6);
    IF GT JUMP _wt_validate_limit;
    R6 = 11;
    COMP(R5,R6);
    R6 = 0;
    IF NE R3 = R6;
_wt_validate_limit:
    COMP(R1, R3);
    IF GT JUMP _wt_invalid;
    R0 = FEXT R0 BY 0:16;
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_validate_control;
    I0 = R4;
    R0 = DM(19, I0);
    R0 = LSHIFT R0 BY -16;
    R0 = PASS R0;
    IF EQ JUMP _wt_invalid;
    R1 = DM(22, I0);
    R1 = LSHIFT R1 BY -16;
    R1 = PASS R1;
    IF EQ JUMP _wt_valid;
    R1 = 7;
    COMP(R0, R1);
    IF GT JUMP _wt_invalid;
_wt_valid:
    I0 = _wt_low_controls;
    R0 = 1;
    RTS;
_wt_invalid:
    R0 = 0;
    RTS;

// R4=instance, R8=normalized process input, called once after event commit.
_wt_prepare:
    DM(_wt_state) = R4;
    DM(_wt_input) = R8;
    R0 = 0;
    DM(_wt_enabled) = R0;
    DM(_wt_pm_active) = R0;
    I0 = R4;
    I1 = R8;
    R0 = DM(10, I1);
    R1 = 32;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(9, I1);
    R1 = DM(_fm_bank);
    R1 = PASS R1;
    IF NE R0 = R1;
    I2 = R0;
    R0 = DM(0, I2);
    R0 = FEXT R0 BY 0:8;
    R1 = 11;
    COMP(R0,R1);
    IF NE JUMP _wt_old_version;
    R1 = 1;
    DM(_wt_pm_active) = R1;
    JUMP _wt_version_ready;
_wt_old_version:
    R1 = 10;
    COMP(R0, R1);
    IF EQ JUMP _wt_version_ready;
    R1 = 9;
    COMP(R0, R1);
    IF NE RTS;
_wt_version_ready:
    R0 = DM(1,I2);
    R1 = 3;
    COMP(R0,R1);
    IF EQ RTS;
    R0 = DM(1, I2);
    R1 = 2;
    COMP(R0, R1);
    R0 = 0;
    R1 = 1;
    IF EQ R0 = R1;
    DM(_wt_enabled) = R0;
    F0 = FLOAT R0;
    F1 = DM(232, I0);
    DM(_wt_source_start) = F1;
    F0 = F0 - F1;
    F2 = 0.0327838995;
    F0 = F0 * F2;
    F1 = F1 + F0;
    DM(232, I0) = F1;
    DM(_wt_source_mix) = F1;
    F2 = 0.03125;
    F0 = F0 * F2;
    DM(_wt_source_step) = F0;
    R0 = DM(_wt_enabled);
    // Finish tiny source-transition residue.
    R1 = DM(232, I0);
    R2 = 0x35800000;
    COMP(R1, R2);
    IF GT R0 = R1;
    DM(_wt_enabled) = R0;
    R0 = DM(_wt_enabled);
    R0 = PASS R0;
    IF EQ RTS;
    MODIFY(I2, 16) (NW);
    I1 = _wt_controls;
    R2 = 16;
_wt_unpack:
    R0 = DM(I2, M6);
    R0 = LSHIFT R0 BY -16;
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_unpack;
    CALL _wt_pm_prepare;
    // Record beginning-of-block timbre envelopes for the arp's parent.
    I1 = R4;
    MODIFY(I1, 200) (NW);
    R2 = 8;
_wt_envelope_snapshot:
    R0 = DM(1, I1);
    DM(3, I1) = R0;
    MODIFY(I1, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_envelope_snapshot;
    I1 = R4;
    MODIFY(I1, 248) (NW);
    I2 = _wt_expr_starts;
    R2 = 8;
_wt_expr_snapshot:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_expr_snapshot;
    I1 = _wt_controls;
    R0 = DM(1, I1);
    F0 = FLOAT R0;
    F1 = 0.01;
    F0 = F0 * F1;
    F1 = DM(233, I0);
    R3 = DM(234, I0);
    R3 = PASS R3;
    IF EQ F1 = F0;
    F0 = F0 - F1;
    F2 = 0.0327838995;
    F0 = F0 * F2;
    F1 = F1 + F0;
    DM(233, I0) = F1;
    R0 = DM(0, I1);
    R1 = DM(234, I0);
    R1 = PASS R1;
    IF NE JUMP _wt_table_existing;
    DM(235, I0) = R0;
    DM(236, I0) = R0;
    R1 = 1;
    DM(234, I0) = R1;
    F1 = 1.0;
    DM(237, I0) = F1;
    // Start smooth-random movement in its first cycle, from zero toward
    // the first deterministic endpoint (the LCG seeded with zero).
    R1 = 1013904223;
    DM(244, I0) = R1;
    F1 = 0.0;
    DM(245, I0) = F1;
    F1 = -0.5278640985;
    DM(246, I0) = F1;
_wt_table_existing:
    // One transition at a time. New requests replace the pending destination.
    DM(238, I0) = R0;
    F0 = DM(237, I0);
    DM(_wt_table_start) = F0;
    F1 = 1.0;
    COMP(F0, F1);
    IF LT JUMP _wt_crossfade_advance;
    R0 = DM(236, I0);
    DM(235, I0) = R0;
    R1 = DM(238, I0);
    COMP(R0, R1);
    IF EQ JUMP _wt_crossfade_ready;
    DM(236, I0) = R1;
    F0 = 0.0;
_wt_crossfade_advance:
    DM(_wt_table_start) = F0;
    F1 = 0.03333333333; // 20ms linear table transition
    F0 = F0 + F1;
    F1 = 1.0;
    F0 = MIN(F0, F1);
    DM(237, I0) = F0;
_wt_crossfade_ready:
    R0 = DM(235, I0);
    DM(_wt_table_a) = R0;
    R1 = 37088; // 9272 words per factory table
    R1 = R0 * R1 (SSI);
    R2 = _wt_factory;
    R1 = R1 + R2;
    DM(_wt_table_base_a) = R1;
    R0 = DM(236, I0);
    DM(_wt_table_b) = R0;
    R1 = 37088;
    R1 = R0 * R1 (SSI);
    R2 = _wt_factory;
    R1 = R1 + R2;
    DM(_wt_table_base_b) = R1;
    R0 = DM(237, I0);
    DM(_wt_table_mix) = R0;
    F1 = DM(_wt_table_start);
    F0 = F0 - F1;
    F1 = 0.03125;
    F0 = F0 * F1;
    DM(_wt_table_step) = F0;
    // Shared phrase restart: only zero-to-held transition resets the scan.
    R2 = 4;
    I2 = R4;
    R3 = 0;
_wt_count_held:
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF EQ JUMP _wt_count_next;
    R0 = DM(1, I2);
    R1 = 3;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF EQ R3 = R1;
_wt_count_next:
    MODIFY(I2, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_count_held;
    I1 = _wt_controls;
    R0 = DM(5, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _wt_pattern_reset;
    R0 = DM(240, I0);
    R0 = PASS R0;
    IF NE JUMP _wt_reset_done;
    R3 = PASS R3;
    IF EQ JUMP _wt_reset_done;
    R0 = 0;
    DM(239, I0) = R0;
    JUMP _wt_reset_done;
_wt_pattern_reset:
    R1 = 2;
    COMP(R0, R1);
    IF NE JUMP _wt_reset_done;
    R0 = DM(_wt_input);
    I2 = R0;
    R0 = DM(0, I2);
    R1 = 3;
    COMP(R0, R1);
    IF LT JUMP _wt_reset_done;
    R0 = DM(12, I2);
    R1 = 3;
    R0 = R0 AND R1;
    COMP(R0, R1);
    IF NE JUMP _wt_reset_done;
    R0 = DM(13, I2);
    R1 = DM(241, I0);
    COMP(R0, R1);
    DM(241, I0) = R0;
    IF NE JUMP _wt_reset_pattern_phase;
    R0 = DM(19, I2);
    R1 = DM(257, I0);
    COMP(R0, R1);
    DM(257, I0) = R0;
    IF NE JUMP _wt_reset_pattern_phase;
    R0 = DM(14, I2);
    R1 = DM(256, I0);
    COMP(R0, R1);
    DM(256, I0) = R0;
    IF EQ JUMP _wt_reset_done;
    R0 = DM(15, I2);
    R0 = PASS R0;
    IF NE JUMP _wt_reset_done;
_wt_reset_pattern_phase:
    R0 = 0;
    DM(239, I0) = R0;
    R0 = DM(14, I2);
    DM(256, I0) = R0;
    R0 = DM(19, I2);
    DM(257, I0) = R0;
_wt_reset_done:
    DM(240, I0) = R3;
    R0 = DM(3, I1);
    F0 = FLOAT R0;
    F1 = 0.000006666666667; // .01Hz ×32/48000
    F0 = F0 * F1;
    R3 = DM(6, I1);
    R3 = PASS R3;
    IF NE JUMP _wt_scan_master;
    R3 = 0;
    DM(258, I0) = R3;
    JUMP _wt_scan_advance;
_wt_scan_master:
    // Use measured fine-clock progress; loss/stopped clock holds phase.
    R0 = DM(_wt_input);
    I2 = R0;
    R0 = DM(0, I2);
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _wt_scan_hold;
    R1 = 5;
    COMP(R0, R1);
    IF NE JUMP _wt_master_capable;
    R0 = DM(12, I2);
    R1 = 512;
    R0 = R0 AND R1;
    IF EQ JUMP _wt_scan_hold;
_wt_master_capable:
    R0 = DM(12, I2);
    R1 = 3;
    R0 = R0 AND R1;
    COMP(R0, R1);
    IF NE JUMP _wt_scan_hold;
    R0 = DM(20, I2);
    R1 = DM(242, I0);
    DM(242, I0) = R0;
    R2 = DM(258, I0);
    R3 = 1;
    DM(258, I0) = R3;
    R4 = DM(13, I2);
    R5 = DM(259, I0);
    DM(259, I0) = R4;
    COMP(R4, R5);
    IF NE JUMP _wt_scan_observed;
    R2 = PASS R2;
    IF EQ JUMP _wt_scan_observed;
    R0 = R0 - R1;
    R1 = 0;
    COMP(R0, R1);
    IF LT JUMP _wt_scan_hold;
    R1 = 21600000;
    COMP(R0, R1);
    IF GT JUMP _wt_scan_hold;
    F0 = FLOAT R0;
    F1 = 0.000000002893518519; // ticks / (16quarters×21600000)
    F0 = F0 * F1;
    R2 = DM(3, I1);
    R2 = R2 - 1;
    R3 = 1;
    R3 = LSHIFT R3 BY R2;
    F1 = FLOAT R3;
    F0 = F0 * F1;
    JUMP _wt_scan_advance;
_wt_scan_hold:
    R0 = 0;
    DM(258, I0) = R0;
_wt_scan_observed:
    F0 = 0.0;
_wt_scan_advance:
    F1 = DM(239, I0);
    F0 = F0 + F1;
    R2 = TRUNC F0;
    DM(_wt_scan_wraps) = R2;
    F2 = FLOAT R2;
    F0 = F0 - F2;
    DM(239, I0) = F0;
    // Triangle in [-1,1], then smooth sine-like easing or random interpolation.
    F1 = 0.5;
    F0 = F0 - F1;
    F0 = ABS F0;
    F1 = 4.0;
    F0 = F0 * F1;
    F1 = 1.0;
    F0 = F0 - F1;
    R6 = DM(4, I1);
    R7 = 1;
    COMP(R6, R7);
    IF EQ JUMP _wt_scan_shape_ready;
    // sin(pi/2 * triangle), ninth order (<4e-6 endpoint error).
    F1 = F0 * F0;
    F2 = 0.0001604411848;
    F2 = F2 * F1;
    F3 = -0.004681754136;
    F2 = F2 + F3;
    F2 = F2 * F1;
    F3 = 0.07969262625;
    F2 = F2 + F3;
    F2 = F2 * F1;
    F3 = -0.6459640975;
    F2 = F2 + F3;
    F2 = F2 * F1;
    F3 = 1.570796327;
    F2 = F2 + F3;
    F0 = F0 * F2;
    R7 = 2;
    COMP(R6, R7);
    IF NE JUMP _wt_scan_shape_ready;
    // Advance every elapsed cycle, including exact/multiple master-clock
    // cycles whose fractional phase does not decrease. At most four cycles
    // can pass the validated quarter-note delta at the fastest division.
    R6 = DM(_wt_scan_wraps);
    R6 = PASS R6;
    IF EQ JUMP _wt_random_ready;
_wt_random_advance:
    R0 = DM(244, I0);
    R1 = 1664525;
    R0 = R0 * R1 (SSI);
    R1 = 1013904223;
    R0 = R0 + R1;
    DM(244, I0) = R0;
    R0 = LSHIFT R0 BY -8;
    F2 = FLOAT R0;
    F3 = 0.0000001192092896;
    F2 = F2 * F3;
    F3 = 1.0;
    F2 = F2 - F3;
    R0 = DM(246, I0);
    DM(245, I0) = R0;
    DM(246, I0) = F2;
    R6 = R6 - 1;
    IF NE JUMP _wt_random_advance;
_wt_random_ready:
    F1 = DM(239, I0);
    DM(243, I0) = F1;
    F2 = F1 * F1;
    F3 = 2.0;
    F3 = F3 * F1;
    F4 = 3.0;
    F3 = F4 - F3;
    F2 = F2 * F3;
    F0 = DM(246, I0);
    F3 = DM(245, I0);
    F0 = F0 - F3;
    F0 = F0 * F2;
    F0 = F0 + F3;
_wt_scan_shape_ready:
    R6 = DM(2, I1);
    F1 = FLOAT R6;
    F2 = 0.035;
    F1 = F1 * F2;
    F0 = F0 * F1;
    F1 = DM(247, I0);
    DM(_wt_scan_start) = F1;
    DM(247, I0) = F0;
    F0 = F0 - F1;
    F1 = 0.03125;
    F0 = F0 * F1;
    DM(_wt_scan_step) = F0;
    F0 = DM(233, I0);
    DM(_wt_position) = F0;
    RTS;

// REPLACE keeps old timbre until its last tail sample, then invalidates
// ownership before a new generation can reuse its low token serials.
.GLOBAL _wt_finish;
_wt_finish:
    R0 = DM(_wt_state);
    I0 = R0;
    R0 = DM(263, I0);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = DM(136, I0);
    R0 = PASS R0;
    IF NE RTS;
    R2 = 6;
_wt_finish_slots:
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF NE RTS;
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_finish_slots;
    R0 = DM(_wt_state);
    I0 = R0;
    R0 = 0;
    DM(263, I0) = R0;
    MODIFY(I0, 200) (NW);
    R2 = 32;
_wt_finish_aux:
    DM(I0, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_finish_aux;
    MODIFY(I0, 16) (NW);
    R2 = 8;
_wt_finish_expression:
    DM(I0, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_finish_expression;
    RTS;

// atan2(F0,F1)/(2pi), normalized to [0,1). Range reduction confines the
// Taylor series to |z|<=tan(pi/8); 13th order keeps error below float32 noise.
// Scratch R0..7. Two Newton reciprocal refinements avoid seed precision error.
_wt_atan:
    R6 = R0;
    R7 = R1;
    F0 = ABS F0;
    F1 = ABS F1;
    F2 = MIN(F0, F1);
    F3 = MAX(F0, F1);
    F4 = RECIPS F3;
    F5 = F3 * F4;
    F0 = 2.0;
    F5 = F0 - F5;
    F4 = F4 * F5;
    F5 = F3 * F4;
    F5 = F0 - F5;
    F4 = F4 * F5;
    F2 = F2 * F4;
    F5 = 0.0;
    F3 = 0.4142135624;
    COMP(F2, F3);
    IF LE JUMP _wt_atan_poly;
    F3 = 1.0;
    F4 = F2 + F3;
    F2 = F2 - F3;
    F0 = RECIPS F4;
    F1 = F4 * F0;
    F3 = 2.0;
    F1 = F3 - F1;
    F0 = F0 * F1;
    F1 = F4 * F0;
    F1 = F3 - F1;
    F0 = F0 * F1;
    F2 = F2 * F0;
    F5 = 0.7853981634;
_wt_atan_poly:
    F3 = F2 * F2;
    F4 = 0.07692307692;
    F4 = F4 * F3;
    F0 = -0.09090909091;
    F4 = F4 + F0;
    F4 = F4 * F3;
    F0 = 0.1111111111;
    F4 = F4 + F0;
    F4 = F4 * F3;
    F0 = -0.1428571429;
    F4 = F4 + F0;
    F4 = F4 * F3;
    F0 = 0.2;
    F4 = F4 + F0;
    F4 = F4 * F3;
    F0 = -0.3333333333;
    F4 = F4 + F0;
    F4 = F4 * F3;
    F0 = 1.0;
    F4 = F4 + F0;
    F4 = F4 * F2;
    F4 = F4 + F5;
    F0 = ABS F6;
    F1 = ABS F7;
    COMP(F0, F1);
    F0 = 1.570796327;
    IF GT F4 = F0 - F4;
    F7 = PASS F7;
    F0 = 3.141592654;
    IF LT F4 = F0 - F4;
    F6 = PASS F6;
    F0 = 6.283185307;
    IF LT F4 = F0 - F4;
    F0 = 0.1591549431;
    F0 = F4 * F0;
    RTS;

_wt_begin_slot:
    R0 = DM(_wt_enabled);
    R0 = PASS R0;
    IF EQ RTS;
    CALL _wt_save_regs;
    CALL _wt_slot;
    CALL _wt_restore_regs;
    RTS;
_wt_slot:
    R0 = 0;
    DM(_wt_is_new) = R0;
    DM(_wt_pm_parent) = R0;
    R0 = DM(_wt_source_start);
    DM(_wt_source_cursor) = R0;
    R0 = DM(_wt_table_start);
    DM(_wt_table_cursor) = R0;
    R0 = DM(_wt_scan_start);
    DM(_wt_scan_cursor) = R0;
    R0 = I0;
    DM(_wt_slot_address) = R0;
    R0 = DM(1, I0);
    DM(_wt_gate) = R0;
    R1 = 4;
    R0 = R0 AND R1;
    R0 = LSHIFT R0 BY 6;
    DM(_wt_lane) = R0;
    R0 = DM(_wt_state);
    R1 = 544;
    R0 = R0 + R1;
    R1 = I0;
    COMP(R0, R1);
    IF NE JUMP _wt_lane_ready;
    R0 = 256;
    DM(_wt_lane) = R0;
_wt_lane_ready:
    F0 = F2;
    F1 = F3;
    CALL _wt_atan;
    DM(_wt_phase) = F0;
    R0 = DM(_wt_r4);
    R1 = DM(_wt_r5);
    CALL _wt_atan;
    DM(_wt_increment) = F0;
    // PM broadens every carrier harmonic; resolve its conservative frequency
    // bound after ownership is known, immediately before the mip selection.
    JUMP _wt_mip_later;
_wt_choose_mip:
    // Mip starts filtering at .20/H cycles/sample and reaches the next
    // level by .40/H. The richer band is gone well before Nyquist.
    I1 = _wt_mip_sizes;
    I2 = _wt_mip_offsets;
    R6 = 0;
    F1 = 64.0;
_wt_mip_find:
    F2 = F0 * F1;
    F3 = 0.4;
    COMP(F2, F3);
    IF LT JUMP _wt_mip_found;
    R6 = R6 + 1;
    MODIFY(I1, 1) (NW);
    MODIFY(I2, 1) (NW);
    F3 = 0.5;
    F1 = F1 * F3;
    R7 = 6;
    COMP(R6, R7);
    IF LT JUMP _wt_mip_find;
_wt_mip_found:
    R0 = DM(0, I1);
    F0 = FLOAT R0;
    DM(_wt_size_a) = F0;
    R0 = DM(0, I2);
    DM(_wt_offset_a) = R0;
    R0 = DM(1, I1);
    F0 = FLOAT R0;
    DM(_wt_size_b) = F0;
    R0 = DM(1, I2);
    DM(_wt_offset_b) = R0;
    F0 = 0.2;
    F2 = F2 - F0;
    F0 = 5.0;
    F2 = F2 * F0;
    F0 = 0.0;
    F2 = MAX(F2, F0);
    F0 = 1.0;
    F2 = MIN(F2, F0);
    DM(_wt_mip_mix) = F2;
    RTS;
_wt_mip_later:
    // Resolve token history; prune only records no longer sounding anywhere.
    R0 = DM(_wt_slot_address);
    I0 = R0;
    R8 = DM(0, I0);
    R0 = DM(_wt_state);
    I1 = R0;
    MODIFY(I1, 200) (NW);
    R2 = 8;
    R3 = 0;
_wt_find_token:
    R0 = DM(0, I1);
    COMP(R0, R8);
    IF NE JUMP _wt_token_not_match;
    R0 = DM(2, I1);
    R1 = 256;
    R0 = R0 AND R1;
    R1 = DM(_wt_lane);
    COMP(R0, R1);
    IF EQ JUMP _wt_resolve_parent;
    R0 = DM(0, I1);
_wt_token_not_match:
    R0 = PASS R0;
    IF NE JUMP _wt_token_next;
    R3 = I1;
_wt_token_next:
    MODIFY(I1, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_find_token;
    R3 = PASS R3;
    IF NE JUMP _wt_new_token;
    R0 = DM(_wt_state);
    I1 = R0;
    MODIFY(I1, 200) (NW);
    R2 = 8;
_wt_find_stale:
    R9 = DM(0, I1);
    R7 = DM(2, I1);
    R0 = 256;
    R7 = R7 AND R0;
    R0 = DM(_wt_state);
    I2 = R0;
    R10 = 6;
_wt_token_liveness:
    R0 = DM(0, I2);
    COMP(R0, R9);
    IF NE JUMP _wt_liveness_next;
    R0 = DM(1, I2);
    R1 = 4;
    R0 = R0 AND R1;
    R0 = LSHIFT R0 BY 6;
    COMP(R0, R7);
    IF EQ JUMP _wt_record_next;
_wt_liveness_next:
    MODIFY(I2, 8) (NW);
    R10 = R10 - 1;
    IF NE JUMP _wt_token_liveness;
    R0 = DM(_wt_state);
    I2 = R0;
    R0 = DM(136, I2);
    COMP(R0, R9);
    IF NE JUMP _wt_use_stale;
    R0 = 256;
    COMP(R7, R0);
    IF NE JUMP _wt_use_stale;
_wt_record_next:
    MODIFY(I1, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_find_stale;
    // Eight records for seven sounding groups always leave a reclaimable one.
_wt_use_stale:
    R3 = I1;
_wt_new_token:
    R0 = 1;
    DM(_wt_is_new) = R0;
    I1 = R3;
    DM(0, I1) = R8;
    R0 = 0;
    DM(1, I1) = R0;
    DM(3, I1) = R0;
    R0 = DM(_wt_lane);
    R1 = 32;
    R0 = R0 OR R1;
    DM(2, I1) = R0;
    // A reused WT record also invalidates its independent PM history.
    R0 = I1;
    CALL _wt_pm_address;
    R0 = 0;
    DM(0,I2) = R0;
_wt_find_route:
    R0 = DM(_wt_state);
    I2 = R0;
    MODIFY(I2, 73) (NW);
    R2 = 10;
_wt_route:
    R0 = DM(0, I2);
    COMP(R0, R8);
    IF EQ JUMP _wt_route_found;
    MODIFY(I2, 2) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_route;
    JUMP _wt_resolve_parent;
_wt_route_found:
    R0 = DM(1, I2);
    R0 = FEXT R0 BY 0:6;
    R1 = DM(_wt_lane);
    R0 = R0 OR R1;
    DM(2, I1) = R0;
_wt_resolve_parent:
    // The active arp articulates its parent's evolving timbre even while its
    // amplitude gate releases. Shared tails retain their captured history.
    R0 = DM(_wt_slot_address);
    R1 = DM(_wt_state);
    R2 = 544;
    R1 = R1 + R2;
    COMP(R0, R1);
    IF NE JUMP _wt_token_ready;
    R0 = DM(_wt_state);
    I2 = R0;
    R9 = DM(155, I2);
    R2 = 4;
_wt_parent_gate:
    R0 = DM(0, I2);
    COMP(R0, R9);
    IF EQ JUMP _wt_parent_gate_found;
    MODIFY(I2, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_parent_gate;
    JUMP _wt_token_ready;
_wt_parent_gate_found:
    R0 = DM(1, I2);
    DM(_wt_gate) = R0;
    R0 = DM(_wt_state);
    I2 = R0;
    MODIFY(I2, 200) (NW);
    R2 = 8;
_wt_parent:
    R0 = DM(0, I2);
    COMP(R0, R9);
    IF NE JUMP _wt_parent_next;
    R0 = DM(2, I2);
    R1 = 256;
    R0 = R0 AND R1;
    IF EQ JUMP _wt_parent_found;
_wt_parent_next:
    MODIFY(I2, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_parent;
    JUMP _wt_token_ready;
_wt_parent_found:
    R0 = I2;
    DM(_wt_pm_parent) = R0;
    R0 = DM(3, I2);
    DM(1, I1) = R0;
    R0 = DM(2, I2);
    R1 = 256;
    R0 = R0 OR R1;
    DM(2, I1) = R0;
    R0 = I2;
    R1 = DM(_wt_state);
    R0 = R0 - R1;
    R1 = 800;
    R0 = R0 - R1;
    R0 = LSHIFT R0 BY -2;
    R1 = _wt_expr_starts;
    R0 = R0 + R1;
    I2 = R0;
    R2 = DM(0, I2);
    R0 = I1;
    R1 = DM(_wt_state);
    R0 = R0 - R1;
    R1 = 800;
    R0 = R0 - R1;
    R0 = LSHIFT R0 BY -2;
    R1 = DM(_wt_state);
    R0 = R0 + R1;
    I2 = R0;
    DM(248, I2) = R2;
    R0 = 0;
    DM(_wt_is_new) = R0;
_wt_token_ready:
    R0 = I1;
    DM(_wt_aux) = R0;
    R0 = DM(1, I1);
    DM(_wt_env) = R0;
    R0 = DM(2, I1);
    R0 = FEXT R0 BY 0:6;
    R1 = 32;
    COMP(R0, R1);
    R1 = 0;
    IF GE JUMP _wt_wheel_ready;
    R0 = LSHIFT R0 BY 2;
    R1 = DM(_wt_state);
    R0 = R0 + R1;
    I2 = R0;
    R1 = DM(264, I2);
_wt_wheel_ready:
    DM(_wt_pm_wheel) = R1;
    F0 = FLOAT R1;
    F1 = 0.0005511811024; // wheel/127 ×7/100
    F0 = F0 * F1;
    I1 = _wt_controls;
    R4 = DM(11, I1);
    R5 = 100;
    R4 = R4 - R5;
    F1 = FLOAT R4;
    F0 = F0 * F1;
    F2 = DM(_wt_position);
    F0 = F0 + F2;
    F2 = DM(_wt_r6); // normalized velocity
    R4 = DM(10, I1);
    R4 = R4 - R5;
    F1 = FLOAT R4;
    F3 = 0.07;
    F1 = F1 * F3;
    F1 = F1 * F2;
    F0 = F0 + F1;
    // Smooth expression edits per owner, then interpolate every sample.
    R2 = DM(_wt_aux);
    R3 = DM(_wt_state);
    R2 = R2 - R3;
    R3 = 800;
    R2 = R2 - R3;
    R2 = LSHIFT R2 BY -2; // aux record byte stride16 -> position byte stride4
    R3 = DM(_wt_state);
    R2 = R2 + R3;
    I2 = R2;
    F1 = DM(248, I2);
    R4 = DM(_wt_is_new);
    R4 = PASS R4;
    IF NE F1 = F0;
    DM(_wt_note_position) = F1;
    // Also publish a new owner's initial expression for a same-block arp.
    R3 = DM(_wt_aux);
    R4 = DM(_wt_state);
    R3 = R3 - R4;
    R4 = 800;
    R3 = R3 - R4;
    R3 = LSHIFT R3 BY -2;
    R4 = _wt_expr_starts;
    R3 = R3 + R4;
    I0 = R3;
    DM(0, I0) = F1;
    F0 = F0 - F1;
    F2 = 0.0327838995;
    F0 = F0 * F2;
    F1 = F1 + F0;
    DM(248, I2) = F1;
    F1 = 0.03125;
    F0 = F0 * F1;
    DM(_wt_position_step) = F0;
    R1 = 100;
    R0 = DM(7, I1);
    R0 = R0 - R1;
    F0 = FLOAT R0;
    F1 = 0.07;
    F0 = F0 * F1;
    DM(_wt_env_amount) = F0;
    R0 = DM(_wt_gate);
    R1 = 3;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _wt_env_attack;
    R0 = DM(9, I1);
    R0 = LSHIFT R0 BY 3;
    R1 = _cp_envelopes;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(1, I1);
    JUMP _wt_env_step_ready;
_wt_env_attack:
    R0 = DM(8, I1);
    R0 = LSHIFT R0 BY 3;
    R1 = _cp_envelopes;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(0, I1);
_wt_env_step_ready:
    DM(_wt_env_step) = R0;
    CALL _wt_pm_slot;
    F0 = DM(_wt_pm_frequency);
    CALL _wt_choose_mip;
    RTS;

// Wavetable rendering of one voice block (replaces the per-sample tick).
// The voice leaves its 32 pad-oscillator samples in word 4 of the 8-word
// records of _wt_blk; this routine replaces them with the wavetable output.
// Record: 0 frame byte offset, 1 envelope, 2 frame mix, 3 read phase,
// 4 audio, 5 table A sample, 6 table B sample, 7 unused. Record 32 pads the
// one-ahead loads of the last iteration. The passes keep every per-sample
// float operation and its operand order from the per-sample tick, so the
// output is bit-identical; only the schedule and the call overhead change.
// Needs M4 = 8 (record stride), M5 = 0, M6 = 1, L0..L2 = 0.
// Clobbers R0..R15, I0..I2.
_wt_render_block:
    // Pass 1: scan cursor, note position, phase, envelope, frame position.
    F8 = DM(_wt_scan_cursor);
    F12 = DM(_wt_scan_step);
    F9 = DM(_wt_note_position);
    F13 = DM(_wt_position_step);
    F10 = DM(_wt_phase);
    F14 = DM(_wt_increment);
    F11 = DM(_wt_env);
    F15 = DM(_wt_env_step);
    F3 = DM(_wt_env_amount);
    F4 = 0.0;
    F5 = 1.0;
    F6 = 7.0;
    R7 = 1159;
    I0 = _wt_blk;
    LCNTR = 32, DO _wt_control_last UNTIL LCE;
    F8 = F8 + F12;
    F9 = F9 + F13;
    F10 = F10 + F14;
    COMP(F10, F5);
    IF GE F10 = F10 - F5;
    F11 = F11 + F15;
    F11 = MAX(F11, F4);
    F11 = MIN(F11, F5);
    F0 = F11 * F3, DM(3,I0) = F10;
    F0 = F0 + F9, DM(1,I0) = F11;
    F0 = F0 + F8;
    F0 = MAX(F0, F4);
    F0 = MIN(F0, F6);
    R1 = TRUNC F0;
    F2 = FLOAT R1;
    F0 = F0 - F2;
    R1 = R1 * R7 (SSI), DM(2,I0) = F0;
    R1 = LSHIFT R1 BY 2;
_wt_control_last:
    DM(I0,M4) = R1;
    DM(_wt_scan_cursor) = F8;
    DM(_wt_note_position) = F9;
    DM(_wt_phase) = F10;
    DM(_wt_read_phase) = F10;
    DM(_wt_env) = F11;
    DM(_wt_frame_mix) = F0;
    R1 = LSHIFT R1 BY -2;
    DM(_wt_frame) = R1;
    R0 = DM(_wt_aux);
    I1 = R0;
    DM(1,I1) = F11;
    // Pass 2: phase modulation of the read phase.
    R0 = DM(_wt_pm_active);
    R0 = PASS R0;
    IF NE CALL _wt_pm_block;
    // Pass 3: table reads. Each table's address is its mip offset and base
    // folded into one constant; integer sums are unchanged by reassociation.
    R0 = DM(_wt_offset_a);
    R0 = LSHIFT R0 BY 2;
    R1 = DM(_wt_offset_b);
    R1 = LSHIFT R1 BY 2;
    R2 = DM(_wt_table_base_a);
    R10 = R0 + R2;
    R11 = R1 + R2;
    R2 = DM(_wt_table_base_b);
    R12 = R0 + R2;
    R13 = R1 + R2;
    F8 = DM(_wt_size_a);
    F9 = DM(_wt_size_b);
    F6 = DM(_wt_mip_mix);
    I0 = _wt_blk;
    R0 = DM(_wt_table_base_b);
    R1 = DM(_wt_table_base_a);
    COMP(R0, R1);
    R0 = DM(I0,M4); // frame offset of record 0; each iteration loads the next
    IF EQ JUMP _wt_tables_one;
    LCNTR = 32, DO _wt_tables_two_last UNTIL LCE;
    F1 = DM(-5,I0); // read phase
    F2 = F8 * F1, F7 = DM(-6,I0); // frame mix
    R3 = TRUNC F2;
    F4 = FLOAT R3;
    F4 = F2 - F4;
    R3 = LSHIFT R3 BY 2;
    R14 = R3 + R0;
    F2 = F9 * F1;
    R3 = TRUNC F2;
    F5 = FLOAT R3;
    F5 = F2 - F5;
    R3 = LSHIFT R3 BY 2;
    R15 = R3 + R0;
    R1 = R14 + R10;
    R2 = R15 + R11, I1 = R1;
    I2 = R2;
    R3 = 32452; // final frame 7*1159 words, as bytes: no read past the table
    COMP(R0, R3);
    IF EQ JUMP _wt_two_final;
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    DM(-3,I0) = F0; // first frame; the next frame follows
    MODIFY(I1,1159) (NW);
    MODIFY(I2,1159) (NW);
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    F1 = DM(-3,I0);
    F0 = F0 - F1;
    F0 = F0 * F7;
    F0 = F0 + F1;
    DM(-3,I0) = F0;
    R1 = R14 + R12;
    R2 = R15 + R13, I1 = R1;
    I2 = R2;
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    DM(-2,I0) = F0; // first frame; the next frame follows
    MODIFY(I1,1159) (NW);
    MODIFY(I2,1159) (NW);
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    F1 = DM(-2,I0);
    F0 = F0 - F1;
    F0 = F0 * F7;
    F0 = F0 + F1;
    JUMP _wt_two_join;
_wt_two_final:
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    DM(-3,I0) = F0;
    R1 = R14 + R12;
    R2 = R15 + R13, I1 = R1;
    I2 = R2;
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
_wt_two_join:
    DM(-2,I0) = F0;
_wt_tables_two_last:
    R0 = DM(I0,M4);
    JUMP _wt_blend;
_wt_tables_one:
    LCNTR = 32, DO _wt_tables_one_last UNTIL LCE;
    F1 = DM(-5,I0); // read phase
    F2 = F8 * F1, F7 = DM(-6,I0); // frame mix
    R3 = TRUNC F2;
    F4 = FLOAT R3;
    F4 = F2 - F4;
    R3 = LSHIFT R3 BY 2;
    R14 = R3 + R0;
    F2 = F9 * F1;
    R3 = TRUNC F2;
    F5 = FLOAT R3;
    F5 = F2 - F5;
    R3 = LSHIFT R3 BY 2;
    R15 = R3 + R0;
    R1 = R14 + R10;
    R2 = R15 + R11, I1 = R1;
    I2 = R2;
    R3 = 32452; // final frame 7*1159 words, as bytes: no read past the table
    COMP(R0, R3);
    IF EQ JUMP _wt_one_final;
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    DM(-3,I0) = F0; // first frame; the next frame follows
    MODIFY(I1,1159) (NW);
    MODIFY(I2,1159) (NW);
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
    F1 = DM(-3,I0);
    F0 = F0 - F1;
    F0 = F0 * F7;
    F0 = F0 + F1;
    JUMP _wt_one_join;
_wt_one_final:
    F0 = DM(0,I1);
    F1 = DM(1,I1);
    F1 = F1 - F0, F2 = DM(0,I2);
    F1 = F4 * F1, F3 = DM(1,I2);
    F0 = F1 + F0;
    F3 = F3 - F2;
    F3 = F5 * F3;
    F2 = F3 + F2;
    F2 = F2 - F0;
    F2 = F2 * F6;
    F0 = F2 + F0;
_wt_one_join:
    DM(-3,I0) = F0;
_wt_tables_one_last:
    R0 = DM(I0,M4);
_wt_blend:
    // Pass 4: table transition and source blend into the audio word.
    F8 = DM(_wt_source_cursor);
    F12 = DM(_wt_source_step);
    F9 = DM(_wt_table_cursor);
    F13 = DM(_wt_table_step);
    I0 = _wt_blk;
    MODIFY(I0,4) (NW); // audio word at offset 0
    R0 = DM(_wt_table_base_b);
    R1 = DM(_wt_table_base_a);
    COMP(R0, R1);
    IF EQ JUMP _wt_blend_one;
    LCNTR = 32, DO _wt_blend_two_last UNTIL LCE;
    F8 = F8 + F12;
    F9 = F9 + F13, F1 = DM(1,I0); // table A sample
    F0 = DM(2,I0); // table B sample
    F0 = F0 - F1;
    F0 = F0 * F9;
    F0 = F0 + F1, F1 = DM(0,I0); // pad oscillator
    F0 = F0 - F1;
    F0 = F0 * F8;
    F0 = F0 + F1;
_wt_blend_two_last:
    DM(I0,M4) = F0;
    JUMP _wt_blend_done;
_wt_blend_one:
    LCNTR = 32, DO _wt_blend_one_last UNTIL LCE;
    F8 = F8 + F12;
    F9 = F9 + F13, F1 = DM(0,I0); // pad oscillator
    F0 = DM(1,I0); // table A sample
    F0 = F0 - F1;
    F0 = F0 * F8;
    F0 = F0 + F1;
_wt_blend_one_last:
    DM(I0,M4) = F0;
_wt_blend_done:
    DM(_wt_source_cursor) = F8;
    DM(_wt_table_cursor) = F9;
    RTS;
// A WT auxiliary ordinal owns six otherwise reserved words of the mutually
// exclusive FM history: token, phase, ratio, depth, increment, reserved.
// R0 WT auxiliary address -> I2 PM history; scratch R0..3, preserve I0/I1.
_wt_pm_address:
    R1 = DM(_wt_state);
    R0 = R0 - R1;
    R2 = 800;
    R0 = R0 - R2;
    R2 = LSHIFT R0 BY 3;
    R0 = LSHIFT R0 BY 2;
    R0 = R0 + R2;
    R2 = 1348; // (296+41)*4
    R0 = R0 + R2;
    R0 = R0 + R1;
    I2 = R0;
    RTS;

_wt_pm_prepare:
    // Only CPSB interprets the last four high halfwords. Older banks leave
    // audio, history and modulation untouched, even after a newer bank.
    R0 = DM(_wt_pm_active);
    R0 = PASS R0;
    IF EQ RTS;
    I1 = _wt_controls;
    R0 = DM(12,I1);
    F0 = FLOAT R0;
    F1 = 0.01;
    F0 = F0 * F1;
    DM(_wt_pm_depth) = F0;
    R0 = DM(13,I1);
    R0 = LSHIFT R0 BY 2;
    R1 = _fm_ratios;
    R0 = R0 + R1;
    I2 = R0;
    R0 = DM(0,I2);
    DM(_wt_pm_ratio) = R0;
    R0 = DM(14,I1);
    R1 = 100;
    R0 = R0 - R1;
    F0 = FLOAT R0;
    F1 = 0.01;
    F0 = F0 * F1;
    DM(_wt_pm_env_amount) = F0;
    R0 = DM(15,I1);
    R1 = 100;
    R0 = R0 - R1;
    F0 = FLOAT R0;
    F1 = 0.00007874015748; // signed amount/100, wheel/127
    F0 = F0 * F1;
    DM(_wt_pm_wheel_amount) = F0;
    // Snapshots are callback-local; publish each pad's actual beginning below.
    I1 = _wt_pm_snapshots;
    R0 = 0;
    R2 = 8;
_wt_pm_clear_snapshots:
    DM(0,I1) = R0;
    MODIFY(I1,6) (NW);
    R2 = R2 - 1;
    IF NE JUMP _wt_pm_clear_snapshots;
    RTS;

_wt_pm_slot:
    F0 = DM(_wt_increment);
    DM(_wt_pm_frequency) = F0;
    R0 = DM(_wt_pm_active);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = DM(_wt_aux);
    CALL _wt_pm_address;
    R0 = I2;
    DM(_wt_pm_aux) = R0;
    I1 = R0;
    R0 = DM(_wt_aux);
    I0 = R0;
    R3 = DM(0,I0);
    R0 = DM(0,I1);
    COMP(R0,R3);
    IF EQ JUMP _wt_pm_existing;
    DM(0,I1) = R3;
    R0 = 0;
    DM(1,I1) = R0;
    DM(3,I1) = R0;
    DM(5,I1) = R0;
    R0 = DM(_wt_pm_ratio);
    DM(2,I1) = R0;
    // Same-frequency arp starts from the pad's beginning-of-block history.
    // Different pitches keep their own phase; envelope/route still inherit.
    R0 = DM(_wt_pm_parent);
    R0 = PASS R0;
    IF EQ JUMP _wt_pm_existing;
    CALL _wt_pm_address;
    R0 = I2;
    CALL _wt_pm_snapshot_address;
    R0 = DM(0,I2);
    R0 = PASS R0;
    IF EQ JUMP _wt_pm_existing;
    R0 = DM(4,I2);
    R1 = DM(_wt_increment);
    COMP(R0,R1);
    IF NE JUMP _wt_pm_existing;
    R0 = DM(1,I2);
    DM(1,I1) = R0;
    R0 = DM(2,I2);
    DM(2,I1) = R0;
    R0 = DM(3,I2);
    DM(3,I1) = R0;
_wt_pm_existing:
    R0 = DM(_wt_increment);
    DM(4,I1) = R0;
    R0 = DM(_wt_lane);
    R0 = PASS R0;
    IF NE JUMP _wt_pm_depth_target;
    R0 = I1;
    CALL _wt_pm_snapshot_address;
    R2 = 6;
    I0 = I1;
_wt_pm_snapshot:
    R0 = DM(I0,M6);
    DM(I2,M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _wt_pm_snapshot;
_wt_pm_depth_target:
    R0 = DM(_wt_pm_wheel);
    F0 = FLOAT R0;
    F1 = DM(_wt_pm_wheel_amount);
    F0 = F0 * F1;
    F1 = DM(_wt_pm_depth);
    F0 = F0 + F1;
    DM(_wt_pm_base) = F0;
    // Ratio advances smoothly every sample, preserving continuous phase.
    F0 = DM(2,I1);
    F1 = DM(_wt_pm_ratio);
    F2 = MAX(F0,F1);
    DM(_wt_pm_ratio_bound) = F2;
    F1 = F1 - F0;
    F2 = 0.003900835401; // (1-exp(-32/(48000*.005)))/32
    F1 = F1 * F2;
    DM(_wt_pm_ratio_step) = F1;
    F2 = 32.0;
    F2 = F1 * F2;
    F2 = F2 + F0;
    F2 = MAX(F0,F2);
    F3 = 0.00004; // covers repeated float additions through this block
    F2 = F2 + F3;
    DM(_wt_pm_render_ratio_bound) = F2;
    F0 = DM(_wt_pm_ratio_bound);
    F1 = 1.0;
    F0 = MAX(F0,F1);
    F1 = DM(_wt_increment);
    F3 = F0 * F1; // highest requested/current oscillator /48000
    F0 = 0.2;
    F0 = F0 - F3;
    F1 = 10.0;
    F0 = F0 * F1;
    F1 = 0.0;
    F0 = MAX(F0,F1);
    F1 = 1.0;
    F0 = MIN(F0,F1);
    F2 = RECIPS F3;
    F1 = F3 * F2;
    F4 = 2.0;
    F1 = F4 - F1;
    F2 = F2 * F1;
    F1 = F3 * F2;
    F1 = F4 - F1;
    F2 = F2 * F1;
    F1 = 0.007;
    F2 = F2 * F1;
    F1 = 1.0;
    F2 = MIN(F2,F1);
    F0 = F0 * F2;
    // Depth is squared, so take sqrt here to apply the bandwidth taper once.
    // One reciprocal-square-root refinement stays close to the analytic limit.
    F1 = 0.0;
    COMP(F0,F1);
    IF LE JUMP _wt_pm_scale_ready;
    F3 = RSQRTS F0;
    F2 = F3 * F3;
    F2 = F2 * F0;
    F1 = 0.5;
    F2 = F2 * F1;
    F1 = 1.5;
    F2 = F1 - F2;
    F3 = F3 * F2;
    F0 = F0 * F3;
_wt_pm_scale_ready:
    DM(_wt_pm_key_scale) = F0;
    // DEP is squared in the phase expression (maximum one radian). A mip
    // bound fc*(1+4*d*ratio) includes the significant PM sidebands of every
    // carrier harmonic. Bound only values reachable during this block: using
    // the final requested depth immediately would darken the table before the
    // audible depth ramp. Small margins cover repeated float32 additions.
    F1 = DM(_wt_pm_env_amount);
    F2 = 0.0;
    F1 = MAX(F1,F2);
    F2 = DM(_wt_pm_base);
    F1 = F1 + F2;
    F2 = 0.0;
    F1 = MAX(F1,F2);
    F2 = 1.0;
    F1 = MIN(F1,F2);
    F1 = F1 * F0;
    F0 = DM(3,I1);
    F1 = F1 - F0;
    F2 = 0.124826732846117; // 1-(1-.004158)^32
    F1 = F1 * F2;
    F1 = F1 + F0;
    F1 = MAX(F1,F0);
    F2 = 1.00001;
    F1 = F1 * F2;
    F0 = DM(_wt_pm_render_ratio_bound);
    F1 = F1 * F0;
    F0 = 4.0;
    F1 = F1 * F0;
    F0 = 1.0;
    F1 = F1 + F0;
    F0 = DM(_wt_increment);
    F0 = F0 * F1;
    DM(_wt_pm_frequency) = F0;
    RTS;

// R0 PM record -> I2 matching callback snapshot. Preserve I1.
_wt_pm_snapshot_address:
    R1 = DM(_wt_state);
    R0 = R0 - R1;
    R1 = 1348;
    R0 = R0 - R1;
    R0 = LSHIFT R0 BY -3; // record192bytes -> snapshot24bytes
    R1 = _wt_pm_snapshots;
    R0 = R0 + R1;
    I2 = R0;
    RTS;

// Phase-modulation pass of _wt_render_block: per sample the depth follows
// its target, the ratio and modulator phase advance, and a nonzero depth
// offsets the record's read phase. Same operations as the per-sample tick.
// Needs M4 = 8; clobbers R0..R15, I0..I2.
_wt_pm_block:
    R0 = DM(_wt_pm_aux);
    I2 = R0;
    F8 = DM(3,I2); // depth
    F9 = DM(2,I2); // ratio
    F10 = DM(1,I2); // modulator phase
    F11 = DM(_wt_pm_env_amount);
    F12 = DM(_wt_pm_base);
    F13 = DM(_wt_pm_key_scale);
    F14 = DM(_wt_pm_ratio_step);
    F15 = DM(_wt_increment);
    F3 = 0.0;
    F4 = 1.0;
    F5 = 0.004158;
    F7 = 0.1591549431; // one radian in cycles
    // I0 addresses the read phase word: envelope at -2. F6 holds the
    // envelope, then the read phase, which the join stores and steps past.
    I0 = _wt_blk;
    MODIFY(I0,3) (NW);
    F6 = DM(-2,I0); // envelope of record 0; each iteration loads the next
    LCNTR = 32, DO _wt_pm_last UNTIL LCE;
    F0 = F6 * F11;
    F6 = DM(0,I0); // read phase: unchanged unless modulated
    F0 = F0 + F12;
    F0 = MAX(F0, F3);
    F0 = MIN(F0, F4);
    F0 = F0 * F13;
    F2 = F0;
    F0 = F0 - F8;
    F0 = F0 * F5;
    F0 = F0 + F8;
    F1 = 0.0000001;
    COMP(F0, F1);
    IF GE JUMP _wt_pm_depth_kept;
    COMP(F2, F3);
    IF EQ F0 = F3;
_wt_pm_depth_kept:
    F8 = F0;
    F9 = F9 + F14;
    F0 = F9 * F15;
    F0 = F0 + F10;
    R2 = TRUNC F0;
    F1 = FLOAT R2;
    F10 = F0 - F1;
    R0 = PASS R8;
    IF EQ JUMP _wt_pm_next; // exact unmodulated read phase, including neutral CPSB sounds
    // Linear 1024-point sine of the modulator phase.
    F1 = 1024.0;
    F0 = F10 * F1;
    R2 = TRUNC F0;
    F1 = FLOAT R2;
    F0 = F0 - F1;
    R2 = LSHIFT R2 BY 2;
    R1 = _fm_sines;
    R2 = R2 + R1;
    I1 = R2;
    F1 = DM(0,I1);
    F2 = DM(1,I1);
    F2 = F2 - F1;
    F0 = F0 * F2;
    F0 = F0 + F1;
    F1 = F8 * F8;
    F0 = F0 * F1;
    F0 = F0 * F7;
    F0 = F0 + F6;
    R2 = TRUNC F0;
    F1 = FLOAT R2;
    F0 = F0 - F1;
    COMP(F0, F3);
    IF LT F0 = F0 + F4;
    // Adding one to a tiny negative float can round to exactly one.
    COMP(F0, F4);
    IF GE F0 = F0 - F4;
    F6 = F0;
_wt_pm_next:
    DM(I0,M4) = F6;
_wt_pm_last:
    F6 = DM(-2,I0);
    DM(3,I2) = F8;
    DM(_wt_pm_depth_cursor) = F8;
    DM(2,I2) = F9;
    DM(1,I2) = F10;
    F0 = DM(-8,I0); // read phase of record 31
    DM(_wt_read_phase) = F0;
    RTS;

.SECTION/DM seg_dmda;
.GLOBAL _wt_state;
.GLOBAL _wt_enabled;
.VAR _wt_state = 0;
.VAR _wt_enabled = 0;
.VAR _wt_input = 0;
.PKBSS _wt_low_controls, 128;
.PKBSS _wt_controls, 64;
.VAR _wt_limits = 4;
.VAR _wt_limit1 = 700;
.VAR _wt_limit2 = 100;
.VAR _wt_limit3 = 1000;
.VAR _wt_limit4 = 2;
.VAR _wt_limit5 = 2;
.VAR _wt_limit6 = 1;
.VAR _wt_limit7 = 200;
.VAR _wt_limit8 = 127;
.VAR _wt_limit9 = 127;
.VAR _wt_limit10 = 200;
.VAR _wt_limit11 = 200;
.VAR _wt_limit12 = 100;
.VAR _wt_limit13 = 15;
.VAR _wt_limit14 = 200;
.VAR _wt_limit15 = 200;
.VAR _wt_mip_sizes = 512;
.VAR _wt_size1 = 256;
.VAR _wt_size2 = 128;
.VAR _wt_size3 = 64;
.VAR _wt_size4 = 64;
.VAR _wt_size5 = 64;
.VAR _wt_size6 = 64;
.VAR _wt_size7 = 64;
.VAR _wt_mip_offsets = 0;
.VAR _wt_off1 = 513;
.VAR _wt_off2 = 770;
.VAR _wt_off3 = 899;
.VAR _wt_off4 = 964;
.VAR _wt_off5 = 1029;
.VAR _wt_off6 = 1094;
.VAR _wt_off7 = 1094;

.SECTION/PM seg_pmco;
_wt_save_regs:
    DM(_wt_r0) = R0;
    DM(_wt_r1) = R1;
    DM(_wt_r2) = R2;
    DM(_wt_r3) = R3;
    DM(_wt_r4) = R4;
    DM(_wt_r5) = R5;
    DM(_wt_r6) = R6;
    DM(_wt_r7) = R7;
    DM(_wt_r8) = R8;
    DM(_wt_r9) = R9;
    DM(_wt_r10) = R10;
    DM(_wt_r11) = R11;
    DM(_wt_r12) = R12;
    DM(_wt_r13) = R13;
    DM(_wt_r14) = R14;
    DM(_wt_r15) = R15;
    R0 = I0;
    DM(_wt_i0) = R0;
    R0 = I1;
    DM(_wt_i1) = R0;
    R0 = I2;
    DM(_wt_i2) = R0;
    RTS;
_wt_restore_regs:
    R0 = DM(_wt_i0);
    I0 = R0;
    R0 = DM(_wt_i1);
    I1 = R0;
    R0 = DM(_wt_i2);
    I2 = R0;
    R0 = DM(_wt_r0);
    R1 = DM(_wt_r1);
    R2 = DM(_wt_r2);
    R3 = DM(_wt_r3);
    R4 = DM(_wt_r4);
    R5 = DM(_wt_r5);
    R6 = DM(_wt_r6);
    R7 = DM(_wt_r7);
    R8 = DM(_wt_r8);
    R9 = DM(_wt_r9);
    R10 = DM(_wt_r10);
    R11 = DM(_wt_r11);
    R12 = DM(_wt_r12);
    R13 = DM(_wt_r13);
    R14 = DM(_wt_r14);
    R15 = DM(_wt_r15);
    RTS;
.SECTION/DM seg_dmda;
.PKBSS _wt_source_mix, 4;
.PKBSS _wt_table_a, 4;
.PKBSS _wt_table_b, 4;
.PKBSS _wt_table_base_a, 4;
.PKBSS _wt_table_base_b, 4;
.PKBSS _wt_table_mix, 4;
.PKBSS _wt_position, 4;
.PKBSS _wt_slot_address, 4;
.PKBSS _wt_phase, 4;
.PKBSS _wt_increment, 4;
.PKBSS _wt_size_a, 4;
.PKBSS _wt_size_b, 4;
.PKBSS _wt_offset_a, 4;
.PKBSS _wt_offset_b, 4;
.PKBSS _wt_mip_mix, 4;
.PKBSS _wt_aux, 4;
.PKBSS _wt_env, 4;
.PKBSS _wt_env_step, 4;
.PKBSS _wt_env_amount, 4;
.PKBSS _wt_note_position, 4;
.PKBSS _wt_frame_mix, 4;
.PKBSS _wt_frame, 4;
.PKBSS _wt_r0, 4;
.PKBSS _wt_r1, 4;
.PKBSS _wt_r2, 4;
.PKBSS _wt_r3, 4;
.PKBSS _wt_r4, 4;
.PKBSS _wt_r5, 4;
.PKBSS _wt_r6, 4;
.PKBSS _wt_r7, 4;
.PKBSS _wt_r8, 4;
.PKBSS _wt_r9, 4;
.PKBSS _wt_r10, 4;
.PKBSS _wt_r11, 4;
.PKBSS _wt_r12, 4;
.PKBSS _wt_r13, 4;
.PKBSS _wt_r14, 4;
.PKBSS _wt_r15, 4;
.PKBSS _wt_i0, 4;
.PKBSS _wt_i1, 4;
.PKBSS _wt_i2, 4;
// Contiguous transition triplets permit pipelined compute/load instructions.
// Keep in one data section: individual PKBSS symbols may be relocated apart.
.VAR _wt_source_start = 0;
.VAR _wt_source_step = 0;
.VAR _wt_source_cursor = 0;
.VAR _wt_table_start = 0;
.VAR _wt_table_step = 0;
.VAR _wt_table_cursor = 0;
.VAR _wt_scan_start = 0;
.VAR _wt_scan_step = 0;
.VAR _wt_scan_cursor = 0;
.PKBSS _wt_scan_wraps, 4;
.PKBSS _wt_is_new, 4;
.PKBSS _wt_position_step, 4;
.PKBSS _wt_gate, 4;
.PKBSS _wt_expr_starts, 32;
.PKBSS _wt_lane, 4;
.PKBSS _wt_read_phase, 4;
.PKBSS _wt_pm_active, 4;
.PKBSS _wt_pm_aux, 4;
.PKBSS _wt_pm_parent, 4;
.PKBSS _wt_pm_snapshots, 192;
// PM parameters (read by name in _wt_pm_slot and _wt_pm_block).
.VAR _wt_pm_depth = 0;
.VAR _wt_pm_ratio = 0;
.VAR _wt_pm_env_amount = 0;
.VAR _wt_pm_wheel_amount = 0;
.VAR _wt_pm_wheel = 0;
.VAR _wt_pm_base = 0;
.VAR _wt_pm_ratio_step = 0;
.VAR _wt_pm_ratio_bound = 0;
.VAR _wt_pm_render_ratio_bound = 0;
.VAR _wt_pm_key_scale = 0;
.VAR _wt_pm_frequency = 0;
.VAR _wt_pm_depth_cursor = 0;
.PKBSS _wt_blk, 1056; // 33 records of 8 words: 32 samples and one pad
