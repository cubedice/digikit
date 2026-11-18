// DTII selective phrase router. Original track and raw-input buffers are read
// only. Native sends and the native master read private scaled copies, retaining
// the stock per-track compressor routing and leaving individual USB tracks alone.
#include "dk_fx.inc"
#include "dk_phrase.inc"

.SECTION/PM seg_pmco;
.GLOBAL _phr_pre;
.GLOBAL _phr_first;
.GLOBAL _phr_track_base;
.GLOBAL _phr_master;
.EXTERN _fx_table;
.EXTERN _fxs_trackbus;
.EXTERN _fxs_return;
.EXTERN _fx_blk;
.EXTERN _fx_ctx;
.EXTERN _fx_diag;
.EXTERN _fx_rec;
.EXTERN _fx_save;
.EXTERN _fx_in;
.EXTERN _fx_c;
.EXTERN _fx_scratch;
.EXTERN _fx_s_r0;
.EXTERN _fx_s_i4;
.EXTERN _fx_s_l4;
.EXTERN _phrase_control_info;
.EXTERN _phrase_status;
.EXTERN _phr_main_exclude;
.EXTERN _phr_pre_back;
.EXTERN _phr_first_back;
.EXTERN _phr_tracks_back;
.EXTERN _phr_master_back;

_phr_pre:
    DM(_fx_s_r0) = R0;
    R0 = I4;
    DM(_fx_s_i4) = R0;
    R0 = L4;
    DM(_fx_s_l4) = R0;
    DM(_phr_input_table) = R8;
    L4 = 0;
    CALL _phr_fx_enter;
    R0 = DM(_phr_initialized);
    R0 = PASS R0;
    IF NE JUMP _phr_init_done;
    R0 = 0x3f800000;
    I0 = _phr_dry_prev;
    LCNTR = 18, DO _phr_init_dry UNTIL LCE;
_phr_init_dry: DM(I0,M6) = R0;
    R0 = 1;
    DM(_phr_initialized) = R0;
_phr_init_done:
    // No owner, invalid controls, a failed call or stale status all select
    // the dry branch; this decision is owned by the adapter, not the module.
    R0 = 0;
    DM(_phr_valid) = R0;
    DM(_phr_mask) = R0;
    DM(_phr_wet_target) = R0;
    DM(_phr_old_mask) = R0;
    DM(_phr_new_mask) = R0;
    DM(_phr_mix) = R0;
    I0 = _phr_wet;
    LCNTR = 64, DO _phr_zero_wet UNTIL LCE;
_phr_zero_wet: DM(I0,M6) = R0;
    R1 = 0x3f800000;
    DM(_phr_dry_target) = R1;
    R0 = DM(_phrase_control_info + PH_C_MAGIC);
    R1 = PH_CONTROL_MAGIC;
    COMP(R0,R1);
    IF NE JUMP _phr_no_owner;
    R0 = DM(_phrase_control_info + PH_C_ENABLED);
    R0 = PASS R0;
    IF EQ JUMP _phr_no_owner;
    R0 = DM(_phrase_control_info + PH_C_OWNER_ROW);
    R0 = PASS R0;
    IF LT JUMP _phr_no_owner;
    R1 = 3;
    COMP(R0,R1);
    IF GT JUMP _phr_no_owner;
    R1 = LSHIFT R0 BY 6;
    R0 = LSHIFT R0 BY 5;
    R0 = R0 + R1;
    R1 = _fx_table;
    R0 = R0 + R1;
    DM(_phr_row) = R0;
    I4 = R0;
    R0 = DM(0,I4);
    R0 = PASS R0;
    IF EQ JUMP _phr_no_owner;
    R0 = R0 + 1;
    IF EQ JUMP _phr_no_owner;
    R0 = DM(18,I4);
    R1 = 2;
    COMP(R0,R1);
    IF NE JUMP _phr_no_owner;
    R0 = DM(3,I4);
    R0 = PASS R0;
    IF EQ JUMP _phr_no_owner;
    R0 = DM(14,I4);
    R0 = PASS R0;
    IF NE JUMP _phr_no_owner;
    R0 = DM(_phrase_control_info + PH_C_NEXT_MASK);
    R1 = PH_INPUT_MASK;
    R0 = R0 AND R1;
    DM(_phr_mask) = R0;
    R0 = 1;
    DM(_phr_valid) = R0;
