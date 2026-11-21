// A track's tape motor runs in the input tap even when its stock AMP is closed.
// The voice consumes the resulting stereo block. Only pool address-cache misses
// traverse the directory. Caches are discarded before a revision can change.
_tape_row:
    R2 = LSHIFT R8 BY T_STRIDE_SHIFT; R3 = _rtm_tapes; R2 = R2 + R3; I5 = R2; RTS;
_tape_settings:
    CALL _tape_row;
    R0 = DM(4,I0); R1 = 31; R1 = R0 AND R1; DM(RT_SOURCE,I4) = R1;
    R2 = LSHIFT R0 BY -26; R1 = DM(T_MOD_TYPE,I5); COMP(R1,R2); R1 = 0;
    IF NE DM(T_MOD_PRIMED,I5) = R1; DM(T_MOD_TYPE,I5) = R2;
    R3 = DM(5,I0); R1 = DM(T_RATE,I5); COMP(R1,R3); IF NE CALL _tape_transition;
    DM(T_RATE,I5) = R3;
    R3 = DM(6,I0); R1 = DM(T_SPACING,I5); COMP(R1,R3); IF EQ JUMP _tape_spacing_ready;
    CALL _tape_transition; R1 = 0; DM(T_SLIP,I5) = R1;
_tape_spacing_ready:
    DM(T_SPACING,I5) = R3;
    R1 = 127; R2 = LSHIFT R0 BY -5; R2 = R2 AND R1; F2 = FLOAT R2;
    R3 = 0x3c23d70a; F2 = F2 * F3; DM(T_GAIN,I5) = R2; DM(T_EFF_GAIN,I5) = R2;
    R2 = LSHIFT R0 BY -12; R2 = R2 AND R1; F2 = FLOAT R2; F2 = F2 * F3; DM(T_FEEDBACK,I5) = R2;
    R2 = LSHIFT R0 BY -19; R2 = R2 AND R1;
    F2 = FLOAT R2; F2 = F2 * F3; DM(T_MIX,I5) = R2;
    R0 = 1; DM(T_ACTIVE,I5) = R0;
    R0 = DM(7,I0); R0 = PASS R0; IF GT DM(T_GRID,I5) = R0;
    JUMP _rt_schedule;
_tape_transition:
    R1 = 16; DM(T_RESET,I5) = R1;
    R1 = DM(T_LAST_L,I5); DM(12,I5) = R1; R1 = DM(T_LAST_R,I5); DM(13,I5) = R1; RTS;
_tape_open:
    R0 = DM(_rt_mode); R1 = 1; COMP(R0,R1); IF NE JUMP _tape_clone;
    R0 = DM(_t_arm_grid); R1 = DM(_rt_running); R1 = PASS R1; IF EQ JUMP _tape_grid_ready;
    I0 = _pattern_position_info; R0 = DM(8,I0);
_tape_grid_ready:
    R1 = 1; R0 = MAX(R0,R1); DM(T_GRID,I5) = R0;
    R0 = 0; DM(T_SLIP,I5) = R0; DM(T_FRAC,I5) = R0; DM(RT_POS,I4) = R0; DM(T_LAST_L,I5) = R0; DM(T_LAST_R,I5) = R0;
    R8 = -1; JUMP _tape_prepare;
_tape_clone:
    R8 = DM(_rt_target);
_tape_prepare:
    R4 = RLY_TAPE; CALL _relay_pool; R0 = PASS R0; IF LT JUMP _rt_full_abort;
    R8 = DM(_rt_writer); CALL _rt_row;
    CALL _tape_row; R0 = 0; DM(T_STOPPED,I5) = R0;
    R0 = DM(_rt_goal); DM(RT_LENGTH,I4) = R0;
    R0 = RLY_WRITER; DM(RT_SLOT,I4) = R0;
    R0 = 0; DM(_rt_armed) = R0; DM(RT_PREVIEW_END,I4) = R0;
    R1 = DM(RT_PLAYING,I4); R0 = 1; R1 = R1 XOR R0;
    DM(RT_LAUNCH,I4) = R1; DM(RT_PLAYING,I4) = R0;
    JUMP _rt_advance;
