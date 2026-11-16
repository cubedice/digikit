// One admitted row with up to four instances ("slots"), each bound to one
// track by an explicit slot hint in the transport reset (packet word 10).
// Every entry point loads its slot's working set (the scalar _iv_* symbols
// below), runs the single-instance logic unchanged, then stores the slot.
// Slot k uses registry instance k: state base + k * stride. Transport
// callbacks run under the saved early-frame scalar context. No module callback
// is a V1 voice. Events are copied once, consumed once by process, then removed.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_reset;
.GLOBAL _instrument_configure;
.GLOBAL _instrument_events;
.GLOBAL _instrument_carrier_prepare;
.GLOBAL _iv_voice_render;
.GLOBAL _iv_reset_row;
.GLOBAL _iv_slot_load;
.GLOBAL _iv_slot_store;
.EXTERN _ir_rows;
.EXTERN _ir_process_rows;
.EXTERN _ir_process_flags;
.EXTERN _ir_process_protected;
.EXTERN _ir_normalize_packet;
.EXTERN _ir_reset_instance;
.EXTERN _iv_reset_protected;
.EXTERN _mi_epoch;
.EXTERN _instrument_session;
.EXTERN _ir_lifecycle_ready;
.EXTERN _mi_velocity_prepare;
.EXTERN _mi_carrier_rendered;
.EXTERN _mi_envelope_records;
.EXTERN _mi_native_owner;
.EXTERN _mi_post_ptr;
.EXTERN _mi_carrier_stamp;
.EXTERN _iv_amp_attack_table;
.EXTERN _iv_amp_decay_table;
.EXTERN _iv_amp_hold_table;
.EXTERN _pattern_position_info;
.EXTERN _pattern_clock_info;
.EXTERN _iv_modulate;
.EXTERN _iv_mod_status;
.EXTERN _iv_mod_ready;

// This adapter is absent from ordinary images. Linking it is the trusted
// instrument capability; binding the session must precede lifecycle work.
_instrument_carrier_prepare:
    R0 = 0;
    DM(_instrument_ui_status) = R0;
    I0 = _ir_rows;
    R0 = DM(0, I0);
    DM(_instrument_play_status) = R0;
    R0 = DM(2, I0);
    DM(_instrument_play_status + 4) = R0;
    R0 = DM(_instrument_session);
    R0 = PASS R0;
    IF EQ JUMP _iv_quiet_all;
    R0 = 1;
    DM(_ir_lifecycle_ready) = R0;
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_quiet_all;
    I0 = _ir_process_rows;
    R0 = DM(2, I0);
    I1 = _instrument_voice_rows;
    R1 = _iv_voice_render;
    DM(1, I1) = R1;
    DM(0, I1) = R0; // publish the admitted type last; never shadow a stock type
    R0 = 0;
    DM(_iv_loop) = R0;
_iv_prepare_slot:
    R4 = DM(_iv_loop);
    CALL _iv_slot_bound;
    R0 = PASS R0;
    IF EQ JUMP _iv_prepare_next;
    R4 = DM(_iv_loop);
    CALL _iv_slot_load;
    CALL _iv_run_slot;
    CALL _iv_slot_store;
_iv_prepare_next:
    R0 = DM(_iv_loop);
    R0 = R0 + 1;
    DM(_iv_loop) = R0;
    R1 = DM(_iv_slot_count);
    COMP(R0, R1);
    IF LT JUMP _iv_prepare_slot;
    RTS;

// Session loss or a lost row identity silences and unconfigures every slot.
_iv_quiet_all:
    R0 = 0;
    DM(_iv_loop) = R0;
_iv_quiet_slot:
    R4 = DM(_iv_loop);
    CALL _iv_slot_bound;
    R0 = PASS R0;
    IF EQ JUMP _iv_quiet_next;
    R4 = DM(_iv_loop);
    CALL _iv_slot_load;
    CALL _iv_quiet;
    CALL _iv_slot_store;
_iv_quiet_next:
    R0 = DM(_iv_loop);
    R0 = R0 + 1;
    DM(_iv_loop) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _iv_quiet_slot;
    RTS;

// One slot's block. Working set loaded; returns through RTS on every path.
_iv_run_slot:
    R0 = 0;
    DM(_iv_post_requested) = R0;
    DM(_iv_native_valid) = R0;
    DM(_iv_ui_sink) = R0;
    R0 = DM(_iv_generation);
    R0 = PASS R0;
    IF EQ JUMP _iv_quiet;
    CALL _iv_position;
    I0 = _iv_input;
    R0 = DM(_iv_track); // status word1 bits0..3: the bound track
    DM(2, I0) = R0;
    R0 = DM(_ir_process_flags);
    R1 = FEXT R0 BY 0:1;
    R1 = PASS R1;
    IF EQ JUMP _iv_no_status;
    R0 = 5;
    DM(0, I0) = R0;
    R0 = 96;
    DM(1, I0) = R0;
    R0 = _iv_ui_sink;
    DM(22, I0) = R0;
    R0 = 13;
    DM(23, I0) = R0;
    R0 = DM(12, I0);
    R1 = DM(_iv_timing_meta);
    R0 = R0 OR R1;
    DM(12, I0) = R0;
    R0 = DM(_ir_process_flags);
    R0 = FEXT R0 BY 1:1;
    R0 = PASS R0;
    IF EQ JUMP _iv_no_status;
    R0 = 6;
    DM(0, I0) = R0;
    R0 = 128;
    DM(1, I0) = R0;
    CALL _iv_amp_snapshot;
    I0 = _iv_input;
    R0 = DM(_ir_process_flags);
    R0 = FEXT R0 BY 2:1;
    R0 = PASS R0;
    IF EQ JUMP _iv_no_status;
    DM(_iv_post_requested) = R0;
    R0 = 7;
    DM(0, I0) = R0;
    R0 = 136;
    DM(1, I0) = R0;
    R0 = DM(_iv_post_buf);
    DM(32, I0) = R0;
    R0 = 32;
    DM(33, I0) = R0;
    R0 = DM(_iv_post_buf);
    I1 = R0;
    R0 = 0x7fc00000;
    LCNTR = 32, DO _iv_post_clear UNTIL LCE;
_iv_post_clear:
    DM(I1, M6) = R0;
_iv_no_status:
    R0 = DM(_iv_generation);
    DM(3, I0) = R0;
    R0 = _iv_output;
    DM(5, I0) = R0;
    R0 = _iv_events;
    DM(7, I0) = R0;
    R4 = _iv_controls;
    CALL _iv_lock_overlay;
    R4 = PASS R0;
    R8 = DM(_iv_control_count);
    CALL _iv_modulate;
    I0 = _iv_input;
    DM(9, I0) = R0;
    R0 = DM(_iv_control_count);
    DM(10, I0) = R0;
    R0 = 1;
    // A reset tail renders before configuration. No new events are admitted.
    R1 = DM(_iv_revision);
    R1 = PASS R1;
    IF EQ R1 = PASS R0;
    DM(11, I0) = R1;
    R4 = 0;
    R5 = DM(_iv_slot); // registry instance ordinal: this slot's own state
    R8 = _iv_input;
    CALL _ir_process_protected;
    DM(_iv_result) = R0;
    I0 = _iv_input;
    R1 = 0;
    DM(8, I0) = R1;
    R0 = PASS R0;
    IF LT JUMP _iv_quiet;
    R4 = _iv_controls;
    R8 = _iv_input;
    CALL _iv_mod_status;
    // Only the focused slot (the selected track on the CF) reaches the link.
    R0 = DM(_iv_slot);
    R1 = DM(_iv_focus);
    COMP(R0, R1);
    IF NE JUMP _iv_status_done;
    I0 = _iv_ui_sink;
    I1 = _instrument_ui_status;
    R2 = 13;