_phr_no_owner:
    CALL _phr_capture;
    R0 = DM(_phr_valid);
    R0 = PASS R0;
    IF EQ JUMP _phr_live;
    R0 = DM(_phrase_status + PH_S_HEARTBEAT);
    DM(_phr_heartbeat) = R0;
    R0 = DM(_phrase_control_info + PH_C_OWNER_GENERATION);
    DM(_phr_generation) = R0;
    R0 = _phr_input;
    DM(_phrase_control_info + PH_C_INPUT_PTR) = R0;
    I4 = _fx_in;
    DM(DK_FX_IN_W_IN,I4) = R0;
    R0 = DM(_phr_row);
    DM(_fx_c) = R0;
    I4 = R0;
    CALL _phr_fx_exec;
    R0 = PASS R0;
    IF EQ JUMP _phr_fault;
    R1 = DM(_fx_c + 8);
    DM(12,I4) = R1;
    R0 = DM(_phrase_status + PH_S_MAGIC);
    R1 = PH_STATUS_MAGIC;
    COMP(R0,R1);
    IF NE JUMP _phr_fault;
    R0 = DM(_phrase_status + PH_S_VERSION);
    R1 = PH_VERSION;
    COMP(R0,R1);
    IF NE JUMP _phr_fault;
    R0 = DM(_phrase_status + PH_S_OWNER_GENERATION);
    R1 = DM(_phr_generation);
    COMP(R0,R1);
    IF NE JUMP _phr_fault;
    R0 = DM(_phrase_status + PH_S_HEARTBEAT);
    R1 = DM(_phr_heartbeat);
    COMP(R0,R1);
    IF EQ JUMP _phr_fault;
    // The effect is on whenever it is loaded. With a held phrase its sources are the phrase's (and, in a handoff, the
    // last phrase's); without one they are the sources its tape is recording now.
    R0 = DM(_phrase_status + PH_S_HELD_FRAMES);
    R0 = PASS R0;
    IF EQ JUMP _phr_tape_sources;
    R0 = DM(_phrase_status + PH_S_HELD_MASK);
    R1 = PH_INPUT_MASK;
    R0 = R0 AND R1;
    DM(_phr_new_mask) = R0;
    R0 = DM(_phrase_status + PH_S_OLD_MASK);
    R0 = R0 AND R1;
    DM(_phr_old_mask) = R0;
    JUMP _phr_sources;
_phr_tape_sources:
    R0 = DM(_phr_mask);
    DM(_phr_new_mask) = R0;
    DM(_phr_old_mask) = R0;
_phr_sources:
    R0 = DM(_phr_old_mask);
    R1 = DM(_phr_new_mask);
    R0 = R0 OR R1;
    IF EQ JUMP _phr_live;
    CALL _phr_recover;
    R0 = PASS R0;
    IF EQ JUMP _phr_live;
    R0 = DM(_phrase_status + PH_S_HANDOFF);
    CALL _phr_unit;
    DM(_phr_mix) = R0;
    R0 = DM(_phrase_control_info + PH_C_FADE);
    CALL _phr_unit;
    DM(_phrase_status + PH_S_FADE) = R0;
    DM(_phr_fade) = R0;
    // MIX: the wet comes up over the first half, where the sources stay whole; over the second half the sources give way.
    R1 = 0x3f800000;
    F0 = F0 + F0;
    F0 = MIN(F0,F1);
    CALL _phr_sin;
    DM(_phr_wet_target) = R0;
    R0 = DM(_phr_fade);
    R1 = 0x3f800000;
    F0 = F1 - F0;
    F0 = F0 + F0;
    F0 = MIN(F0,F1);
    CALL _phr_sin;
    DM(_phr_dry_target) = R0;
    I0 = _fx_scratch;
    I1 = _phr_wet;
    LCNTR = 64, DO _phr_keep_wet UNTIL LCE;
        R0 = DM(I0,M6);