_tape_close:
    // Publish the complete private tape even for a punch lasting one frame.
    R8 = DM(_rt_writer); CALL _rt_row;
    R0 = DM(RT_POS,I4); DM(_t_resume_pos) = R0;
    R0 = DM(_rt_goal); DM(_rt_written) = R0; CALL _rt_commit;
    R8 = DM(_rt_writer); CALL _rt_row;
    R0 = DM(_t_resume_pos); DM(RT_POS,I4) = R0; RTS;
_tape_share:
    R0 = DM(_rt_mode); R0 = PASS R0; IF NE JUMP _rt_busy;
    R0 = DM(_rt_load); R0 = PASS R0; IF GE JUMP _rt_busy;
    R8 = DM(6,I0); R1 = 15; COMP(R8,R1); IF GT JUMP _rt_invalid;
    R0 = DM(_rt_focus); COMP(R8,R0); IF EQ JUMP _rt_invalid;
    CALL _rt_row; R0 = DM(RT_DIRTY,I4); R0 = PASS R0; IF NE JUMP _rt_busy;
    R0 = DM(RT_LENGTH,I4); R0 = PASS R0; IF LE JUMP _rt_empty;
    DM(_t_share_length) = R0;
    R1 = RLY_TRACK_MAIN; R12 = R8 + R1; R8 = DM(_rt_focus); R8 = R8 + R1;
    R4 = RLY_SHARE; CALL _relay_pool; R0 = PASS R0; IF LT JUMP _rt_empty;
    R8 = DM(_rt_focus); R1 = RLY_TRACK_UNDO; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_focus); R1 = RLY_TRACK_T1; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_focus); R1 = RLY_TRACK_T2; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_focus); CALL _rt_row; CALL _tape_row;
    R0 = 0; DM(RT_PLAYING,I4) = R0; DM(RT_DIRTY,I4) = R0; DM(RT_POS,I4) = R0;
    DM(RT_UNDOABLE,I4) = R0; DM(RT_UNDONE,I4) = R0; DM(RT_AUDITION,I4) = R0;
    DM(RT_PREVIEW_END,I4) = R0; DM(RT_ARM_PLAY,I4) = R0; DM(RT_LANES,I4) = R0; DM(T_FRAC,I5) = R0; DM(T_SLIP,I5) = R0;
    R1 = RLY_TRACK_MAIN; R1 = R8 + R1; DM(RT_SLOT,I4) = R1;
    R0 = DM(_t_share_length); DM(RT_LENGTH,I4) = R0;
    R0 = DM(RT_REVISION,I4); R0 = R0 + 1; DM(RT_REVISION,I4) = R0; JUMP _rt_schedule;
// R2 is at most one lap outside the tape, R3 is its length.
_tape_fold:
    COMP(R2,R3); IF GE R2 = R2 - R3;
    R2 = PASS R2; IF LT R2 = R2 + R3; RTS;
// Return the unsigned read offset from the writer at the configured spacing.
_tape_base_offset:
    R1 = DM(T_SPACING,I5); R0 = DM(T_RATE,I5);
_tape_rate_offset:
    F1 = FLOAT R1;
    R2 = 0x37800000; F1 = F1 * F2; F2 = FLOAT R3; F2 = F2 * F1; R2 = FIX F2;
    R0 = PASS R0; IF GE R2 = -R2;
    JUMP _tape_fold;