_iv_status_copy:
    R0 = DM(I0, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _iv_status_copy;
_iv_status_done:
    // Every block rewrites this track's owner entries: a value when valid, else 0.
    R1 = DM(_iv_post_requested);
    R1 = PASS R1;
    R4 = 0;
    IF EQ JUMP _iv_post_put;
    CALL _iv_post_normalize;
    R0 = PASS R0;
    IF LT JUMP _iv_post_fault;
    R4 = 0;
    R0 = DM(_iv_native_valid);
    R0 = PASS R0;
    IF EQ JUMP _iv_post_put;
    R4 = DM(_iv_post_buf);
_iv_post_put:
    R8 = _mi_post_ptr;
    CALL _iv_owner_put;
    // The shared AMP may be skipped only after this native block completed.
    R4 = 0;
    R0 = DM(_iv_native_valid);
    R0 = PASS R0;
    IF EQ JUMP _iv_native_put;
    R4 = DM(_mi_carrier_stamp);
_iv_native_put:
    R8 = _mi_native_owner;
    CALL _iv_owner_put;
    R0 = DM(_iv_result);
    R1 = 1;
    R0 = R1 - R0; // ACTIVE=0, QUIET=1
    DM(_iv_active) = R0;
    R10 = DM(_iv_revision);
    R10 = R10 OR R0;
    R4 = DM(_iv_track);
    CALL _mi_velocity_prepare;
    RTS;
_iv_quiet:
    R0 = 0;
    DM(_iv_mod_ready) = R0;
    CALL _iv_owner_clear;
    R0 = 0;
    DM(_iv_active) = R0;
    DM(_iv_revision) = R0;
    I0 = _iv_input;
    DM(8, I0) = R0;
    DM(18, I0) = R0;
    I0 = _iv_output;
    R1 = 32;
_iv_clear_output:
    DM(I0, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _iv_clear_output;
    RTS;

_iv_post_fault:
    R0 = -1;
    DM(_iv_result) = R0;
    I0 = _ir_rows;
    DM(12, I0) = R0;
    R0 = 5;
    DM(0, I0) = R0;
    JUMP _iv_quiet;

// Per-track owner tables (core/midi-events carrier.s), cleared every frame.
// R4=value, R8=table. Writes table[_iv_track]; clobbers R0/R1/I0.
_iv_owner_put:
    R0 = DM(_iv_track);
    R1 = LSHIFT R0 BY -4; // unsigned: tracks 0..15 only (unbound is 0xffffffff)
    R1 = PASS R1;
    IF NE RTS;
    R0 = LSHIFT R0 BY 2;
    R0 = R0 + R8;
    I0 = R0;
    DM(0, I0) = R4;
    RTS;
_iv_owner_clear:
    R4 = 0;
    R8 = _mi_native_owner;
    CALL _iv_owner_put;
    R4 = 0;
    R8 = _mi_post_ptr;
    JUMP _iv_owner_put;

// ---- slots ----
// Slot storage: 8 scalars, then input(34) output(32) events(48) controls(32):
// 154 words in a 160-word (640-byte) stride. The bound track lives outside in
// _iv_slot_tracks (0xffffffff: unbound); slot 0 starts bound to track 0.
// R4=slot. R0=1 when bound and below the admitted count, else 0.
_iv_slot_bound:
    R0 = 0;
    R1 = DM(_iv_slot_count);
    COMP(R4, R1);
    IF GE RTS;
    R1 = LSHIFT R4 BY 2;
    R2 = _iv_slot_tracks;
    R1 = R1 + R2;
    I0 = R1;
    R1 = DM(0, I0);
    R2 = LSHIFT R1 BY -4; // unsigned: an unbound slot holds 0xffffffff
    R2 = PASS R2;
    IF NE RTS;
    R0 = 1;
    RTS;
// R4=track. R0=slot bound to it, or -1. Clobbers R1/I0.
_iv_find_slot:
    R0 = 0;
    I0 = _iv_slot_tracks;
_iv_find_next:
    R1 = DM(_iv_slot_count);
    COMP(R0, R1);
    IF GE JUMP _iv_find_none;
    R1 = DM(I0, M6);
    COMP(R1, R4);
    IF EQ RTS;
    R0 = R0 + 1;
    JUMP _iv_find_next;
_iv_find_none:
    R0 = -1;
    RTS;
// R4=slot -> I0 slot storage base. Clobbers R0/R1.
_iv_slot_base:
    R0 = 640;
    R0 = R4 * R0 (SSI);
    R1 = _iv_slots;
    R0 = R0 + R1;
    I0 = R0;
    RTS;
// R4=slot. Working set <- slot. Clobbers R0..R2/I0..I2.
_iv_slot_load:
    DM(_iv_slot) = R4;
    R0 = LSHIFT R4 BY 7; // 32 gains per slot
    R1 = _iv_post_slots;
    R0 = R0 + R1;
    DM(_iv_post_buf) = R0;
    R0 = LSHIFT R4 BY 2;
    R1 = _iv_slot_tracks;
    R0 = R0 + R1;
    I0 = R0;
    R0 = DM(0, I0);
    DM(_iv_track) = R0;
    CALL _iv_slot_base;
    R0 = DM(0, I0);
    DM(_iv_generation) = R0;
    R0 = DM(1, I0);
    DM(_iv_revision) = R0;
    R0 = DM(2, I0);
    DM(_iv_active) = R0;
    R0 = DM(3, I0);
    DM(_iv_result) = R0;
    R0 = DM(4, I0);
    DM(_iv_post_requested) = R0;
    R0 = DM(5, I0);
    DM(_iv_native_valid) = R0;
    R0 = DM(6, I0);
    DM(_iv_mod_ready) = R0;
    R0 = DM(7, I0);
    DM(_iv_control_count) = R0;
    I1 = I0;
    MODIFY(I1, 8) (NW);
    I2 = _iv_input;
    R2 = 34;
    CALL _iv_copy;
    I2 = _iv_output;
    R2 = 32;
    CALL _iv_copy;
    I2 = _iv_events;
    R2 = 48;
    CALL _iv_copy;
    I2 = _iv_controls;
    R2 = 32;
    CALL _iv_copy;
    // A never-configured slot starts from the legacy one-word bank (count 1,
    // value 1), exactly as the single-instance adapter's initial data did.
    R0 = DM(_iv_control_count);
    R0 = PASS R0;
    IF NE RTS;
    R0 = 1;
    DM(_iv_control_count) = R0;
    DM(_iv_controls) = R0;
    RTS;
// Slot <- working set (slot from the last load). Clobbers R0..R4/I0..I2.
_iv_slot_store:
    R4 = DM(_iv_slot);
    R0 = LSHIFT R4 BY 2;
    R1 = _iv_slot_tracks;
    R0 = R0 + R1;
    I0 = R0;
    R0 = DM(_iv_track);
    DM(0, I0) = R0;
    CALL _iv_slot_base;
    R0 = DM(_iv_generation);
    DM(0, I0) = R0;
    R0 = DM(_iv_revision);
    DM(1, I0) = R0;
    R0 = DM(_iv_active);
    DM(2, I0) = R0;
    R0 = DM(_iv_result);
    DM(3, I0) = R0;
    R0 = DM(_iv_post_requested);
    DM(4, I0) = R0;
    R0 = DM(_iv_native_valid);
    DM(5, I0) = R0;
    R0 = DM(_iv_mod_ready);
    DM(6, I0) = R0;
    R0 = DM(_iv_control_count);
    DM(7, I0) = R0;
    I2 = I0;
    MODIFY(I2, 8) (NW);
    I1 = _iv_input;
    R2 = 34;
    CALL _iv_copy_out;
    I1 = _iv_output;
    R2 = 32;
    CALL _iv_copy_out;
    I1 = _iv_events;
    R2 = 48;
    CALL _iv_copy_out;
    I1 = _iv_controls;
    R2 = 32;
    JUMP _iv_copy_out;
// Copy R2 words from I1 to I2; both advance. Clobbers R0/R2.
_iv_copy:
_iv_copy_out:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _iv_copy;
    RTS;

// V7 preserves the callback's shaped PCM, then moves its common gain past
// stock bit reduction. Validate the entire borrowed output before publication.
_iv_post_normalize:
    R0 = DM(_iv_post_buf);
    I1 = R0;
    R2 = 32;
_iv_post_validate:
    R0 = DM(I1, M6);
    R1 = 0x7fffffff;
    R1 = R0 AND R1;
    IF EQ JUMP _iv_post_gain_ok; // Both signs of zero mean silence.
    R0 = PASS R0;
    IF LT JUMP _iv_post_bad;
    R1 = 0x3f800000;
    COMP(R0, R1);
    IF GT JUMP _iv_post_bad;
_iv_post_gain_ok:
    R2 = R2 - 1;
    IF NE JUMP _iv_post_validate;
    R0 = DM(_iv_native_valid);
    R0 = PASS R0;
    IF EQ JUMP _iv_post_ok;
    I0 = _iv_output;
    R0 = DM(_iv_post_buf);
    I1 = R0;
    R6 = 32;
_iv_post_sample:
    R0 = DM(I0, M5);
    R3 = 0x7fffffff;
    R4 = R0 AND R3;
    R5 = 0x41000000;
    COMP(R4, R5);
    IF GT JUMP _iv_post_bad;
    F1 = DM(I1, M6);
    R3 = R1 AND R3;
    IF EQ JUMP _iv_post_zero;
    R3 = 0x00800000;
    COMP(R1, R3);
    IF LT JUMP _iv_post_tiny_gain;
    COMP(R4, R3);
    IF GE JUMP _iv_post_reciprocal;
    F2 = 16777216.0;
    F1 = F1 * F2;
    JUMP _iv_post_scale_pcm;
_iv_post_tiny_gain:
    // Float inputs flush denormals. Rebuild their mantissas at 2^24 scale
    // using integer conversion; scale an ordinary dividend by the same amount.
    F1 = FLOAT R1;
    R2 = 0x01000000; // 2^-125 = 2^-149 * 2^24
    F1 = F1 * F2;
_iv_post_scale_pcm:
    COMP(R4, R3);
    IF LT JUMP _iv_post_tiny_pcm;
    F2 = 16777216.0;
    F0 = F0 * F2;
    JUMP _iv_post_reciprocal;
_iv_post_tiny_pcm:
    R5 = 0x80000000;
    R5 = R0 AND R5;
    F0 = FLOAT R4;
    R2 = 0x01000000;
    F0 = F0 * F2;
    R0 = R0 OR R5;
_iv_post_reciprocal:
    F2 = RECIPS F1;
    F3 = 2.0;
    F4 = F1 * F2;
    F4 = F3 - F4;
    F2 = F2 * F4;
    F4 = F1 * F2;
    F4 = F3 - F4;
    F2 = F2 * F4;
    F0 = F0 * F2;
    R3 = 0x7fffffff;
    R4 = R0 AND R3;
    R5 = 0x41000000;
    COMP(R4, R5);
    IF GT JUMP _iv_post_bad;
    JUMP _iv_post_store;
_iv_post_zero:
    R0 = 0;
_iv_post_store:
    DM(I0, M6) = R0;
    R6 = R6 - 1;
    IF NE JUMP _iv_post_sample;
_iv_post_ok:
    R0 = 1;
    RTS;
_iv_post_bad:
    R0 = -1;
    RTS;

// Stock configuration runs later in the dispatcher. Its previous completed
// record is stable here, so all eight V6 words describe one native AMP shape.
// Never write the stock record: parameter locks and other machines still own it.
_iv_amp_snapshot:
    I1 = _iv_input;
    R0 = 0;
    DM(24, I1) = R0;
    DM(31, I1) = R0;
    DM(_iv_native_valid) = R0;
    R0 = DM(_mi_envelope_records);
    R0 = PASS R0;
    IF EQ RTS;
    R1 = DM(_iv_track); // the even voice record of this slot's track
    R2 = LSHIFT R1 BY -4;
    R2 = PASS R2;
    IF NE RTS;
    R2 = 88;
    R1 = R1 * R2 (SSI);
    R0 = R0 + R1;
    I0 = R0;
    R4 = DM(6, I0);
    R1 = 1;
    COMP(R4, R1);
    IF EQ JUMP _iv_amp_mode_ok;
    R1 = 2;
    COMP(R4, R1);
    IF NE RTS;
_iv_amp_mode_ok:
    R0 = DM(0, I0);
    R1 = LSHIFT R0 BY -7;
    R1 = PASS R1;
    IF NE RTS;
    R0 = LSHIFT R0 BY 2;
    R1 = _iv_amp_attack_table;
    R0 = R0 + R1;
    I2 = R0;
    R0 = DM(0, I2);
    F0 = -F0;
    DM(25, I1) = R0;
    R0 = DM(1, I0);
    R1 = LSHIFT R0 BY -7;
    R1 = PASS R1;
    IF NE RTS;
    R2 = 127;
    COMP(R0, R2);
    IF EQ JUMP _iv_amp_hold_forever;
    R0 = LSHIFT R0 BY 2;
    R1 = _iv_amp_hold_table;
    R0 = R0 + R1;
    I2 = R0;
    R0 = DM(0, I2);
    JUMP _iv_amp_hold_store;
_iv_amp_hold_forever:
    R0 = 0xffffffff;
_iv_amp_hold_store:
    DM(26, I1) = R0;
    R0 = DM(2, I0);
    R1 = LSHIFT R0 BY -7;
    R1 = PASS R1;
    IF NE RTS;
    R0 = LSHIFT R0 BY 2;
    R1 = _iv_amp_decay_table;
    R0 = R0 + R1;
    I2 = R0;
    R0 = DM(0, I2);
    F0 = -F0;
    DM(27, I1) = R0;
    R0 = DM(3, I0);
    R0 = PASS R0;
    IF LT RTS;
    R1 = 0x3f800000;
    COMP(R0, R1);
    IF GT RTS;
    DM(28, I1) = R0;
    R0 = DM(4, I0);
    R1 = LSHIFT R0 BY -7;
    R1 = PASS R1;
    IF NE RTS;
    R0 = LSHIFT R0 BY 2;
    R1 = _iv_amp_decay_table;
    R0 = R0 + R1;
    I2 = R0;
    R0 = DM(0, I2);
    F0 = -F0;
    DM(29, I1) = R0;
    R0 = DM(5, I0);
    R1 = 0;
    R2 = 1;
    R0 = PASS R0;
    IF NE R1 = PASS R2;
    DM(30, I1) = R1;
    DM(24, I1) = R4;
    DM(_iv_native_valid) = R2;
    RTS;

// Borrow a coherent position snapshot only at the saved early-block boundary.
// Reject wrong ABI or invalid wire state. A missed frame with a new accepted
// snapshot may update BPM, but cannot supply grid/phase continuity. The receive
// counter remains a block clock even when transport or position is invalid.
_iv_position:
    R0 = 0;
    DM(_iv_timing_meta) = R0;
    I0 = _iv_input;
    R0 = 3;
    DM(0, I0) = R0;
    R0 = 80;
    DM(1, I0) = R0;
    R0 = 0;
    DM(20, I0) = R0;
    DM(21, I0) = R0;
    I1 = _pattern_position_info;
    R0 = DM(16, I1);
    DM(4, I0) = R0;
    R0 = 0;
    DM(12, I0) = R0;
    DM(13, I0) = R0;
    DM(14, I0) = R0;
    DM(15, I0) = R0;
    DM(16, I0) = R0;
    DM(19, I0) = R0;
    R0 = DM(0, I1);
    R1 = 0x50504f53;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(1, I1);
    R1 = 2;
    COMP(R0, R1);
    IF NE RTS;
    CALL _iv_tempo_snapshot;
    I1 = _pattern_position_info;
    R0 = DM(19, I1);
    R1 = 1;
    COMP(R0, R1);
    IF GT RTS;
    R0 = DM(_iv_timing_meta);
    R1 = 0x100; // Coherent grid provider, independent of running/valid.
    R0 = R0 OR R1;
    DM(_iv_timing_meta) = R0;
    R0 = DM(14, I1); // Firmware transport state, not boundary validity.
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _iv_transport_known;
    R0 = DM(_iv_timing_meta);
    R1 = 0x400; // Explicitly stopped.
    R0 = R0 OR R1;
    DM(_iv_timing_meta) = R0;
_iv_transport_known:
    R0 = DM(5, I1);
    R1 = 3;
    R0 = R0 AND R1;
    DM(12, I0) = R0;
    R0 = DM(4, I1);
    DM(13, I0) = R0;
    R0 = DM(3, I1);
    DM(14, I0) = R0;
    R0 = DM(7, I1);
    DM(15, I0) = R0;
    R0 = DM(8, I1);
    DM(16, I0) = R0;
    R0 = DM(6, I1);
    DM(19, I0) = R0;
    I1 = _pattern_clock_info;
    R0 = DM(0, I1);
    R1 = 0x50434c4b;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(1, I1);
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _iv_clock_provider;
    R1 = 2;
    COMP(R0, R1);
    IF NE RTS;
_iv_clock_provider:
    R0 = DM(_iv_timing_meta);
    R1 = 0x200;
    R0 = R0 OR R1;
    DM(_iv_timing_meta) = R0;
    R0 = DM(4, I1);
    R1 = 3;
    R0 = R0 AND R1;
    COMP(R0, R1);
    IF EQ JUMP _iv_phase_fresh;
    R0 = 0;
    DM(12, I0) = R0;
    RTS;
_iv_phase_fresh:
    R0 = DM(2, I1);
    DM(20, I0) = R0;
    R0 = DM(3, I1);
    DM(21, I0) = R0;
    R0 = 4;
    DM(0, I0) = R0;
    R0 = 88;
    DM(1, I0) = R0;
    RTS;

// The receiver publishes PCLK alongside each authenticated position snapshot.
// Its version2 selected BPM is independent of phase health and transmit gaps.
// Publish only tempo here; continuity checks above own all transport flags.
_iv_tempo_snapshot:
    I1 = _pattern_clock_info;
    R0 = DM(0, I1);
    R1 = 0x50434c4b;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(1, I1);
    R1 = 2;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(4, I1);
    R0 = LSHIFT R0 BY -16;
    R1 = 3600;
    COMP(R0, R1);
    IF LT RTS;
    R1 = 36000;
    COMP(R0, R1);
    IF GT RTS;
    R0 = LSHIFT R0 BY 16;
    R1 = 0x800;
    R0 = R0 OR R1;
    DM(_iv_timing_meta) = R0;
    RTS;

.PKBSS _iv_timing_meta, 4;

// A receiver reset can precede CREATE. Such a reset establishes only transport
// identity; no callback or state pointer is used until the admitted row is ready.
// Word 10 is an optional slot hint 1..4: the CF binds that slot to the track
// (after resetting the slot's previous track). Without a hint the slot already
// bound to the track is reset; a new epoch resets every bound slot. Tracks
// without a slot are acknowledged as inactive.
_instrument_reset:
    DM(_iv_packet) = R4;
    CALL _iv_slot_limit;
    R0 = DM(_iv_packet);
    I0 = R0;
    R0 = DM(10, I0);
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_lookup;
    R1 = DM(_iv_slot_count);
    COMP(R0, R1);
    IF GT JUMP _iv_reset_inactive; // a hint beyond the admitted instances binds nothing
    R4 = R0 - 1;
    R1 = DM(8, I0);
    // A track has one slot: unbind any other slot that still names it.
    R2 = 0;
    I2 = _iv_slot_tracks;
_iv_reset_unbind:
    R3 = DM(0, I2);
    COMP(R3, R1);
    IF NE JUMP _iv_reset_unbind_next;
    COMP(R2, R4);
    IF EQ JUMP _iv_reset_unbind_next;
    R3 = 0xffffffff;
    DM(0, I2) = R3;
_iv_reset_unbind_next:
    MODIFY(I2, 1) (NW);
    R2 = R2 + 1;
    R3 = 4;
    COMP(R2, R3);
    IF LT JUMP _iv_reset_unbind;
    R2 = LSHIFT R4 BY 2;
    R3 = _iv_slot_tracks;
    R2 = R2 + R3;
    I1 = R2;
    DM(0, I1) = R1;
    JUMP _iv_reset_target;
_iv_reset_lookup:
    R4 = DM(8, I0);
    CALL _iv_find_slot;
    R4 = PASS R0;
_iv_reset_target:
    DM(_iv_reset_slot) = R4; // -1 when no slot belongs to this track
    R0 = DM(_iv_packet);
    I0 = R0;
    R0 = DM(2, I0);
    R1 = DM(_mi_epoch);
    COMP(R0, R1);
    IF NE JUMP _iv_reset_epoch; // a new epoch silences every instance
    R4 = DM(_iv_reset_slot);
    R4 = PASS R4;
    IF LT JUMP _iv_reset_inactive;
    CALL _iv_slot_load;
    CALL _iv_reset_owned;
    DM(_iv_call_result) = R0;
    CALL _iv_slot_store;
    R0 = DM(_iv_call_result);
    RTS;
_iv_reset_inactive:
    R0 = 1;
    RTS; // inactive tracks still acknowledge the transport's all-track reset
_iv_reset_epoch:
    R0 = 1;
    DM(_iv_call_result) = R0;
    R0 = 0;
    DM(_iv_loop) = R0;
_iv_reset_epoch_slot:
    R4 = DM(_iv_loop);
    CALL _iv_slot_bound;
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_epoch_next;
    R4 = DM(_iv_loop);
    CALL _iv_slot_load;
    CALL _iv_reset_owned;
    R0 = PASS R0;
    IF NE JUMP _iv_reset_epoch_store;
    DM(_iv_call_result) = R0;
_iv_reset_epoch_store:
    CALL _iv_slot_store;
_iv_reset_epoch_next:
    R0 = DM(_iv_loop);
    R0 = R0 + 1;
    DM(_iv_loop) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _iv_reset_epoch_slot;
    R0 = DM(_iv_call_result);
    RTS;
// One slot, working set loaded. R0=1 applied, 0 rejected.
_iv_reset_owned:
    CALL _iv_lock_clear;
    R0 = 0;
    DM(_iv_mod_ready) = R0;
    CALL _iv_owner_clear;
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_identity;
    CALL _iv_reset_protected;
    R0 = PASS R0;
    IF NE JUMP _iv_no;
_iv_reset_identity:
    R0 = DM(_iv_packet);
    I0 = R0;
    R0 = DM(8, I0);
    R1 = DM(_iv_track);
    COMP(R0, R1);
    IF NE JUMP _iv_reset_unconfigured;
    R0 = DM(9, I0);
    DM(_iv_generation) = R0;
_iv_reset_unconfigured:
    R0 = 0;
    DM(_iv_revision) = R0;
    I0 = _iv_input;
    DM(8, I0) = R0;
    R0 = DM(_iv_track); // normalized events match the bound track from now on
    DM(2, I0) = R0;
    R0 = 1;
    RTS;
// _iv_slot_count = the row's instance count clamped to 1..4. Clobbers R0/R1/I0.
_iv_slot_limit:
    I0 = _ir_rows;
    R0 = DM(3, I0);
    R1 = 1;
    COMP(R0, R1);
    IF LT R0 = PASS R1;
    R1 = 4;
    COMP(R0, R1);
    IF GT R0 = PASS R1;
    DM(_iv_slot_count) = R0;
    RTS;

_instrument_configure:
    DM(_iv_values) = R4;
    DM(_iv_metadata) = R8;
    I0 = R8;
    R4 = DM(0, I0); // configuration names its track
    CALL _iv_find_slot;
    R4 = PASS R0;
    R4 = PASS R4;
    IF LT JUMP _iv_no;
    CALL _iv_slot_load;
    CALL _iv_configure_slot;
    DM(_iv_call_result) = R0;
    CALL _iv_slot_store;
    R0 = DM(_iv_call_result);
    RTS;
_iv_configure_slot:
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    // A replacement's bounded release tail must finish before new ownership.
    R0 = DM(_iv_revision);
    R0 = PASS R0;
    IF NE JUMP _iv_live_config;
    R0 = DM(_iv_active);
    R0 = PASS R0;
    IF NE JUMP _iv_no;
_iv_live_config:
    R0 = DM(_iv_metadata);
    I0 = R0;
    R0 = DM(0, I0);
    R1 = DM(_iv_track);
    COMP(R0, R1);
    IF NE JUMP _iv_no; // configuration belongs to the bound track only
    R0 = DM(1, I0);
    R1 = DM(_iv_generation);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(4, I0);
    R1 = 4;
    COMP(R0, R1);
    IF EQ JUMP _iv_legacy_config;
    R1 = 128;
    COMP(R0, R1);
    IF EQ JUMP _iv_six_config;
    R1 = 64;
    COMP(R0, R1);
    IF EQ JUMP _iv_five_config;
    R1 = 40;
    COMP(R0, R1);
    IF EQ JUMP _iv_four_config;
    R1 = 24;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(5, I0);
    R1 = 0x43505333;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(1, I1);
    R0 = LSHIFT R0 BY -1;
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    MODIFY(I1, 2) (NW);
    R2 = 4;
_iv_config_validate:
    R0 = DM(I1, M6);
    R0 = LSHIFT R0 BY -7;
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    R2 = R2 - 1;
    IF NE JUMP _iv_config_validate;
    R2 = 6;
    JUMP _iv_config_copy;
_iv_six_config:
    R0 = DM(5, I0);
    R1 = 0x43505342;
    COMP(R0, R1);
    IF EQ JUMP _iv_eleven_config;
    R1 = 0x43505341;
    COMP(R0, R1);
    IF EQ JUMP _iv_ten_config;
    R1 = 0x43505339;
    COMP(R0, R1);
    IF EQ JUMP _iv_nine_config;
    R1 = 0x43505338;
    COMP(R0, R1);
    IF EQ JUMP _iv_eight_config;
    R1 = 0x43505337;
    COMP(R0, R1);
    IF EQ JUMP _iv_seven_config;
    R1 = 0x43505336;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = FEXT R0 BY 0:8;
    R2 = 6;
    COMP(R1, R2);
    IF NE JUMP _iv_no;
    R0 = LSHIFT R0 BY -8;
    R1 = 2;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    JUMP _iv_effective_controls;
_iv_seven_config:
    R2 = 7;
    JUMP _iv_native_config;
_iv_eight_config:
    R2 = 8;
    JUMP _iv_native_config;
_iv_nine_config:
    R2 = 9;
    JUMP _iv_native_config;
_iv_ten_config:
    R2 = 10;
    JUMP _iv_native_config;
_iv_eleven_config:
    R2 = 11;
_iv_native_config:
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = FEXT R0 BY 0:8;
    COMP(R1, R2);
    IF NE JUMP _iv_no;
    R1 = FEXT R0 BY 10:5; // bit 15 is the CF focus flag, removed below
    R1 = PASS R1;
    IF NE JUMP _iv_no;
    R1 = LSHIFT R0 BY -16;
    R2 = 0x400;
    COMP(R1, R2);
    IF LT JUMP _iv_no;
    R2 = 0x7c00;
    COMP(R1, R2);
    IF GT JUMP _iv_no;
    R0 = FEXT R0 BY 8:2;
    R1 = 2;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(0, I1);
    R0 = FEXT R0 BY 0:8;
    R1 = 9;
    COMP(R0, R1);
    IF GE JUMP _iv_wavetable_controls;
_iv_effective_controls:
    R0 = DM(16, I1);
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R1 = 11;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(17, I1);
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R1 = 3;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(20, I1);
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R1 = 500;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(28, I1);
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R1 = 1;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(29, I1);
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R1 = 0;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(18, I1);
    R1 = DM(2, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(19, I1);
    R1 = DM(3, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(21, I1);
    R1 = DM(6, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(22, I1);
    R1 = DM(8, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(23, I1);
    R1 = DM(10, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(24, I1);
    R1 = DM(11, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(25, I1);
    R1 = DM(9, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(26, I1);
    R1 = DM(12, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(27, I1);
    R1 = DM(14, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(30, I1);
    R1 = DM(4, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(31, I1);
    R1 = DM(5, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(20, I1);
    R1 = LSHIFT R0 BY 1;
    R0 = R0 + R1;
    R0 = LSHIFT R0 BY -1;
    R1 = DM(15, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    JUMP _iv_five_body;
// CPS9/CPSA/CPSB preserve the 128-byte transport bank. Presentation words contain
// the original control in low16 and a source-specific control in high16.
// CPSA adds family 3 (FM); CPS9 retains its original family 0..2 contract.
// CPSB uses the last four high halves for remembered wavetable FM controls.
// Validate every field and duplicate before publishing any part of a revision.
_iv_wavetable_controls:
    R0 = DM(0, I1);
    R0 = FEXT R0 BY 0:8;
    R1 = 9;
    COMP(R0, R1);
    IF NE JUMP _iv_extended_controls;
    R0 = DM(1, I1);
    R1 = 2;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
_iv_extended_controls:
    // MIDI callbacks may clobber I0..I2 only. Keep the consumer's I3+ context.
    // The outer frame pins M6=1 and L0..L2=0, but leaves the other DAGs in
    // stock firmware context. The four advancing DAGs I3..I6 need linear
    // lengths, or a table index can wrap into unrelated/unknown memory.
    // Never borrow I7: an interrupt saves its context through the live stack
    // pointer. I0 is scratch; reload the metadata pointer before publication.
    R0 = I3;
    DM(_iv_wt_i3) = R0;
    R0 = I4;
    DM(_iv_wt_i4) = R0;
    R0 = I5;
    DM(_iv_wt_i5) = R0;
    R0 = I6;
    DM(_iv_wt_i6) = R0;
    R0 = L3;
    DM(_iv_wt_l3) = R0;
    R0 = L4;
    DM(_iv_wt_l4) = R0;
    R0 = L5;
    DM(_iv_wt_l5) = R0;
    R0 = L6;
    DM(_iv_wt_l6) = R0;
    L3 = 0;
    L4 = 0;
    L5 = 0;
    L6 = 0;
    I2 = _iv_wt_scalar_min;
    I3 = _iv_wt_scalar_max;
    MODIFY(I1, 1) (NW);
    R6 = 15;
_iv_wt_scalar:
    R0 = DM(I1, M6);
    R1 = DM(I2, M6);
    COMP(R0, R1);
    IF LT JUMP _iv_wt_reject;
    R1 = DM(I3, M6);
    COMP(R0, R1);
    IF GT JUMP _iv_wt_reject;
    R6 = R6 - 1;
    IF NE JUMP _iv_wt_scalar;
    I2 = _iv_wt_low_min;
    I3 = _iv_wt_low_max;
    I4 = _iv_wt_high_min;
    I5 = _iv_wt_high_max;
    R0 = DM(_iv_values);
    I0 = R0;
    R0 = DM(1, I0);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _iv_extension_ranges;
    I4 = _iv_fm_high_min;
    I5 = _iv_fm_high_max;
_iv_extension_ranges:
    R0 = DM(_iv_values);
    I0 = R0;
    R0 = DM(0,I0);
    R0 = FEXT R0 BY 0:8;
    DM(_iv_extension_version) = R0;
    I6 = _iv_wt_duplicate;
    R6 = 16;
_iv_wt_presentation:
    R0 = 4;
    COMP(R6,R0);
    IF NE JUMP _iv_extension_range_ready;
    R0 = DM(_iv_extension_version);
    R1 = 11;
    COMP(R0,R1);
    IF NE JUMP _iv_extension_range_ready;
    I5 = _iv_wtfm_high_max;
_iv_extension_range_ready:
    R7 = DM(I1, M6);
    R0 = FEXT R7 BY 0:16;
    R1 = DM(I2, M6);
    COMP(R0, R1);
    IF LT JUMP _iv_wt_reject;
    R1 = DM(I3, M6);
    COMP(R0, R1);
    IF GT JUMP _iv_wt_reject;
    R2 = DM(I6, M6);
    R2 = PASS R2;
    IF LT JUMP _iv_wt_extension;
    R2 = LSHIFT R2 BY 2;
    R3 = DM(_iv_values);
    R2 = R2 + R3;
    I0 = R2;
    R3 = DM(0, I0);
    COMP(R0, R3);
    IF NE JUMP _iv_wt_reject;
_iv_wt_extension:
    R0 = LSHIFT R7 BY -16;
    R1 = DM(I4, M6);
    COMP(R0, R1);
    IF LT JUMP _iv_wt_reject;
    R1 = DM(I5, M6);
    COMP(R0, R1);
    IF GT JUMP _iv_wt_reject;
    R6 = R6 - 1;
    IF NE JUMP _iv_wt_presentation;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(20, I1);
    R0 = FEXT R0 BY 0:16;
    R1 = LSHIFT R0 BY 1;
    R0 = R0 + R1;
    R0 = LSHIFT R0 BY -1;
    R1 = DM(15, I1);
    COMP(R0, R1);
    IF NE JUMP _iv_wt_reject;
    R0 = DM(13, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _iv_wt_clock;
    R0 = DM(12, I1);
    R0 = PASS R0;
    IF NE JUMP _iv_wt_reject;
_iv_wt_clock:
    R0 = DM(1, I1);
    R1 = 3;
    COMP(R0, R1);
    IF EQ JUMP _iv_wt_valid;
    R0 = DM(22, I1);
    R0 = LSHIFT R0 BY -16;
    R0 = PASS R0;
    IF EQ JUMP _iv_wt_valid;
    R0 = DM(19, I1);
    R0 = LSHIFT R0 BY -16;
    R1 = 7;
    COMP(R0, R1);
    IF GT JUMP _iv_wt_reject;
_iv_wt_valid:
    R2 = 32;
    JUMP _iv_wt_restore;
_iv_wt_reject:
    R2 = 0;
_iv_wt_restore:
    R0 = DM(_iv_wt_i3);
    I3 = R0;
    R0 = DM(_iv_wt_i4);
    I4 = R0;
    R0 = DM(_iv_wt_i5);
    I5 = R0;
    R0 = DM(_iv_wt_i6);
    I6 = R0;
    R0 = DM(_iv_wt_l3);
    L3 = R0;
    R0 = DM(_iv_wt_l4);
    L4 = R0;
    R0 = DM(_iv_wt_l5);
    L5 = R0;
    R0 = DM(_iv_wt_l6);
    L6 = R0;
    R2 = PASS R2;
    IF EQ JUMP _iv_no;
    R0 = DM(_iv_metadata);
    I0 = R0;
    JUMP _iv_config_copy;
_iv_five_config:
    R0 = DM(5, I0);
    R1 = 0x43505335;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 5;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
_iv_five_body:
    R0 = DM(10, I1);
    R1 = 4;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R0 = DM(11, I1);
    R2 = DM(0, I1);
    R2 = FEXT R2 BY 0:8;
    R1 = 8;
    COMP(R2, R1);
    IF EQ JUMP _iv_spread_range;
    R1 = 1;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 3;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    JUMP _iv_spread_ok;
_iv_spread_range:
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R1 = 6;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
_iv_spread_ok:
    R0 = DM(12, I1);
    R1 = 75;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R0 = DM(13, I1);
    R1 = 2;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R0 = DM(14, I1);
    R1 = 2;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R0 = DM(15, I1);
    R1 = 750;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = PASS R0;
    IF LT JUMP _iv_no;
    R0 = DM(13, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _iv_four_body;
    R0 = DM(12, I1);
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    JUMP _iv_four_body;
_iv_four_config:
    R0 = DM(5, I0);
    R1 = 0x43505334;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 4;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
_iv_four_body:
    R0 = DM(1, I1);
    R0 = LSHIFT R0 BY -1;
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    R0 = DM(7, I1);
    R1 = 30;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 300;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(8, I1);
    R1 = 1;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 8;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = DM(9, I1);
    R1 = 1;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 100;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    MODIFY(I1, 2) (NW);
    R2 = 5;
_iv_four_validate:
    R0 = DM(I1, M6);
    R0 = LSHIFT R0 BY -7;
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    R2 = R2 - 1;
    IF NE JUMP _iv_four_validate;
    R2 = 10;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 5;
    COMP(R0, R1);
    R1 = 16;
    IF EQ R2 = PASS R1;
    R0 = FEXT R0 BY 0:8;
    R1 = 6;
    COMP(R0, R1);
    R1 = 32;
    IF EQ R2 = PASS R1;
    R1 = 7;
    COMP(R0, R1);
    R1 = 32;
    IF EQ R2 = PASS R1;
    R1 = 8;
    COMP(R0, R1);
    R1 = 32;
    IF EQ R2 = PASS R1;
    JUMP _iv_config_copy;
_iv_legacy_config:
    R0 = DM(5, I0);
    R1 = 0x43505331;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R2 = 1;
_iv_config_copy:
    // A valid schema can still exceed the installed module's declared bank.
    // Reject before replacing the last usable configuration or its revision.
    I1 = _ir_process_rows;
    R0 = DM(7, I1);
    COMP(R2, R0);
    IF GT JUMP _iv_no;
    DM(_iv_control_count) = R2;
    R0 = DM(_iv_values);
    I1 = R0;
    I2 = _iv_controls;
_iv_config_word:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _iv_config_word;
    // The CF flags the instance on its selected track: only that slot's
    // status reaches the link. The flag is not part of the module's bank.
    R0 = DM(_iv_controls);
    R1 = FEXT R0 BY 15:1;
    R0 = BCLR R0 BY 15;
    DM(_iv_controls) = R0;
    R1 = PASS R1;
    IF EQ JUMP _iv_config_unfocused;
    R1 = DM(_iv_slot);
    DM(_iv_focus) = R1;
_iv_config_unfocused:
    R0 = DM(2, I0);
    DM(_iv_revision) = R0;
    R0 = 1;
    RTS;

_instrument_events:
    DM(_iv_packet) = R4;
    I0 = R4;
    R4 = DM(10, I0);
    R4 = FEXT R4 BY 10:4; // the batch track (every event names the same track)
    CALL _iv_find_slot;
    R4 = PASS R0;
    R4 = PASS R4;
    IF LT JUMP _iv_no;
    CALL _iv_slot_load;
    CALL _iv_events_slot;
    DM(_iv_call_result) = R0;
    CALL _iv_slot_store;
    R0 = DM(_iv_call_result);
    RTS;
_iv_events_slot:
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    R0 = DM(_iv_revision);
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    I0 = _iv_input;
    R0 = DM(8, I0);
    R0 = PASS R0;
    IF NE JUMP _iv_no; // never overwrite an unconsumed batch
    R4 = DM(_iv_packet);
    CALL _iv_lock_stage;
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    R4 = DM(_iv_packet);
    R8 = _iv_input;
    R12 = DM(_mi_epoch);
    CALL _ir_normalize_packet;
    R0 = PASS R0;
    IF LE JUMP _iv_no;
    CALL _iv_lock_commit;
    R0 = 1;
    RTS;
_iv_no:
    R0 = 0;
    RTS;

_iv_admitted:
    I0 = _ir_rows;
    R0 = DM(0, I0);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(3, I0);
    R1 = 1;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 16;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R1 = 4;
    COMP(R0, R1);
    IF GT R0 = PASS R1;
    DM(_iv_slot_count) = R0;
    I1 = _ir_process_rows;
    R0 = DM(0, I1);
    R1 = DM(1, I0);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(1, I1);
    R1 = DM(2, I0);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(2, I1);
    R1 = 16;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 255;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = 1;
    RTS;

// Resets the loaded slot's registry instance: state base + slot * stride.
_iv_reset_row:
    CALL _iv_lock_clear;
    R0 = 0;
    DM(_iv_mod_ready) = R0;
    CALL _iv_owner_clear;
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_bad;
    R0 = DM(_iv_slot);
    R1 = DM(_iv_slot_count);
    COMP(R0, R1);
    IF GE JUMP _iv_reset_bad;
    I0 = _ir_rows;
    R5 = DM(5, I0);
    R1 = LSHIFT R5 BY 2;
    R2 = 7;
    R1 = R1 + R2;
    R2 = 0xfffffff8;
    R1 = R1 AND R2; // the admission stride: state bytes rounded to 8
    R0 = DM(_iv_slot);
    R1 = R0 * R1 (SSI);
    R4 = DM(4, I0);
    R4 = R4 + R1;
    R6 = DM(6, I0);
    R7 = DM(7, I0);
    R12 = DM(8, I0);
    R9 = DM(9, I0);
    R10 = DM(10, I0);
    R8 = 2;
    CALL _ir_reset_instance;
    R0 = PASS R0;
    IF EQ RTS;
    I0 = _ir_rows;
    R1 = 5;
    DM(0, I0) = R1;
    DM(12, I0) = R0;
    RTS;
_iv_reset_bad:
    R0 = 0xffffffff;
    RTS;

// Raw stock-carrier ABI: copy the already-rendered block, never advance DSP
// twice. The stock wrapper duplicates this for the odd stereo voice. R2 is the
// voice's track; a track without a slot (the same type beyond the admitted
// instances) renders silence.
_iv_voice_render:
    DM(_iv_render_out) = R8;
    DM(_iv_render_track) = R2;
    R4 = PASS R2;
    CALL _iv_find_slot;
    R0 = PASS R0;
    IF LT JUMP _iv_voice_silent;
    R4 = PASS R0;
    CALL _iv_slot_base;
    I1 = I0;
    MODIFY(I1, 42) (NW); // slot output
    R0 = DM(_iv_render_out);
    I2 = R0;
    R1 = 32;
_iv_voice_copy:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _iv_voice_copy;
    R4 = DM(_iv_render_track);
    R8 = DM(2, I0); // slot active
    R10 = DM(1, I0); // slot revision
    CALL _mi_carrier_rendered;
    R0 = 0;
    JUMP (M13, I13);
_iv_voice_silent:
    R0 = DM(_iv_render_out);
    I0 = R0;
    R1 = 32;
    R0 = 0;
_iv_voice_zero:
    DM(I0, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _iv_voice_zero;
    JUMP (M13, I13);

.SECTION/DM seg_dmda;
.PKDATA _instrument_voice_rows, 192;
.PKDATA _iv_input, 136;
.PKDATA _iv_post_requested, 4;
.PKDATA _iv_native_valid, 4;
.PKDATA _iv_output, 128;
.PKDATA _iv_events, 192;
.VAR _iv_controls = 1;
.VAR _iv_controls_1 = 0;
.VAR _iv_controls_2 = 0;
.VAR _iv_controls_3 = 0;
.VAR _iv_controls_4 = 5;
.VAR _iv_controls_5 = 15;
.VAR _iv_controls_6 = 0;
.VAR _iv_controls_7 = 120;
.VAR _iv_controls_8 = 4;
.VAR _iv_controls_9 = 50;
.VAR _iv_controls_10 = 0;
.VAR _iv_controls_11 = 1;
.VAR _iv_controls_12 = 0;
.VAR _iv_controls_13 = 0;
.VAR _iv_controls_14 = 0;
.VAR _iv_controls_15 = 0;
.VAR _iv_controls_16 = 0;
.VAR _iv_controls_17 = 0;
.VAR _iv_controls_18 = 0;
.VAR _iv_controls_19 = 0;
.VAR _iv_controls_20 = 0;
.VAR _iv_controls_21 = 0;
.VAR _iv_controls_22 = 0;
.VAR _iv_controls_23 = 0;
.VAR _iv_controls_24 = 0;
.VAR _iv_controls_25 = 0;
.VAR _iv_controls_26 = 0;
.VAR _iv_controls_27 = 0;
.VAR _iv_controls_28 = 0;
.VAR _iv_controls_29 = 0;
.VAR _iv_controls_30 = 0;
.VAR _iv_controls_31 = 0;
.VAR _iv_control_count = 1;
.VAR _iv_wt_scalar_min = 0;
.VAR _iv_wt_scalar_min_1 = 0;
.VAR _iv_wt_scalar_min_2 = 0;
.VAR _iv_wt_scalar_min_3 = 0;
.VAR _iv_wt_scalar_min_4 = 0;
.VAR _iv_wt_scalar_min_5 = 0;
.VAR _iv_wt_scalar_min_6 = 30;
.VAR _iv_wt_scalar_min_7 = 1;
.VAR _iv_wt_scalar_min_8 = 1;
.VAR _iv_wt_scalar_min_9 = 0;
.VAR _iv_wt_scalar_min_10 = 0;
.VAR _iv_wt_scalar_min_11 = 0;
.VAR _iv_wt_scalar_min_12 = 0;
.VAR _iv_wt_scalar_min_13 = 0;
.VAR _iv_wt_scalar_min_14 = 0;
.VAR _iv_wt_scalar_max = 3;
.VAR _iv_wt_scalar_max_1 = 127;
.VAR _iv_wt_scalar_max_2 = 127;
.VAR _iv_wt_scalar_max_3 = 127;
.VAR _iv_wt_scalar_max_4 = 127;
.VAR _iv_wt_scalar_max_5 = 127;
.VAR _iv_wt_scalar_max_6 = 300;
.VAR _iv_wt_scalar_max_7 = 8;
.VAR _iv_wt_scalar_max_8 = 100;
.VAR _iv_wt_scalar_max_9 = 4;
.VAR _iv_wt_scalar_max_10 = 6;
.VAR _iv_wt_scalar_max_11 = 75;
.VAR _iv_wt_scalar_max_12 = 2;
.VAR _iv_wt_scalar_max_13 = 2;
.VAR _iv_wt_scalar_max_14 = 750;
.VAR _iv_wt_low_min = 0;
.VAR _iv_wt_low_min_1 = 0;
.VAR _iv_wt_low_min_2 = 0;
.VAR _iv_wt_low_min_3 = 0;
.VAR _iv_wt_low_min_4 = 0;
.VAR _iv_wt_low_min_5 = 0;
.VAR _iv_wt_low_min_6 = 1;
.VAR _iv_wt_low_min_7 = 0;
.VAR _iv_wt_low_min_8 = 0;
.VAR _iv_wt_low_min_9 = 1;
.VAR _iv_wt_low_min_10 = 0;
.VAR _iv_wt_low_min_11 = 0;
.VAR _iv_wt_low_min_12 = 0;
.VAR _iv_wt_low_min_13 = 0;
.VAR _iv_wt_low_min_14 = 0;
.VAR _iv_wt_low_min_15 = 0;
.VAR _iv_wt_low_max = 11;
.VAR _iv_wt_low_max_1 = 3;
.VAR _iv_wt_low_max_2 = 127;
.VAR _iv_wt_low_max_3 = 127;
.VAR _iv_wt_low_max_4 = 500;
.VAR _iv_wt_low_max_5 = 127;
.VAR _iv_wt_low_max_6 = 8;
.VAR _iv_wt_low_max_7 = 4;
.VAR _iv_wt_low_max_8 = 6;
.VAR _iv_wt_low_max_9 = 100;
.VAR _iv_wt_low_max_10 = 75;
.VAR _iv_wt_low_max_11 = 2;
.VAR _iv_wt_low_max_12 = 1;
.VAR _iv_wt_low_max_13 = 0;
.VAR _iv_wt_low_max_14 = 127;
.VAR _iv_wt_low_max_15 = 127;
.VAR _iv_wt_high_min = 0;
.VAR _iv_wt_high_min_1 = 0;
.VAR _iv_wt_high_min_2 = 0;
.VAR _iv_wt_high_min_3 = 1;
.VAR _iv_wt_high_min_4 = 0;
.VAR _iv_wt_high_min_5 = 0;
.VAR _iv_wt_high_min_6 = 0;
.VAR _iv_wt_high_min_7 = 0;
.VAR _iv_wt_high_min_8 = 0;
.VAR _iv_wt_high_min_9 = 0;
.VAR _iv_wt_high_min_10 = 0;
.VAR _iv_wt_high_min_11 = 0;
.VAR _iv_wt_high_min_12 = 0;
.VAR _iv_wt_high_min_13 = 0;
.VAR _iv_wt_high_min_14 = 0;
.VAR _iv_wt_high_min_15 = 0;
.VAR _iv_wt_high_max = 4;
.VAR _iv_wt_high_max_1 = 700;
.VAR _iv_wt_high_max_2 = 100;
.VAR _iv_wt_high_max_3 = 1000;
.VAR _iv_wt_high_max_4 = 2;
.VAR _iv_wt_high_max_5 = 2;
.VAR _iv_wt_high_max_6 = 1;
.VAR _iv_wt_high_max_7 = 200;
.VAR _iv_wt_high_max_8 = 127;
.VAR _iv_wt_high_max_9 = 127;
.VAR _iv_wt_high_max_10 = 200;
.VAR _iv_wt_high_max_11 = 200;
.VAR _iv_wt_high_max_12 = 0;
.VAR _iv_wt_high_max_13 = 0;
.VAR _iv_wt_high_max_14 = 0;
.VAR _iv_wt_high_max_15 = 0;
.VAR _iv_fm_high_min = 0;
.VAR _iv_fm_high_min_1 = 0;
.VAR _iv_fm_high_min_2 = 0;
.VAR _iv_fm_high_min_3 = 0;
.VAR _iv_fm_high_min_4 = 0;
.VAR _iv_fm_high_min_5 = 0;
.VAR _iv_fm_high_min_6 = 0;
.VAR _iv_fm_high_min_7 = 0;
.VAR _iv_fm_high_min_8 = 0;
.VAR _iv_fm_high_min_9 = 0;
.VAR _iv_fm_high_min_10 = 0;
.VAR _iv_fm_high_min_11 = 0;
.VAR _iv_fm_high_min_12 = 0;
.VAR _iv_fm_high_min_13 = 0;
.VAR _iv_fm_high_min_14 = 0;
.VAR _iv_fm_high_min_15 = 0;
.VAR _iv_fm_high_max = 3;
.VAR _iv_fm_high_max_1 = 100;
.VAR _iv_fm_high_max_2 = 15;
.VAR _iv_fm_high_max_3 = 15;
.VAR _iv_fm_high_max_4 = 100;
.VAR _iv_fm_high_max_5 = 127;
.VAR _iv_fm_high_max_6 = 127;
.VAR _iv_fm_high_max_7 = 100;
.VAR _iv_fm_high_max_8 = 127;
.VAR _iv_fm_high_max_9 = 200;
.VAR _iv_fm_high_max_10 = 200;
.VAR _iv_fm_high_max_11 = 200;
.VAR _iv_fm_high_max_12 = 0;
.VAR _iv_fm_high_max_13 = 0;
.VAR _iv_fm_high_max_14 = 0;
.VAR _iv_fm_high_max_15 = 0;
.VAR _iv_wtfm_high_max = 100;
.VAR _iv_wtfm_high_max_1 = 15;
.VAR _iv_wtfm_high_max_2 = 200;
.VAR _iv_wtfm_high_max_3 = 200;
.PKBSS _iv_extension_version, 4;
.VAR _iv_wt_duplicate = 0xffffffff;
.VAR _iv_wt_duplicate_1 = 0xffffffff;
.VAR _iv_wt_duplicate_2 = 2;
.VAR _iv_wt_duplicate_3 = 3;
.VAR _iv_wt_duplicate_4 = 0xffffffff;
.VAR _iv_wt_duplicate_5 = 6;
.VAR _iv_wt_duplicate_6 = 8;
.VAR _iv_wt_duplicate_7 = 10;
.VAR _iv_wt_duplicate_8 = 11;
.VAR _iv_wt_duplicate_9 = 9;
.VAR _iv_wt_duplicate_10 = 12;
.VAR _iv_wt_duplicate_11 = 14;
.VAR _iv_wt_duplicate_12 = 0xffffffff;
.VAR _iv_wt_duplicate_13 = 0xffffffff;
.VAR _iv_wt_duplicate_14 = 4;
.VAR _iv_wt_duplicate_15 = 5;
.VAR _iv_generation = 0;
.VAR _iv_track = 0;
// Slots: storage, bound tracks (slot 0 starts on track 0), focus and scratch.
.PKBSS _iv_slots, 2560;
.PKBSS _iv_post_slots, 512;
.VAR _iv_slot_tracks = 0;
.VAR _iv_slot_tracks_1 = 0xffffffff;
.VAR _iv_slot_tracks_2 = 0xffffffff;
.VAR _iv_slot_tracks_3 = 0xffffffff;
.VAR _iv_slot_count = 1;
.VAR _iv_slot = 0;
.VAR _iv_focus = 0;
.PKBSS _iv_post_buf, 4;
.PKBSS _iv_loop, 4;
.PKBSS _iv_call_result, 4;
.PKBSS _iv_reset_slot, 4;
.PKBSS _iv_render_out, 4;
.PKBSS _iv_render_track, 4;
.PKDATA _iv_ui_sink, 52;
.VAR _iv_revision = 0;
.VAR _iv_active = 0;
.VAR _iv_result = 0;
.PKBSS _iv_packet, 4;
.PKBSS _iv_values, 4;
.PKBSS _iv_metadata, 4;

.PKDATA _instrument_play_status, 8;
.PKDATA _instrument_ui_status, 52;

.PKBSS _iv_wt_i3, 4;
.PKBSS _iv_wt_i4, 4;
.PKBSS _iv_wt_i5, 4;
.PKBSS _iv_wt_i6, 4;
.PKBSS _iv_wt_l3, 4;
.PKBSS _iv_wt_l4, 4;
.PKBSS _iv_wt_l5, 4;
.PKBSS _iv_wt_l6, 4;