_phr_keep_wet: DM(I1,M6) = R0;
    JUMP _phr_make_dry;
_phr_fault:
    // A successful future callback alone cannot unexpectedly re-duck LIVE.
    // Recovery requires a new owner, command or FADE zero then re-engage.
    R0 = DM(_phr_fault_latched);
    R0 = PASS R0;
    IF NE JUMP _phr_live;
    R0 = 1;
    DM(_phr_fault_latched) = R0;
    R0 = DM(_phrase_control_info + PH_C_CMD_SEQ);
    DM(_phr_fault_cmd) = R0;
    R0 = DM(_phrase_control_info + PH_C_OWNER_GENERATION);
    DM(_phr_fault_generation) = R0;
    R0 = DM(_phrase_control_info + PH_C_FADE);
    CALL _phr_unit;
    R0 = PASS R0;
    R1 = 0;
    R2 = 1;
    IF EQ R1 = PASS R2;
    DM(_phr_fault_zero) = R1;
_phr_live:
    R0 = 0;
    DM(_phrase_status + PH_S_FADE) = R0;
_phr_make_dry:
    // The CF UI must distinguish a latched safety fallback from a legitimate
    // zero FADE or a control/status roundtrip still in flight. Publish only
    // the current owner's latch; an unloaded/rebound owner cannot inherit it.
    R0 = 0;
    R1 = DM(_phr_fault_latched);
    R1 = PASS R1;
    IF EQ JUMP _phr_publish_fault;
    R1 = DM(_phr_fault_generation);
    R2 = DM(_phrase_control_info + PH_C_OWNER_GENERATION);
    COMP(R1,R2);
    R1 = 1;
    IF EQ R0 = PASS R1;
_phr_publish_fault:
    DM(_phrase_status + PH_S_ROUTE_FAULT) = R0;
    CALL _phr_shadows;
    CALL _phr_scale_wet;
    CALL _phr_fx_leave;
    R0 = DM(_fx_s_l4);
    L4 = R0;
    R0 = DM(_fx_s_i4);
    I4 = R0;
    R0 = DM(_fx_s_r0);
    R8 = _phr_ext_pointers;
    JUMP _phr_pre_back;

// Exact native reader substitutions. They never change the original buses.
_phr_first:
    I4 = _phr_tracks;
    JUMP _phr_first_back;
_phr_track_base:
    DM(_phr_pointer_r0) = R0;
    DM(_phr_pointer_r1) = R1;
    R0 = I0;
    R1 = _phr_tracks;
    R0 = R0 + R1;
    I0 = R0;
    R1 = DM(_phr_pointer_r1);
    R0 = DM(_phr_pointer_r0);
    JUMP _phr_tracks_back;

// Native sends have finished. Add our already-scaled, validated wet block to
// their return, then let native per-track compressor routing read our mirror.
_phr_master:
    DM(_fx_s_r0) = R0;
    R0 = I4;
    DM(_fx_s_i4) = R0;
    R0 = L4;
    DM(_fx_s_l4) = R0;
    L4 = 0;
    CALL _phr_fx_enter;
    I0 = _phr_wet;
    I1 = _fxs_return;
    LCNTR = 64, DO _phr_wet_add UNTIL LCE;
        R0 = DM(I0,M6);
        R1 = DM(I1,M5);
        F1 = F1 + F0;