_tape_seek:
    CALL _tape_row; R3 = DM(RT_LENGTH,I4); R3 = PASS R3; IF LE JUMP _rt_schedule;
    R1 = DM(7,I0); COMP(R1,R3); IF NE JUMP _rt_schedule;
    R1 = DM(RT_PREVIEW_END,I4); R1 = PASS R1; IF NE JUMP _rt_schedule;
    R1 = RT_HOME_READ; COMP(R6,R1); IF EQ JUMP _tape_home;
    R4 = DM(6,I0); R1 = ABS R4; COMP(R1,R3); IF GT JUMP _rt_schedule;
    R1 = RT_SEEK_WRITE; COMP(R6,R1); IF NE JUMP _tape_move_read;
    R2 = DM(RT_POS,I4); R2 = R2 + R4; CALL _tape_fold; DM(RT_POS,I4) = R2;
    R4 = -R4;
_tape_move_read:
    R2 = DM(T_SLIP,I5); R2 = R2 + R4; CALL _tape_fold; JUMP _tape_store_slip;
_tape_home:
    CALL _tape_base_offset; R1 = DM(RT_POS,I4); R2 = R2 + R1; CALL _tape_fold;
    R2 = -R2; CALL _tape_fold;
_tape_store_slip:
    DM(T_SLIP,I5) = R2; CALL _tape_transition; JUMP _rt_schedule;
_tape_play:
    R0 = DM(_rt_mode); R1 = DM(_rt_writer); COMP(R1,R8); IF NE JUMP _tape_motor;
    R0 = PASS R0; IF EQ JUMP _tape_motor;
    R1 = DM(_rt_armed); R1 = PASS R1; IF NE JUMP _rt_cancel;
    R1 = 2; COMP(R0,R1); IF NE JUMP _rt_cancel;
    CALL _tape_close;
_tape_motor:
    R0 = DM(RT_LENGTH,I4); R0 = PASS R0; IF EQ JUMP _rt_empty;
    R8 = DM(_rt_focus); CALL _tape_row; CALL _tape_transition;
    R1 = DM(RT_PLAYING,I4); DM(T_STOPPED,I5) = R1;
    R0 = 1; R1 = R1 XOR R0; DM(RT_PLAYING,I4) = R1; DM(RT_LAUNCH,I4) = R1;
    JUMP _rt_schedule;

// Resolve one 256-frame window, shared by both heads. R12 is an in-range
// frame. The second cache prevents alternating read and write heads thrashing.
_tape_address:
    R1 = LSHIFT R12 BY -8;
    R0 = DM(_t_key0); COMP(R0,R1); IF EQ JUMP _t_hit0;
    R0 = DM(_t_key1); COMP(R0,R1); IF EQ JUMP _t_hit1;
    R0 = DM(_t_key0); DM(_t_key1) = R0; R0 = DM(_t_ptr0); DM(_t_ptr1) = R0;
    DM(_t_key0) = R1;
    R8 = DM(_t_slot); R4 = RLY_READ_SPAN; CALL _relay_pool;
    // The slot is pinned by this motor's current revision for the whole block.
    R1 = 255; R1 = R12 AND R1; R1 = LSHIFT R1 BY 2; R0 = R0 - R1; DM(_t_ptr0) = R0;
    R8 = DM(_t_track); CALL _rt_row; CALL _tape_row;
_t_hit0: R0 = DM(_t_ptr0); JUMP _t_address_offset;
_t_hit1: R0 = DM(_t_ptr1);
_t_address_offset:
    R1 = 255; R1 = R12 AND R1; R1 = LSHIFT R1 BY 2; R0 = R0 + R1; I0 = R0; RTS;
_tape_pcm:
    CALL _tape_address;
    R0 = DM(0,I0); R1 = LSHIFT R0 BY 16; R1 = ASHIFT R1 BY -16; R0 = ASHIFT R0 BY -16;
    F0 = FLOAT R0; F1 = FLOAT R1; R2 = 0x38000000; F0 = F0 * F2; F1 = F1 * F2; RTS;
