#include "dk_relay.inc"
#include "dk_relay_track.inc"
.SECTION/PM seg_pmco;
.EXTERN _relay_pool;
.EXTERN _relay_external;
.EXTERN _relay_pack_span;
.EXTERN _relay_status;
.EXTERN _relay_timing;
.EXTERN _rtm_command;
.EXTERN _rtm_bus;
.EXTERN _rtm_mod_frame;
.EXTERN _pattern_position_info;
.GLOBAL _rtm_block;
// The input tap runs once per 32 stereo frames even when no voice is active.
// RT_TRIG protects writer alignment against retriggers. All published roots
// belong to a track; only the uncommitted writer and the export pin are shared.
_rtm_block:
    R0 = DM(_rt_started);
    R0 = PASS R0;
    IF NE JUMP _rt_clock;
    I0 = _rtm_command; R0 = DM(1,I0); R0 = PASS R0; IF EQ RTS;
    R4 = RLY_RESET;
    CALL _relay_pool;
    R0 = 1;
    DM(_rt_started) = R0;
    R0 = -1;
    DM(_rt_load) = R0;
    DM(_rt_pin) = R0;
_rt_clock:
    I0 = _pattern_position_info;
    R0 = DM(0,I0); R1 = 0x50504f53; COMP(R0,R1); IF NE JUMP _rt_clock_gap;
    R0 = DM(5,I0);
    R1 = 2;
    R2 = R0 AND R1;
    R1 = DM(_rt_running);
    DM(_rt_running) = R2;
    R2 = PASS R2;
    IF NE JUMP _rt_clock_play;
    JUMP _rt_clock_stopped;
_rt_clock_gap:
    // A rejected clock packet carries no transport edge. Keep the last motor state.
    R1 = DM(_rt_running); R1 = PASS R1; IF EQ JUMP _rt_still_stopped;
    R0 = 0; DM(_rt_first) = R0; DM(_rt_beat) = R0; JUMP _rt_control;
_rt_clock_play:
    R1 = PASS R1; IF EQ CALL _rt_start_tracks;
    I0 = _pattern_position_info; R0 = DM(5,I0); JUMP _rt_clock_running;
_rt_clock_stopped:
    R1 = PASS R1;
    IF NE CALL _rt_stop;
_rt_still_stopped:
    R0 = 1;
    DM(_rt_beat) = R0; DM(_rt_first) = R0;
    JUMP _rt_control;
_rt_clock_running:
    R1 = 0;
    DM(_rt_first) = R1;
    DM(_rt_beat) = R1;
    R1 = DM(3,I0);
    R2 = DM(_rt_edge);
    DM(_rt_edge) = R1;
    COMP(R1,R2);
    IF EQ JUMP _rt_quarter;
    R1 = 1;
    R0 = R0 AND R1;
    IF EQ JUMP _rt_quarter;
    R0 = DM(7,I0);
    R0 = PASS R0;
    IF NE JUMP _rt_quarter;
    DM(_rt_first) = R1;
_rt_quarter:
    I0 = _relay_timing;
    R1 = DM(3,I0);
    R2 = DM(_rt_quarters);
    DM(_rt_quarters) = R1;
    COMP(R1,R2);
    IF EQ JUMP _rt_control;
    R0 = 1;
    DM(_rt_beat) = R0;
_rt_control:
    CALL _rt_transfer;
    I0 = _rtm_command;
    R1 = DM(1,I0);
    R2 = DM(_rt_seq);
    COMP(R1,R2);
    IF EQ JUMP _rt_schedule;
    DM(_rt_seq) = R1;
    R0 = 0; DM(_rt_error) = R0;
    R8 = DM(3,I0); R1 = 15;
    COMP(R8,R1); IF GT JUMP _rt_schedule;
    R8 = PASS R8; IF LT JUMP _rt_schedule;
    R6 = DM(2,I0); R1 = RT_VIEW; COMP(R6,R1); IF EQ JUMP _rt_view;
    R1 = RT_TRANSPORT_STOP; COMP(R6,R1); IF EQ JUMP _rt_panel_stop;
    DM(_rt_focus) = R8;
    CALL _rt_row;
    R6 = DM(2,I0); R1 = RT_SETTINGS; COMP(R6,R1); IF EQ JUMP _tape_settings;
    R1 = RT_SEEK_READ; COMP(R6,R1); IF EQ JUMP _tape_seek;
    R1 = RT_SEEK_WRITE; COMP(R6,R1); IF EQ JUMP _tape_seek;
    R1 = RT_HOME_READ; COMP(R6,R1); IF EQ JUMP _tape_seek;
    R1 = RT_FOCUS; COMP(R6,R1); IF EQ JUMP _rt_schedule;
    R1 = DM(4,I0);
    DM(RT_SOURCE,I4) = R1;
    R6 = DM(2,I0);
    R1 = RT_SHARE; COMP(R6,R1); IF EQ JUMP _tape_share;
    R1 = RT_WRITE; COMP(R6,R1); IF EQ JUMP _rt_toggle_write;
    R1 = RT_RECORD; COMP(R6,R1); IF EQ JUMP _rt_cmd_record;
    R1 = RT_FINISH; COMP(R6,R1); IF EQ JUMP _rt_cmd_finish;
    R1 = RT_PLAY; COMP(R6,R1); IF EQ JUMP _rt_cmd_play;
    R1 = RT_UNDO; COMP(R6,R1); IF EQ JUMP _rt_cmd_undo;
    R1 = RT_ALT; COMP(R6,R1); IF EQ JUMP _rt_cmd_alt;
    R1 = RT_AUD; COMP(R6,R1); IF EQ JUMP _rt_cmd_aud;
    R1 = RT_KEEP; COMP(R6,R1); IF EQ JUMP _rt_cmd_keep;
    R1 = RT_EXIT; COMP(R6,R1); IF EQ JUMP _rt_cmd_exit;
    R1 = RT_CLEAR; COMP(R6,R1); IF EQ JUMP _rt_cmd_clear;
    R1 = RT_PREVIEW; COMP(R6,R1); IF EQ JUMP _rt_cmd_preview;
    JUMP _rt_schedule;