_phr_wet_add: DM(I1,M6) = R1;
    CALL _phr_fx_leave;
    R0 = DM(_fx_s_l4);
    L4 = R0;
    R0 = DM(_fx_s_i4);
    I4 = R0;
    R0 = DM(_fx_s_r0);
    I13 = _phr_tracks;
    JUMP _phr_master_back;

_phr_recover:
    R0 = DM(_phr_fault_latched);
    R0 = PASS R0;
    IF EQ JUMP _phr_recovered;
    R0 = DM(_phrase_control_info + PH_C_OWNER_GENERATION);
    R1 = DM(_phr_fault_generation);
    COMP(R0,R1);
    IF NE JUMP _phr_recovered;
    R0 = DM(_phrase_control_info + PH_C_CMD_SEQ);
    R1 = DM(_phr_fault_cmd);
    COMP(R0,R1);
    IF NE JUMP _phr_recovered;
    R0 = DM(_phrase_control_info + PH_C_FADE);
    CALL _phr_unit;
    R0 = PASS R0;
    IF NE JUMP _phr_recover_raised;
    R1 = 1;
    DM(_phr_fault_zero) = R1;
    RTS;
_phr_recover_raised:
    R0 = DM(_phr_fault_zero);
    R0 = PASS R0;
    IF EQ RTS;
_phr_recovered:
    R0 = 0;
    DM(_phr_fault_latched) = R0;
    R0 = 1;
    RTS;

// Clamp a positive float to [0,1], fail closed on NaN, infinity or negatives.
_phr_unit:
    R1 = 0x7f800000;
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF EQ JUMP _phr_unit_zero;
    R0 = PASS R0;
    IF LT JUMP _phr_unit_zero;
    R1 = 0x3f800000;
    R0 = MIN(R0,R1);
    RTS;
_phr_unit_zero:
    R0 = 0;
    RTS;

// sin(pi*x/2) on[0,1]. Ninth-order Taylor (<4e-6 power error), with exact
// endpoints. It runs only twice a block, outside the audio loops.
_phr_sin:
    R0 = PASS R0;
    IF EQ RTS;
    R1 = 0x3f800000;
    COMP(R0,R1);
    IF EQ RTS;
    F2 = F0 * F0;
    R1 = 0x39283c1a;
    R3 = 0xbb996966;
    F1 = F1 * F2;
    F1 = F1 + F3;
    R3 = 0x3da335e3;
    F1 = F1 * F2;
    F1 = F1 + F3;
    R3 = 0xbf255de7;
    F1 = F1 * F2;
    F1 = F1 + F3;
    R3 = 0x3fc90fdb;
    F1 = F1 * F2;
    F1 = F1 + F3;
    F0 = F0 * F1;
    RTS;

// Sum current selected originals. Track main exclusions are observed; external
// raw capture is independent of native monitoring levels and enable flags.
_phr_capture:
    I0 = _phr_input;
    R0 = 0;
    LCNTR = 64, DO _phr_capture_zero UNTIL LCE;
_phr_capture_zero: DM(I0,M6) = R0;
    DM(_phr_index) = R0;
_phr_capture_source:
    R0 = DM(_phr_index);
    R1 = 1;
    R1 = LSHIFT R1 BY R0;
    R2 = DM(_phr_mask);
    R2 = R2 AND R1;
    IF EQ JUMP _phr_cap_zero_gain;
    R2 = 16;
    COMP(R0,R2);
    IF GE JUMP _phr_cap_gain;
    R2 = DM(_phr_main_exclude);
    R2 = R2 AND R1;
    IF NE JUMP _phr_cap_zero_gain;
_phr_cap_gain:
    R1 = LSHIFT R0 BY 2;
    R2 = PH_C_GAINS;
    R1 = R1 + R2;
    R2 = _phrase_control_info;
    R1 = R1 + R2;
    I0 = R1;
    R0 = DM(0,I0);
    CALL _phr_unit;
    JUMP _phr_cap_gain_ready;
_phr_cap_zero_gain:
    R0 = 0;