_tape_alt_gain:
    // Preserve the pool span contract while scaling an independent alternate.
    I0 = R4; I1 = R8; R8 = DM(_rt_writer); CALL _tape_row;
    R4 = I0; R8 = I1; R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF EQ RTS;
    F2 = DM(T_EFF_GAIN,I5); I2 = _t_alt_l; I3 = _t_alt_r;
    LCNTR = R9, DO _t_alt_sample UNTIL LCE;
        F0 = DM(I0,M6); F1 = DM(I1,M6); F0 = F0 * F2; F1 = F1 * F2;
        DM(I2,M6) = R0;
_t_alt_sample: DM(I3,M6) = R1;
    R4 = _t_alt_l; R8 = _t_alt_r; RTS;
// The native frame stores two halfwords per DSP word. Convert one centered
// MOD lane to a bipolar float, rejecting values outside the stock range.
_tape_mod_delta:
    R1 = 65535; R0 = R0 AND R1; R1 = 32512; COMP(R0,R1);
    R1 = 16384; IF GT R0 = R1; R0 = R0 - R1;
    F0 = FLOAT R0; R1 = 0x38800000; F0 = F0 * F1; RTS;
_tape_mod_percent:
    CALL _tape_mod_delta; F0 = F0 + F4;
    R1 = 0; F0 = MAX(F0,F1); R1 = 0x3f800000; F0 = MIN(F0,F1); RTS;
_tape_modulate:
    R0 = DM(T_RATE,I5); DM(T_EFF_RATE,I5) = R0;
    R0 = DM(T_SPACING,I5); DM(T_EFF_SPACING,I5) = R0;
    R0 = DM(T_FEEDBACK,I5); DM(T_EFF_FEEDBACK,I5) = R0;
    R0 = DM(T_MIX,I5); DM(T_EFF_MIX,I5) = R0;
    R0 = DM(T_GAIN,I5); DM(T_EFF_GAIN,I5) = R0;
    R0 = 0; DM(T_EFF_POSITION,I5) = R0;
    R4 = DM(T_MOD_TYPE,I5); R4 = PASS R4; IF EQ RTS;
    R0 = LSHIFT R8 BY -1; R0 = LSHIFT R0 BY 2; R1 = _rtm_mod_frame;
    R0 = R0 + R1; I0 = R0; R0 = DM(37,I0);
    R1 = 1; R1 = R8 AND R1; IF NE R0 = LSHIFT R0 BY -16;
    R1 = 65535; R0 = R0 AND R1; COMP(R0,R4); IF NE JUMP _tape_mod_foreign;
    R0 = DM(T_MOD_PRIMED,I5); R0 = PASS R0; IF NE JUMP _tape_mod_current;
    R0 = 1; DM(T_MOD_PRIMED,I5) = R0; RTS;
_tape_mod_foreign:
    R0 = 0; DM(T_MOD_PRIMED,I5) = R0; RTS;
_tape_mod_current:
    R1 = 96; R0 = R8 * R1 (SSI); R1 = _rtm_mod_frame; R0 = R0 + R1; I0 = R0;
    R0 = DM(55,I0); CALL _tape_mod_delta;
    R1 = 0x48800000; F0 = F0 * F1; R0 = FIX F0; R1 = DM(T_RATE,I5); R0 = R0 + R1;
    R1 = -131072; R0 = MAX(R0,R1); R1 = 131072; R0 = MIN(R0,R1); DM(T_EFF_RATE,I5) = R0;
    R0 = DM(55,I0); R0 = LSHIFT R0 BY -16; CALL _tape_mod_delta;
    R1 = 0x47800000; F0 = F0 * F1; R0 = FIX F0; R1 = DM(T_SPACING,I5); R0 = R0 + R1;
    R1 = 0; R0 = MAX(R0,R1); R1 = 65536; R0 = MIN(R0,R1); DM(T_EFF_SPACING,I5) = R0;
    R0 = DM(56,I0); F4 = DM(T_FEEDBACK,I5); CALL _tape_mod_percent; DM(T_EFF_FEEDBACK,I5) = R0;
    R0 = DM(57,I0); R0 = LSHIFT R0 BY -16;
    F4 = DM(T_MIX,I5); CALL _tape_mod_percent; DM(T_EFF_MIX,I5) = R0;
    R0 = DM(58,I0); F4 = DM(T_GAIN,I5); CALL _tape_mod_percent; DM(T_EFF_GAIN,I5) = R0;
    R0 = DM(58,I0); R0 = LSHIFT R0 BY -16; CALL _tape_mod_delta; DM(T_EFF_POSITION,I5) = R0; RTS;