_rt_panel_stop: CALL _rt_stop; JUMP _rt_schedule;
_rt_view: R0 = R8 + 1; DM(_rt_display) = R0;
_rt_schedule:
    R0 = DM(_rt_armed); R0 = PASS R0;
    IF EQ JUMP _rt_record_block;
    R8 = DM(_rt_writer); CALL _rt_row;
    R0 = DM(_rt_mode); R1 = 1;
    COMP(R0,R1); IF EQ JUMP _rt_first_boundary;
    R1 = 2; COMP(R0,R1); IF NE JUMP _rt_other_boundary;
    CALL _tape_row; R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF NE JUMP _rt_start_pass;
_rt_other_boundary:
    R0 = DM(RT_PLAYING,I4); R0 = PASS R0;
    IF EQ JUMP _rt_beat_boundary;
    CALL _tape_row; R1 = DM(T_ACTIVE,I5); R1 = PASS R1; IF NE JUMP _rt_beat_boundary;
    R0 = DM(RT_POS,I4); R1 = 32;
    COMP(R0,R1); IF GE JUMP _rt_record_block;
    JUMP _rt_start;
_rt_first_boundary:
    R0 = DM(_rt_len); R0 = PASS R0;
    IF NE JUMP _rt_fixed_boundary;
    R0 = DM(_rt_first); JUMP _rt_check_boundary;
_rt_fixed_boundary:
    R1 = 5; COMP(R0,R1); IF EQ JUMP _rt_start;
_rt_beat_boundary:
    R0 = DM(_rt_beat);
_rt_check_boundary:
    R0 = PASS R0; IF EQ JUMP _rt_record_block;
_rt_start:
    R0 = DM(_rt_mode); R1 = 1; COMP(R0,R1);
    IF NE JUMP _rt_start_pass;
    R0 = DM(_rt_len); R1 = 5;
    COMP(R0,R1); IF EQ JUMP _rt_start_pass;
    I0 = _relay_timing;
    R8 = DM(1,I0); R0 = PASS R0;
    IF EQ JUMP _rt_latch_length;
    R1 = R0 - 1;
    R8 = DM(2,I0); R8 = LSHIFT R8 BY R1;
_rt_latch_length:
    R8 = PASS R8; IF LE JUMP _rt_bad_length;
    DM(_rt_goal) = R8;
    R4 = RLY_RETARGET; CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _rt_full_abort;
_rt_start_pass:
    R8 = DM(_rt_writer); CALL _rt_row; CALL _tape_row;
    R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF EQ JUMP _rt_linear_pass;
    R0 = DM(_rt_mode); R1 = 3; COMP(R0,R1); IF NE JUMP _tape_open;
    R0 = 0; DM(_rt_armed) = R0; JUMP _rt_record_block;
_rt_linear_pass:
    R0 = 0; DM(_rt_armed) = R0;
    DM(RT_POS,I4) = R0;
    R1 = 1; DM(RT_TRIG,I4) = R1;
    R0 = DM(_rt_mode); R2 = 3; COMP(R0,R2);
    IF EQ JUMP _rt_mute_alt;
    JUMP _rt_record_block;
_rt_mute_alt:
    R0 = 0; DM(RT_PLAYING,I4) = R0;
_rt_record_block:
    R0 = DM(_rt_mode); R0 = PASS R0; IF EQ JUMP _rt_advance;
    R0 = DM(_rt_armed); R0 = PASS R0; IF NE JUMP _rt_advance;
    R8 = DM(_rt_writer); CALL _tape_row;
    R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF EQ JUMP _rt_linear_block;
    R0 = DM(_rt_mode); R1 = 3; COMP(R0,R1); IF NE JUMP _rt_advance;
_rt_linear_block:
    R0 = 0; DM(_rt_offset) = R0;
_rt_span:
    R0 = DM(_rt_goal); R1 = DM(_rt_written); R0 = R0 - R1;
    R1 = 32; R2 = DM(_rt_offset); R1 = R1 - R2;
    R0 = MIN(R0,R1); DM(_rt_count) = R0;
    R0 = DM(_rt_repeat); R0 = PASS R0; IF NE JUMP _rt_repeat_span;
    R4 = RLY_ACQUIRE; CALL _relay_pool;
    JUMP _rt_span_ready;