_phr_cap_gain_ready:
    R1 = DM(_phr_index);
    R1 = LSHIFT R1 BY 2;
    R2 = _phr_capture_prev;
    R1 = R1 + R2;
    I5 = R1;
    R4 = DM(0,I5);
    DM(0,I5) = R0;
    F5 = F0 - F4;
    R3 = 0x3d000000;
    F5 = F5 * F3;
    R0 = DM(_phr_index);
    R1 = 16;
    COMP(R0,R1);
    IF GE JUMP _phr_capture_external;
    R0 = LSHIFT R0 BY 8;
    R1 = _fxs_trackbus;
    R0 = R0 + R1;
    I0 = R0;
    I1 = _phr_input;
    CALL _phr_capture_channel;
    CALL _phr_capture_channel;
    JUMP _phr_cap_next;
_phr_capture_external:
    R0 = R0 - R1;
    R0 = LSHIFT R0 BY 2;
    R1 = DM(_phr_input_table);
    R0 = R0 + R1;
    I0 = R0;
    R0 = DM(0,I0);
    DM(_phr_external_ptr) = R0;
    I0 = R0;
    R1 = DM(_phr_mask);
    R2 = 0x30000;
    R1 = R1 AND R2;
    COMP(R1,R2);
    IF NE JUMP _phr_capture_mono;
    R0 = DM(_phr_index);
    R1 = 16;
    R0 = R0 - R1;
    R0 = LSHIFT R0 BY 7;
    R1 = _phr_input;
    R0 = R0 + R1;
    I1 = R0;
    CALL _phr_capture_channel;
    JUMP _phr_cap_next;
_phr_capture_mono:
    R3 = 0x3f3504f3;
    F4 = F4 * F3;
    F5 = F5 * F3;
    I1 = _phr_input;
    CALL _phr_capture_channel;
    R0 = DM(_phr_external_ptr);
    I0 = R0;
    CALL _phr_capture_channel;
_phr_cap_next:
    R0 = DM(_phr_index);
    R0 = R0 + 1;
    DM(_phr_index) = R0;
    R1 = 18;
    COMP(R0,R1);
    IF LT JUMP _phr_capture_source;
    RTS;

// I0 source/I1 summed capture channel; F4 old gain/F5 per-sample step.
// Advances each pointer32floats, preservesF4,F5 and caller index state.
_phr_capture_channel:
    R8 = PASS R4;
    LCNTR = 32, DO _phr_cap_samples UNTIL LCE;
        F8 = F8 + F5;
        R0 = DM(I0,M6);
        R1 = DM(I1,M5);
        F0 = F0 * F8;
        F1 = F1 + F0;
_phr_cap_samples: DM(I1,M6) = R1;
    RTS;

// Mirror the18sources, applying old/new held membership and the same dry
// curve to both native monitoring and future native send feeds.
_phr_shadows:
    R0 = 0;
    DM(_phr_index) = R0;
_phr_shadow_source:
    R0 = DM(_phr_index);
    R1 = 1;
    R1 = LSHIFT R1 BY R0;
    R2 = DM(_phr_old_mask);
    R2 = R2 AND R1;
    R4 = 0;
    R3 = 0x3f800000;
    IF NE R4 = PASS R3;
    R2 = DM(_phr_new_mask);
    R2 = R2 AND R1;
    R5 = 0;
    IF NE R5 = PASS R3;
    R2 = DM(_phr_mix);
    F5 = F5 - F4;
    F5 = F5 * F2;
    F5 = F5 + F4;
    R2 = DM(_phr_dry_target);
    F2 = F2 - F3;
    F0 = F2 * F5;
    F0 = F0 + F3;
    R1 = DM(_phr_index);
    R1 = LSHIFT R1 BY 2;
    R2 = _phr_dry_prev;
    R1 = R1 + R2;
    I5 = R1;
    R4 = DM(0,I5);
    DM(0,I5) = R0;
    F5 = F0 - F4;
    R3 = 0x3d000000;
    F5 = F5 * F3;
    R0 = DM(_phr_index);
    R1 = 16;
    COMP(R0,R1);
    IF GE JUMP _phr_shadow_external;
    R0 = LSHIFT R0 BY 8;
    R1 = _fxs_trackbus;
    R2 = R0 + R1;
    I0 = R2;
    R1 = _phr_tracks;
    R0 = R0 + R1;
    I1 = R0;
    CALL _phr_shadow_channel;
    CALL _phr_shadow_channel;
    JUMP _phr_shadow_next;