_tape_track:
    DM(_t_track) = R8;
    CALL _tape_modulate;
    R0 = DM(T_EFF_RATE,I5); DM(_t_rate) = R0;
    R0 = DM(RT_PREVIEW_END,I4); R0 = PASS R0; IF NE JUMP _tape_slice_offset;
    R3 = DM(RT_LENGTH,I4); R0 = DM(_t_rate); R1 = DM(T_EFF_SPACING,I5); CALL _tape_rate_offset;
    R1 = DM(T_SLIP,I5); R2 = R2 + R1; CALL _tape_fold;
    F0 = DM(T_EFF_POSITION,I5); F1 = FLOAT R3; F0 = F0 * F1; R0 = FIX F0;
    R2 = R2 + R0; CALL _tape_fold;
    R1 = DM(T_SLIP,I5); R0 = R0 OR R1; R1 = DM(T_EFF_SPACING,I5); R0 = R0 OR R1; JUMP _tape_offset_ready;
_tape_slice_offset:
    R0 = 65536; DM(_t_rate) = R0; R2 = 0;
_tape_offset_ready:
    DM(_t_separation) = R0; DM(_t_offset) = R2; DM(T_READ_OFFSET,I5) = R2;
    F2 = DM(T_EFF_MIX,I5); R0 = 0x3f800000; COMP(F2,F0); R0 = 0; IF LT R0 = R0 + 1;
    R1 = DM(T_MONITOR,I5); DM(T_MONITOR,I5) = R0;
    COMP(R0,R1); IF LE JUMP _tape_monitor_ready;
    R1 = DM(RT_PLAYING,I4); R1 = PASS R1; IF EQ DM(RT_LAUNCH,I4) = R0;
_tape_monitor_ready:
    R0 = DM(RT_PLAYING,I4); R1 = DM(T_MONITOR,I5); R0 = R0 OR R1;
    R1 = DM(T_RESET,I5); R0 = R0 OR R1; DM(T_TAIL_BLOCK,I5) = R0;
    R0 = PASS R0; IF EQ JUMP _rt_step_next;
    R0 = DM(RT_SOURCE,I4); CALL _rt_inputs; DM(_t_left) = R4; DM(_t_right) = R8;
    R0 = DM(RT_SLOT,I4); DM(_t_slot) = R0;
    R0 = -1; DM(_t_key0) = R0; DM(_t_key1) = R0;
    R1 = DM(_rt_writer); R0 = DM(_t_track); COMP(R0,R1); R0 = 0; IF NE JUMP _tape_write_gate;
    R1 = DM(_rt_armed); R1 = PASS R1; IF NE JUMP _tape_write_gate;
    R0 = DM(_rt_mode); R1 = 3; COMP(R0,R1); IF EQ R0 = R0 XOR R0;
_tape_write_gate:
    DM(_t_gate) = R0; R5 = 0;