_rt_repeat_span:
    R8 = RLY_WRITER; R12 = DM(_rt_written); R4 = RLY_READ_SPAN; CALL _relay_pool;
_rt_span_ready:
    R2 = -1; COMP(R0,R2); IF EQ JUMP _rt_full_abort;
    DM(_rt_dst) = R0;
    R2 = DM(_rt_count); R2 = MIN(R2,R1); DM(_rt_count) = R2;
    R10 = DM(_rt_dst); R0 = DM(_rt_repeat); R0 = PASS R0; IF NE JUMP _rt_pack;
    R10 = 0; R0 = DM(_rt_mode); R1 = 2; COMP(R0,R1);
    IF NE JUMP _rt_pack;
    R0 = RLY_TRACK_MAIN; R8 = DM(_rt_writer); R8 = R8 + R0;
    R12 = DM(_rt_written); R4 = RLY_READ_SPAN; CALL _relay_pool;
    R2 = -1; COMP(R0,R2); IF EQ JUMP _rt_full_abort;
    R10 = R0; R2 = DM(_rt_count); R2 = MIN(R2,R1); DM(_rt_count) = R2;
_rt_pack:
    R0 = DM(_rt_source);
    CALL _rt_inputs;
    R0 = DM(_rt_offset); R0 = LSHIFT R0 BY 2;
    R4 = R4 + R0; R8 = R8 + R0;
    R12 = DM(_rt_dst); R9 = DM(_rt_count);
    R0 = DM(_rt_mode); R1 = 3; COMP(R0,R1); IF EQ CALL _tape_alt_gain;
    CALL _relay_pack_span;
    R8 = DM(_rt_count); R0 = DM(_rt_repeat); R0 = PASS R0; IF EQ JUMP _rt_publish_span;
    R0 = DM(_rt_written); R0 = R0 + R8; JUMP _rt_count_written;
_rt_publish_span: R4 = RLY_PUBLISH; CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _rt_full_abort;
_rt_count_written:
    DM(_rt_written) = R0; R1 = DM(_rt_goal); COMP(R0,R1);
    IF EQ JUMP _rt_complete;
    R0 = DM(_rt_count); R1 = DM(_rt_offset); R0 = R0 + R1; DM(_rt_offset) = R0;
    R1 = 32; COMP(R0,R1); IF LT JUMP _rt_span;
    R0 = DM(_rt_mode); R1 = 1; COMP(R0,R1); IF NE JUMP _rt_advance;
    R0 = DM(_rt_finish); R1 = DM(_rt_beat); R0 = R0 AND R1;
    IF EQ JUMP _rt_advance;
_rt_complete:
    R0 = DM(_rt_mode); R1 = 2; COMP(R0,R1); IF NE JUMP _rt_complete_once;
    R0 = DM(_rt_finish); R0 = PASS R0; IF NE JUMP _rt_complete_once;
    R0 = 1; DM(_rt_repeat) = R0; R0 = 0; DM(_rt_written) = R0;
    R8 = DM(_rt_writer); CALL _rt_row; R0 = RLY_WRITER; DM(RT_SLOT,I4) = R0;
    R0 = DM(_rt_count); R1 = DM(_rt_offset); R0 = R0 + R1; DM(_rt_offset) = R0;
    R1 = 32; COMP(R0,R1); IF LT JUMP _rt_span;
    JUMP _rt_advance;
_rt_complete_once: CALL _rt_commit;
_rt_advance:
    R0 = 0; DM(_rt_dirty_mask) = R0; DM(_rt_tape_mask) = R0;
    R8 = 0; DM(_rt_launch_mask) = R8; DM(_rt_gate_mask) = R8;
_rt_phase_track:
    CALL _rt_row;
    R0 = DM(RT_ARM_PLAY,I4); R1 = DM(_rt_beat); R0 = R0 AND R1;
    IF EQ JUMP _rt_step;
    R0 = 0; DM(RT_ARM_PLAY,I4) = R0;
    DM(RT_POS,I4) = R0;
    R1 = DM(RT_PLAYING,I4); R0 = 1; R1 = R1 XOR R0; DM(RT_PLAYING,I4) = R1; DM(RT_LAUNCH,I4) = R1;
_rt_step:
    CALL _tape_row; R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF NE JUMP _tape_track;
    R0 = DM(RT_PLAYING,I4); R0 = PASS R0; IF EQ JUMP _rt_step_next;
    R1 = DM(RT_LENGTH,I4); R1 = PASS R1; IF LE JUMP _rt_step_next;
    R0 = DM(RT_POS,I4); R2 = 32; R0 = R0 + R2;
    R2 = DM(RT_PREVIEW_END,I4); R2 = PASS R2;
    IF EQ JUMP _rt_wrap;
    COMP(R0,R2); IF LT JUMP _rt_pos;
    R0 = 0; DM(RT_PREVIEW_END,I4) = R0; DM(RT_PLAYING,I4) = R0;
_rt_wrap:
    COMP(R0,R1); IF LT JUMP _rt_pos;
    R0 = R0 - R1; JUMP _rt_wrap;
_rt_pos:
    DM(RT_POS,I4) = R0;