_phr_shadow_external:
    R0 = R0 - R1;
    R1 = LSHIFT R0 BY 2;
    R2 = DM(_phr_input_table);
    R1 = R1 + R2;
    I0 = R1;
    R1 = DM(0,I0);
    I0 = R1;
    R0 = LSHIFT R0 BY 7;
    R1 = _phr_external;
    R0 = R0 + R1;
    I1 = R0;
    CALL _phr_shadow_channel;
_phr_shadow_next:
    R0 = DM(_phr_index);
    R0 = R0 + 1;
    DM(_phr_index) = R0;
    R1 = 18;
    COMP(R0,R1);
    IF LT JUMP _phr_shadow_source;
    R0 = _phr_external;
    DM(_phr_ext_pointers) = R0;
    R1 = 128;
    R0 = R0 + R1;
    DM(_phr_ext_pointers + 4) = R0;
    RTS;

_phr_shadow_channel:
    R8 = PASS R4;
    LCNTR = 32, DO _phr_shadow_samples UNTIL LCE;
        F8 = F8 + F5;
        R0 = DM(I0,M6);
        F0 = F0 * F8;
_phr_shadow_samples: DM(I1,M6) = R0;
    RTS;

_phr_scale_wet:
    R0 = DM(_phr_wet_target);
    R4 = DM(_phr_wet_prev);
    DM(_phr_wet_prev) = R0;
    F5 = F0 - F4;
    R3 = 0x3d000000;
    F5 = F5 * F3;
    I0 = _phr_wet;
    I1 = _phr_wet;
    CALL _phr_shadow_channel;
    CALL _phr_shadow_channel;
    RTS;

// The router's own effect guard (`_phr_fx_enter`, `_phr_fx_exec`, `_phr_fx_leave`): see guard.inc.
#include "guard.inc"

.SECTION/DM seg_dmda;
.GLOBAL _phrase_route_info;
.VAR _phrase_route_info = 1;
.PKDATA _phr_tracks, 4096;
.PKDATA _phr_external, 256;
.PKDATA _phr_ext_pointers, 8;
.PKDATA _phr_input, 256;
.PKDATA _phr_wet, 256;
.PKDATA _phr_capture_prev, 72;
.PKDATA _phr_dry_prev, 72;
.PKDATA _phr_initialized, 4;
.PKDATA _phr_input_table, 4;
.PKDATA _phr_external_ptr, 4;
.PKDATA _phr_index, 4;
.PKDATA _phr_row, 4;
.PKDATA _phr_valid, 4;
.PKDATA _phr_mask, 4;
.PKDATA _phr_heartbeat, 4;
.PKDATA _phr_generation, 4;
.PKDATA _phr_fade, 4;
.PKDATA _phr_mix, 4;
.PKDATA _phr_old_mask, 4;
.PKDATA _phr_new_mask, 4;
.PKDATA _phr_dry_target, 4;
.PKDATA _phr_wet_target, 4;
.PKDATA _phr_wet_prev, 4;
.PKDATA _phr_pointer_r0, 4;
.PKDATA _phr_pointer_r1, 4;
.PKDATA _phr_fault_latched, 4;
.PKDATA _phr_fault_cmd, 4;
.PKDATA _phr_fault_generation, 4;
.PKDATA _phr_fault_zero, 4;