_tape_sample:
    R0 = LSHIFT R5 BY 2; R1 = DM(_t_left); R1 = R1 + R0; I0 = R1;
    R1 = DM(_t_right); R1 = R1 + R0; I1 = R1; F1 = DM(0,I0); F2 = DM(0,I1);
    F3 = DM(T_EFF_GAIN,I5); F1 = F1 * F3; F2 = F2 * F3; DM(_t_dry_l) = R1; DM(_t_dry_r) = R2;
    R0 = DM(RT_PLAYING,I4); R1 = DM(_t_rate); R0 = PASS R0;
    IF EQ JUMP _tape_park; R1 = PASS R1; IF EQ JUMP _tape_park;
    R3 = DM(RT_LENGTH,I4); R3 = PASS R3; IF LE JUMP _tape_park;
    R12 = DM(RT_POS,I4); R1 = DM(_t_offset); R12 = R12 + R1;
    COMP(R12,R3); IF GE R12 = R12 - R3; R12 = PASS R12; IF LT R12 = R12 + R3;
    DM(_t_read) = R12; CALL _tape_pcm;
    R2 = DM(T_FRAC,I5); R2 = PASS R2; IF EQ JUMP _tape_heard;
    DM(_t_interp_l) = R0; DM(_t_interp_r) = R1;
    R12 = DM(_t_read); R12 = R12 + 1; R3 = DM(RT_LENGTH,I4); COMP(R12,R3); IF GE R12 = R12 - R3;
    CALL _tape_pcm; R2 = DM(T_FRAC,I5); F2 = FLOAT R2; R3 = 0x37800000; F2 = F2 * F3;
    F3 = DM(_t_interp_l); F0 = F0 - F3; F0 = F0 * F2; F0 = F0 + F3;
    F3 = DM(_t_interp_r); F1 = F1 - F3; F1 = F1 * F2; F1 = F1 + F3;
_tape_heard:
    DM(_t_wet_l) = R0; DM(_t_wet_r) = R1;
    R2 = DM(T_FRAC,I5); R3 = DM(_t_rate); R2 = R2 + R3;
    R3 = 65535; R3 = R2 AND R3; DM(T_FRAC,I5) = R3; R2 = ASHIFT R2 BY -16; DM(_t_delta) = R2;
    R2 = DM(_t_gate); R2 = PASS R2; IF EQ JUMP _tape_advance;
    R2 = DM(_t_separation); R2 = PASS R2; IF NE JUMP _tape_print;
    F0 = DM(T_LAST_L,I5); F1 = DM(T_LAST_R,I5);
_tape_print:
    F2 = DM(T_EFF_FEEDBACK,I5); F0 = F0 * F2; F1 = F1 * F2;
    F2 = DM(_t_dry_l); F0 = F0 + F2; F2 = DM(_t_dry_r); F1 = F1 + F2;
    R2 = 0xbf800000; F0 = MAX(F0,F2); F1 = MAX(F1,F2);
    R2 = 0x3f7ffe00; F0 = MIN(F0,F2); F1 = MIN(F1,F2);
    R2 = DM(_t_separation); R2 = PASS R2; IF NE JUMP _tape_pack;
    DM(_t_wet_l) = R0; DM(_t_wet_r) = R1;
_tape_pack:
    R2 = 0x47000000; F0 = F0 * F2; F1 = F1 * F2; R0 = FIX F0; R1 = FIX F1;
    R2 = 65535; R1 = R1 AND R2; R0 = LSHIFT R0 BY 16; R0 = R0 OR R1; DM(_t_word) = R0;
    R12 = DM(RT_POS,I4); CALL _tape_address; R0 = DM(_t_word); DM(0,I0) = R0;
    R2 = DM(_t_delta); R1 = ABS R2; R3 = 2; COMP(R1,R3); IF LT JUMP _tape_advance;
    R2 = PASS R2; IF GT R12 = R12 + 1; IF LT R12 = R12 - 1;
    R3 = DM(RT_LENGTH,I4); COMP(R12,R3); IF GE R12 = R12 - R3; R12 = PASS R12; IF LT R12 = R12 + R3;
    CALL _tape_address; R0 = DM(_t_word); DM(0,I0) = R0;
_tape_advance:
    R0 = DM(RT_POS,I4); R2 = DM(_t_delta); R0 = R0 + R2;
    R3 = DM(RT_LENGTH,I4);