_rt_step_next:
    R0 = DM(RT_DIRTY,I4); R0 = LSHIFT R0 BY R8; R1 = DM(_rt_dirty_mask); R1 = R1 OR R0; DM(_rt_dirty_mask) = R1;
    R0 = DM(RT_LENGTH,I4); R0 = PASS R0; IF EQ JUMP _rt_tape_absent;
    R0 = 1; R0 = LSHIFT R0 BY R8; R1 = DM(_rt_tape_mask); R1 = R1 OR R0; DM(_rt_tape_mask) = R1;
_rt_tape_absent:
    CALL _tape_row; R1 = DM(T_MONITOR,I5);
    R0 = DM(RT_PLAYING,I4); R0 = R0 OR R1;
    R2 = LSHIFT R0 BY R8; R3 = DM(_rt_gate_mask); R3 = R3 OR R2; DM(_rt_gate_mask) = R3;
    R1 = DM(RT_LAUNCH,I4); R0 = R0 AND R1;
    IF EQ JUMP _rt_next;
    R0 = LSHIFT R0 BY R8; R1 = DM(_rt_launch_mask); R1 = R1 OR R0; DM(_rt_launch_mask) = R1;
_rt_next:
    R8 = R8 + 1; R0 = 16; COMP(R8,R0); IF LT JUMP _rt_phase_track;
    JUMP _rt_report;
_rt_row:
    R2 = LSHIFT R8 BY 6; R3 = _rtm_tracks; R2 = R2 + R3; I4 = R2;
    RTS;
_rt_inputs:
    R1 = 3; COMP(R0,R1); IF LT JUMP _rt_ext;
    R0 = R0 - R1; R0 = LSHIFT R0 BY 8; R1 = _rtm_bus; R4 = R1 + R0;
    R1 = 128; R8 = R4 + R1; RTS;
_rt_ext:
    R4 = _relay_external; R1 = 128; R8 = R4 + R1;
    R1 = 1; COMP(R0,R1); IF EQ R8 = R4;
    R1 = 2; COMP(R0,R1); IF EQ R4 = R8;
    RTS;
_rt_toggle_write:
    R0 = DM(_rt_mode); R0 = PASS R0; IF EQ JUMP _rt_cmd_record;
    R1 = DM(_rt_writer); COMP(R8,R1); IF EQ JUMP _rt_cmd_finish;
    JUMP _rt_busy;
_rt_cmd_record:
    R7 = 1; R0 = DM(RT_LENGTH,I4); R0 = PASS R0;
    IF EQ JUMP _rt_arm;
    R7 = 2; JUMP _rt_arm;
_rt_cmd_alt:
    R7 = 3;
_rt_arm:
    R0 = DM(_rt_load); R0 = PASS R0; IF GE JUMP _rt_busy;
    R0 = DM(_rt_mode); R0 = PASS R0; IF NE JUMP _rt_busy;
    R1 = DM(4,I0); R2 = 18; COMP(R1,R2); IF GT JUMP _rt_invalid;
    R2 = 3; R2 = R2 + R8; COMP(R1,R2); IF EQ JUMP _rt_invalid;
    DM(_rt_source) = R1; DM(_rt_writer) = R8;
    R1 = DM(5,I0); DM(_rt_len) = R1;
    R1 = DM(7,I0); DM(_t_arm_grid) = R1;
    R1 = RLY_TRACK_MAIN; R2 = R8 + R1; DM(_rt_target) = R2;
    R1 = 1; COMP(R7,R1); IF EQ JUMP _rt_first_arm;
    R1 = DM(RT_LENGTH,I4); R1 = PASS R1; IF EQ JUMP _rt_empty;
    R1 = 3; COMP(R7,R1); IF NE JUMP _rt_loop_arm;
    R0 = DM(RT_LANES,I4); R1 = 1; R0 = R0 AND R1;
    IF EQ JUMP _rt_lane1;
    R0 = DM(RT_LANES,I4); R1 = 2; R0 = R0 AND R1;
    IF NE JUMP _rt_lane_full;
    R1 = RLY_TRACK_T2; JUMP _rt_lane;
_rt_lane1:
    R1 = RLY_TRACK_T1;
_rt_lane:
    R1 = R1 + R8; DM(_rt_target) = R1;
_rt_loop_arm:
    R8 = DM(RT_LENGTH,I4); JUMP _rt_reserve;
_rt_first_arm:
    R8 = DM(6,I0);
    R1 = DM(_rt_len); R2 = 5; COMP(R1,R2); IF NE JUMP _rt_check_goal;
    R8 = 0; JUMP _rt_reserve;
_rt_check_goal:
    R8 = PASS R8; IF LE JUMP _rt_len_error;
_rt_reserve:
    DM(_rt_goal) = R8; DM(_rt_wanted) = R7;
    R4 = RLY_BEGIN; CALL _relay_pool; R0 = PASS R0; IF LT JUMP _rt_full;
    R0 = DM(_rt_goal); R0 = PASS R0; IF NE JUMP _rt_ready;
    R4 = RLY_AVAILABLE; CALL _relay_pool; DM(_rt_goal) = R0;
_rt_ready:
    R0 = DM(_rt_wanted); DM(_rt_mode) = R0;
    R0 = 1; DM(_rt_armed) = R0; R0 = 0; DM(_rt_finish) = R0; DM(_rt_written) = R0;
    JUMP _rt_schedule;
_rt_cmd_finish:
    R1 = DM(_rt_writer); COMP(R1,R8); IF NE JUMP _rt_busy;
    R1 = DM(_rt_mode); R1 = PASS R1; IF EQ JUMP _rt_schedule;
    R0 = DM(_rt_armed); R0 = PASS R0; IF NE JUMP _rt_cancel;
    CALL _tape_row; R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF EQ JUMP _rt_legacy_finish;
    R0 = 2; COMP(R1,R0); IF NE JUMP _rt_cancel;
    CALL _tape_close; JUMP _rt_schedule;
_rt_legacy_finish:
    R0 = DM(_rt_len); R2 = 5; COMP(R0,R2); IF NE JUMP _rt_toggle;
    R2 = 1; COMP(R1,R2); IF NE JUMP _rt_toggle;
    CALL _rt_commit; JUMP _rt_schedule;
_rt_toggle:
    R1 = DM(_rt_finish); R0 = 1; R1 = R1 XOR R0; DM(_rt_finish) = R1;
    JUMP _rt_schedule;
_rt_cancel:
    CALL _rt_abort; JUMP _rt_schedule;
_rt_cmd_play:
    CALL _tape_row; R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF NE JUMP _tape_play;
    R0 = DM(RT_LENGTH,I4); R0 = PASS R0; IF EQ JUMP _rt_empty;
    R0 = DM(RT_ARM_PLAY,I4); R1 = 1; R0 = R0 XOR R1; DM(RT_ARM_PLAY,I4) = R0;
    JUMP _rt_schedule;
_rt_busy_local:
    R0 = DM(_rt_mode); R0 = PASS R0; IF EQ RTS;
    R0 = DM(_rt_writer); COMP(R0,R8); IF NE JUMP _rt_local_ok;
    R0 = 1; RTS;
_rt_local_ok: R0 = 0; RTS;
_rt_cmd_undo:
    CALL _rt_busy_local; R0 = PASS R0; IF NE JUMP _rt_busy;
    R0 = DM(RT_UNDOABLE,I4); R0 = PASS R0; IF EQ JUMP _rt_schedule;
    R1 = RLY_TRACK_UNDO; R12 = R1 + R8;
    R1 = RLY_TRACK_MAIN; R8 = R1 + R8;
    R4 = RLY_SWAP; CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _rt_busy;
    R8 = DM(_rt_focus); CALL _rt_row;
    R0 = DM(RT_UNDONE,I4); R1 = 1; R0 = R0 XOR R1; DM(RT_UNDONE,I4) = R0;
    JUMP _rt_dirty;
_rt_cmd_aud:
    CALL _rt_busy_local; R0 = PASS R0; IF NE JUMP _rt_busy;
    R0 = DM(RT_LENGTH,I4); R0 = PASS R0; IF EQ JUMP _rt_empty;
    R0 = DM(6,I0); CALL _rt_lane_slot;
    R0 = PASS R0; IF LT JUMP _rt_empty;
    DM(RT_SLOT,I4) = R0;
    R1 = DM(RT_AUDITION,I4); R1 = PASS R1; IF NE JUMP _rt_schedule;
    R1 = DM(RT_PLAYING,I4); DM(RT_BEFORE_AUD,I4) = R1;
    R0 = 1; R1 = R1 XOR R0; DM(RT_LAUNCH,I4) = R1;
    R1 = 1; DM(RT_AUDITION,I4) = R1; DM(RT_PLAYING,I4) = R1; JUMP _rt_schedule;
_rt_lane_slot:
    R1 = RLY_TRACK_MAIN; R1 = R8 + R1;
    R0 = PASS R0; IF EQ JUMP _rt_slot_ok;
    R2 = 2; COMP(R0,R2); IF GT JUMP _rt_no_slot;
    R3 = R0 - 1; R2 = 1; R2 = LSHIFT R2 BY R3;
    R3 = DM(RT_LANES,I4); R2 = R2 AND R3; IF EQ JUMP _rt_no_slot;
    R0 = LSHIFT R0 BY 4; R1 = R0 + R1;
_rt_slot_ok: R0 = R1; RTS;
_rt_no_slot: R0 = -1; RTS;
_rt_end_aud:
    R0 = DM(RT_AUDITION,I4); R0 = PASS R0; IF EQ RTS;
    R0 = 0; DM(RT_AUDITION,I4) = R0;
    R0 = DM(RT_BEFORE_AUD,I4); DM(RT_PLAYING,I4) = R0;
    R0 = RLY_TRACK_MAIN; R0 = R8 + R0; DM(RT_SLOT,I4) = R0; RTS;
_rt_cmd_exit: CALL _rt_end_aud; JUMP _rt_schedule;
_rt_cmd_keep:
    R0 = DM(RT_AUDITION,I4); R0 = PASS R0; IF EQ JUMP _rt_schedule;
    R12 = DM(RT_SLOT,I4); CALL _rt_end_aud;
    R1 = RLY_TRACK_MAIN; R8 = R1 + R8; COMP(R8,R12); IF EQ JUMP _rt_schedule;
    R4 = RLY_SWAP; CALL _relay_pool;
    R8 = DM(_rt_focus); R1 = RLY_TRACK_UNDO; R8 = R1 + R8; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_focus); CALL _rt_row; R0 = 0;
    DM(RT_UNDOABLE,I4) = R0; DM(RT_UNDONE,I4) = R0;