_tape_wrap: COMP(R0,R3); IF LT JUMP _tape_wrap_low; R0 = R0 - R3; JUMP _tape_wrap;
_tape_wrap_low: R0 = PASS R0; IF GE JUMP _tape_position; R0 = R0 + R3; JUMP _tape_wrap_low;
_tape_position:
    R1 = DM(RT_PREVIEW_END,I4); R1 = PASS R1; IF EQ JUMP _tape_set_pos;
    R2 = DM(RT_POS,I4); R2 = R2 + 1; COMP(R2,R1); IF LT JUMP _tape_set_pos;
    R0 = 0; DM(RT_PREVIEW_END,I4) = R0; DM(RT_PLAYING,I4) = R0;
_tape_set_pos: DM(RT_POS,I4) = R0; JUMP _tape_output;
_tape_park: R0 = 0; DM(_t_wet_l) = R0; DM(_t_wet_r) = R0;
_tape_output:
    F0 = DM(_t_wet_l); F1 = DM(_t_wet_r); DM(T_LAST_L,I5) = R0; DM(T_LAST_R,I5) = R1;
    R2 = DM(T_RESET,I5); R2 = PASS R2; IF EQ JUMP _tape_mix;
    F3 = FLOAT R2; R2 = R2 - 1; DM(T_RESET,I5) = R2;
    R2 = 0x3d800000; F3 = F3 * F2;
    F2 = DM(12,I5); F2 = F2 - F0; F2 = F2 * F3; F0 = F0 + F2;
    F2 = DM(13,I5); F2 = F2 - F1; F2 = F2 * F3; F1 = F1 + F2;
_tape_mix:
    R2 = LSHIFT R5 BY 2; R3 = I5; R2 = R2 + R3; I0 = R2;
    F2 = DM(T_EFF_MIX,I5); R3 = 0x3f800000; F3 = F3 - F2;
    F0 = F0 * F2; F1 = F1 * F2;
    F2 = DM(_t_dry_l); F2 = F2 * F3; F0 = F0 + F2; DM(T_LEFT,I0) = R0;
    F2 = DM(_t_dry_r); F2 = F2 * F3; F1 = F1 + F2; DM(T_RIGHT,I0) = R1;
    R0 = DM(_t_gate); R1 = 1; COMP(R0,R1); IF NE JUMP _tape_next_sample;
    R0 = DM(_rt_written); R0 = R0 + 1; DM(_rt_written) = R0;
    R1 = DM(_rt_goal); COMP(R0,R1); IF NE JUMP _tape_next_sample;
    R0 = 0; DM(_t_gate) = R0;
_tape_next_sample:
    R5 = R5 + 1; R0 = 32; COMP(R5,R0); IF LT JUMP _tape_sample;
    R8 = DM(_t_track); R0 = DM(_rt_writer); COMP(R8,R0); IF NE JUMP _rt_step_next;
    R0 = DM(_rt_mode); R1 = 1; COMP(R0,R1); IF NE JUMP _rt_step_next;
    R0 = DM(_rt_written); R1 = DM(_rt_goal); COMP(R0,R1); IF LT JUMP _rt_step_next;
    CALL _tape_close; JUMP _rt_step_next;

.SECTION/DM seg_dmda;
.GLOBAL _rtm_tapes;
.VAR _rtm_tapes[2048];
.VAR _t_alt_l[32];
.VAR _t_alt_r[32];
.VAR _t_arm_grid;
.VAR _t_track;
.VAR _t_slot;
.VAR _t_share_length;
.VAR _t_key0;
.VAR _t_key1;
.VAR _t_ptr0;
.VAR _t_ptr1;
.VAR _t_rate;
.VAR _t_delta;
.VAR _t_offset;
.VAR _t_separation;
.VAR _t_left;
.VAR _t_right;
.VAR _t_gate;
.VAR _t_dry_l;
.VAR _t_dry_r;
.VAR _t_wet_l;
.VAR _t_wet_r;
.VAR _t_interp_l;
.VAR _t_interp_r;
.VAR _t_word;
.VAR _t_read;
.VAR _t_resume_pos;
.SECTION/PM seg_pmco;