_rt_dirty:
    R0 = 1; DM(RT_DIRTY,I4) = R0; R0 = DM(RT_REVISION,I4); R0 = R0 + 1; DM(RT_REVISION,I4) = R0;
    JUMP _rt_schedule;
_rt_cmd_preview:
    R0 = DM(6,I0); R1 = DM(7,I0); R2 = DM(RT_LENGTH,I4);
    COMP(R1,R2); IF GT JUMP _rt_schedule;
    COMP(R1,R0); IF LE JUMP _rt_schedule;
    DM(RT_POS,I4) = R0; DM(RT_PREVIEW_END,I4) = R1;
    R0 = DM(RT_PLAYING,I4); R1 = 1; R0 = R0 XOR R1; DM(RT_LAUNCH,I4) = R0; R1 = 1; DM(RT_PLAYING,I4) = R1;
    JUMP _rt_schedule;
_rt_cmd_clear:
    CALL _rt_busy_local; R0 = PASS R0; IF NE JUMP _rt_busy;
    CALL _rt_end_aud;
    R0 = DM(6,I0); R0 = PASS R0; IF EQ JUMP _rt_clear_all;
    DM(_rt_lane_number) = R0; CALL _rt_lane_slot; R0 = PASS R0; IF LT JUMP _rt_empty;
    R8 = R0; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_focus); CALL _rt_row;
    R0 = DM(_rt_lane_number); R0 = R0 - 1; R1 = 1; R1 = LSHIFT R1 BY R0;
    R0 = DM(RT_LANES,I4); R0 = R0 XOR R1; DM(RT_LANES,I4) = R0;
    JUMP _rt_dirty;
_rt_clear_all:
    CALL _rt_release_track; JUMP _rt_dirty;
_rt_release_track:
    DM(_rt_clear_track) = R8;
    R8 = DM(_rt_clear_track); R1 = RLY_TRACK_MAIN; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_clear_track); R1 = RLY_TRACK_T1; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_clear_track); R1 = RLY_TRACK_T2; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_clear_track); R1 = RLY_TRACK_UNDO; R8 = R8 + R1; R4 = RLY_CLEAR; CALL _relay_pool;
    R8 = DM(_rt_clear_track); CALL _rt_row; R0 = 0;
    DM(RT_LENGTH,I4) = R0; DM(RT_POS,I4) = R0; DM(RT_PLAYING,I4) = R0; DM(RT_LANES,I4) = R0;
    DM(RT_UNDONE,I4) = R0; DM(RT_UNDOABLE,I4) = R0; DM(RT_AUDITION,I4) = R0;
    DM(RT_PREVIEW_END,I4) = R0; DM(RT_ARM_PLAY,I4) = R0;
    CALL _tape_row; DM(T_GRID,I5) = R0; DM(T_FRAC,I5) = R0; DM(T_SLIP,I5) = R0;
    R1 = RLY_TRACK_MAIN; R1 = R1 + R8; DM(RT_SLOT,I4) = R1; RTS;
_rt_commit:
    R0 = DM(_rt_written); R0 = PASS R0; IF EQ JUMP _rt_abort;
    R4 = RLY_COMMIT; R0 = DM(_rt_mode); R1 = 2; COMP(R0,R1); IF NE JUMP _rt_publish;
    R4 = RLY_FINISH_DUB;
_rt_publish:
    R8 = DM(_rt_target); CALL _relay_pool;
    R0 = PASS R0; IF LT JUMP _rt_abort;
    R8 = DM(_rt_writer); CALL _rt_row;
    R0 = DM(_rt_mode); R1 = 3; COMP(R0,R1); IF EQ JUMP _rt_commit_lane;
    R1 = 2; COMP(R0,R1); IF NE JUMP _rt_commit_main;
    R0 = 1; DM(RT_UNDOABLE,I4) = R0; R0 = 0; DM(RT_UNDONE,I4) = R0;
_rt_commit_main:
    R0 = DM(_rt_written); DM(RT_LENGTH,I4) = R0;
    R0 = DM(_rt_target); DM(RT_SLOT,I4) = R0;
    R0 = DM(RT_REVISION,I4); R0 = R0 + 1; DM(RT_REVISION,I4) = R0;
    R0 = 1; DM(RT_DIRTY,I4) = R0;
    JUMP _rt_resume;
_rt_commit_lane:
    R0 = DM(RT_REVISION,I4); R0 = R0 + 1; DM(RT_REVISION,I4) = R0;
    R0 = DM(_rt_target); R1 = RLY_TRACK_T2; COMP(R0,R1); R2 = 1;
    IF LT JUMP _rt_lane_bit;
    R2 = 2;
_rt_lane_bit:
    R0 = DM(RT_LANES,I4); R0 = R0 OR R2; DM(RT_LANES,I4) = R0;
    CALL _tape_row; R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF NE JUMP _rt_idle;
_rt_resume:
    R0 = DM(RT_PLAYING,I4); R1 = 1; R0 = R0 XOR R1; DM(RT_LAUNCH,I4) = R0;
    R0 = 1; DM(RT_PLAYING,I4) = R0; R0 = 0; DM(RT_POS,I4) = R0;
    JUMP _rt_idle;
_rt_abort:
    R0 = DM(_rt_mode); R1 = 1; COMP(R0,R1); IF NE JUMP _rt_abort_pool;
    R8 = DM(_rt_writer); CALL _rt_row; CALL _tape_row;
    R1 = DM(T_ACTIVE,I5); R1 = PASS R1; IF EQ JUMP _rt_abort_pool;
    R1 = 0; DM(RT_LENGTH,I4) = R1; DM(RT_PLAYING,I4) = R1; DM(RT_LAUNCH,I4) = R1;
_rt_abort_pool:
    R4 = RLY_ABORT; CALL _relay_pool;
    R8 = DM(_rt_writer); CALL _rt_row;
_rt_idle:
    R0 = RLY_TRACK_MAIN; R0 = R0 + R8; DM(RT_SLOT,I4) = R0;
    R0 = 0; DM(_rt_repeat) = R0; DM(RT_TRIG,I4) = R0; DM(_rt_mode) = R0; DM(_rt_armed) = R0; DM(_rt_finish) = R0; RTS;
_rt_start_tracks:
    R8 = 0;
_rt_start_one:
    CALL _rt_row; CALL _tape_row;
    R0 = DM(T_ACTIVE,I5); R0 = PASS R0; IF EQ JUMP _rt_start_next;
    R0 = DM(RT_LENGTH,I4); R0 = PASS R0; IF EQ JUMP _rt_start_next;
    CALL _tape_transition;
    R0 = 0; DM(RT_POS,I4) = R0; DM(T_FRAC,I5) = R0; DM(T_SLIP,I5) = R0; DM(T_STOPPED,I5) = R0;
    DM(RT_PREVIEW_END,I4) = R0;
    R0 = 1; DM(RT_PLAYING,I4) = R0; DM(RT_LAUNCH,I4) = R0;
_rt_start_next:
    R8 = R8 + 1; R1 = 16; COMP(R8,R1); IF LT JUMP _rt_start_one;
    RTS;
_rt_stop:
    R0 = DM(_rt_mode); R0 = PASS R0; IF EQ JUMP _rt_stop_tracks;
    R1 = DM(_rt_armed); R1 = PASS R1; IF NE JUMP _rt_stop_abort;
    R8 = DM(_rt_writer); CALL _tape_row; R1 = DM(T_ACTIVE,I5); R1 = PASS R1;
    IF EQ JUMP _rt_legacy_stop;
    R1 = 2; COMP(R0,R1); IF NE JUMP _rt_stop_abort;
    CALL _tape_close; JUMP _rt_stop_tracks;
_rt_legacy_stop:
    R1 = 1; COMP(R0,R1); IF NE JUMP _rt_stop_abort;
    CALL _rt_commit; JUMP _rt_stop_tracks;
_rt_stop_abort: CALL _rt_abort;
_rt_stop_tracks:
    R8 = 0;
_rt_stop_one:
    CALL _rt_row; CALL _rt_end_aud; CALL _tape_row;
    R0 = DM(T_ACTIVE,I5); R1 = DM(RT_PLAYING,I4); R0 = R0 AND R1; IF NE CALL _tape_transition;
    R0 = 1; DM(T_STOPPED,I5) = R0;
    R0 = 0; DM(RT_PLAYING,I4) = R0; DM(RT_ARM_PLAY,I4) = R0; DM(RT_LAUNCH,I4) = R0;
    R8 = R8 + 1; R1 = 16; COMP(R8,R1); IF LT JUMP _rt_stop_one;
    RTS;
_rt_bad_length: CALL _rt_abort;
_rt_len_error: R0 = RL_CHOOSE_LEN; JUMP _rt_err;
_rt_full_abort: CALL _rt_abort;
_rt_full: R0 = RL_RAM_FULL; JUMP _rt_err;
_rt_lane_full: R0 = RL_LANES_FULL; JUMP _rt_err;
_rt_busy: R0 = RL_BUSY; JUMP _rt_err;
_rt_empty: R0 = RL_EMPTY; JUMP _rt_err;
_rt_invalid: R0 = RL_NOT_READY;
_rt_err: DM(_rt_error) = R0; JUMP _rt_advance;
_rt_report:
    R8 = DM(_rt_sweep); R1 = R8 + 1; R0 = 15; R1 = R1 AND R0; DM(_rt_sweep) = R1;
    R1 = RLY_TRACK_MAIN; R8 = R1 + R8; R4 = RLY_TIDY; CALL _relay_pool;
    R8 = DM(_rt_display); R8 = PASS R8;
    IF EQ JUMP _rt_report_legacy;
    R8 = R8 - 1; JUMP _rt_report_row;
_rt_report_legacy: R8 = DM(_rt_focus);
_rt_report_row: CALL _rt_row;
    I3 = _relay_status;
    R0 = 0x524c5932; DM(0,I3) = R0; R0 = DM(_rt_seq); DM(1,I3) = R0;
    CALL _tape_row;
    R0 = DM(_rt_mode); R1 = DM(_rt_armed); R1 = LSHIFT R1 BY 4; R0 = R0 OR R1;
    R1 = DM(_rt_writer); R1 = LSHIFT R1 BY 8; R0 = R0 OR R1; R1 = 65535; R0 = R0 AND R1;
    R2 = DM(T_READ_OFFSET,I5); R1 = LSHIFT R2 BY 16; R0 = R0 OR R1; DM(2,I3) = R0;
    R1 = DM(_rt_gate_mask); R1 = LSHIFT R1 BY 16; R1 = R1 OR R8; R2 = LSHIFT R2 BY -16; R2 = LSHIFT R2 BY 8; R1 = R1 OR R2; DM(4,I3) = R1;
    R0 = DM(RT_LENGTH,I4); DM(5,I3) = R0;
    R0 = DM(RT_POS,I4); DM(6,I3) = R0;
    R0 = DM(RT_PLAYING,I4); R1 = DM(_rt_launch_mask); R1 = LSHIFT R1 BY 16;
    R0 = R0 OR R1; DM(7,I3) = R0;
    R1 = DM(_rt_tape_mask); R1 = LSHIFT R1 BY 16; R0 = DM(RT_LANES,I4); R0 = R0 OR R1;
    R1 = DM(RT_AUDITION,I4); R1 = LSHIFT R1 BY 2; R0 = R0 OR R1; DM(8,I3) = R0;
    R0 = DM(RT_REVISION,I4); DM(9,I3) = R0;
    R1 = DM(_rt_dirty_mask); R1 = LSHIFT R1 BY 16; R0 = DM(RT_UNDONE,I4); R0 = R0 OR R1; DM(10,I3) = R0;
    CALL _tape_row; R1 = DM(T_GRID,I5); R1 = LSHIFT R1 BY 8;
    R0 = DM(RT_UNDOABLE,I4); R0 = R0 OR R1; DM(11,I3) = R0;
    R0 = DM(_rt_written); DM(12,I3) = R0;
    R0 = DM(RT_SOURCE,I4); CALL _rt_inputs; I0 = R4; I1 = R8;
    R2 = 0; R3 = 0;
    LCNTR = 32, DO _rt_peak UNTIL LCE;
        F0 = DM(I0,M6); F1 = DM(I1,M6);
        F0 = ABS F0; F1 = ABS F1;
        F2 = MAX(F2,F0);
_rt_peak: F3 = MAX(F3,F1);
    R4 = 0x3f800000; F2 = MIN(F2,F4); F3 = MIN(F3,F4);
    R4 = 0x42fe0000; F2 = F2 * F4; F3 = F3 * F4;
    R2 = FIX F2; R3 = FIX F3; R2 = LSHIFT R2 BY 8; R3 = LSHIFT R3 BY 16;
    R0 = DM(_rt_error); R0 = R0 OR R2; R0 = R0 OR R3; DM(13,I3) = R0;
    R4 = RLY_AVAILABLE; CALL _relay_pool; I3 = _relay_status; DM(3,I3) = R0;
    // Feedback rows are shared by multiple modules. Sign the complete snapshot.
    I0 = _relay_status; R1 = DM(I0,M6); R0 = 0;
    LCNTR = 13, DO _rt_hash_word UNTIL LCE;
        R1 = LSHIFT R0 BY 5; R1 = R1 - R0; R2 = DM(I0,M6);
_rt_hash_word: R0 = R1 + R2;
    R1 = LSHIFT R0 BY -16; R0 = R0 XOR R1;
    R1 = LSHIFT R0 BY 24; R2 = DM(3,I3); R2 = R2 OR R1; DM(3,I3) = R2;
    R1 = LSHIFT R0 BY -8; R1 = LSHIFT R1 BY 24; R2 = DM(13,I3); R2 = R2 OR R1; DM(13,I3) = R2; RTS;

#include "tape.inc"
#include "transfer.inc"
.SECTION/DM seg_dmda;
.GLOBAL _rtm_tracks;
.VAR _rtm_tracks[256];
.VAR _rt_dirty_mask;
.VAR _rt_tape_mask;
.VAR _rt_sweep;
.VAR _rt_repeat;
.VAR _rt_started;
.VAR _rt_seq;
.VAR _rt_focus;
.VAR _rt_display;
.VAR _rt_gate_mask;
.VAR _rt_launch_mask;
.VAR _rt_writer;
.VAR _rt_target;
.VAR _rt_len;
.VAR _rt_goal;
.VAR _rt_wanted;
.VAR _rt_mode;
.VAR _rt_armed;
.VAR _rt_written;
.VAR _rt_finish;
.VAR _rt_dst;
.VAR _rt_count;
.VAR _rt_offset;
.VAR _rt_source;
.VAR _rt_error;
.VAR _rt_running;
.VAR _rt_edge;
.VAR _rt_first;
.VAR _rt_beat;
.VAR _rt_quarters;
.VAR _rt_clear_track;
.VAR _rt_lane_number;
.VAR _rt_pin;
.VAR _rt_load;
