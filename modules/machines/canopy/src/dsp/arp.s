// CPS5 movement scheduler. Runs only at the audio process boundary.
// R4=768-byte instance, R8=V2/V3 input. Returns active(1)/quiet(0), negative fault.
// State words136..143: one arp group;144..145:blend;146:phase;147:enabled;
// 148:ordinal;149:token serial;150..153:pending token/pitch/velocity/bend.
// Four candidates come from logical foreground/pending pad ownership, never
// the two release tails. Replacement ramps the arp group for <=64 samples.
.SECTION/PM seg_pmco;
.GLOBAL _ca_process;
.EXTERN _cp_render_one;
.EXTERN _cp_start_one;
.EXTERN _cp_retune;
.EXTERN _cp_blend;
_ca_process:
    DM(_ca_state) = R4;
    DM(_ca_input) = R8;
    R0 = 544;
    R0 = R4 + R0;
    DM(_ca_voice) = R0;
    I2 = R8;
    R0 = 0;
    DM(_ca_order) = R0;
    DM(_ca_swing) = R0;
    DM(_ca_clock) = R0;
    DM(_ca_restart) = R0;
    R0 = 1;
    DM(_ca_octaves) = R0;
    R0 = DM(10, I2);
    R1 = 16;
    COMP(R0, R1);
    IF EQ JUMP _arp_full_controls;
    R1 = 32;
    COMP(R0, R1);
    IF NE JUMP _ca_legacy;
_arp_full_controls:
    R0 = DM(9, I2);
    I1 = R0;
    R0 = DM(10, I1);
    DM(_ca_order) = R0;
    R0 = DM(11, I1);
    DM(_ca_octaves) = R0;
    R0 = DM(12, I1);
    DM(_ca_swing) = R0;
    R0 = DM(13, I1);
    DM(_ca_clock) = R0;
    R0 = DM(14, I1);
    DM(_ca_restart) = R0;
    JUMP _ca_configured;
_ca_legacy:
    R1 = 10;
    COMP(R0, R1);
    IF NE JUMP _ca_disable;
_ca_configured:
    R0 = DM(9, I2);
    DM(_ca_controls) = R0;
    I1 = R0;
    R0 = DM(6, I1);
    F0 = FLOAT R0;
    F1 = 0.007874015748;
    F0 = F0 * F1;
    R1 = DM(_ca_voice);
    I2 = R1;
    DM(9, I2) = F0;
    R0 = DM(6, I1);
    R0 = PASS R0;
    IF EQ JUMP _ca_disable_clock;
    CALL _ca_candidates;
    CALL _ca_membership;
    CALL _ca_expand;
    R0 = DM(_ca_count);
    R0 = PASS R0;
    IF NE JUMP _ca_have_members;
    // CONTINUE retains the running phase even through an empty chord.
    // Membership already cancelled pending onsets; silence the current group.
    R0 = DM(_ca_restart);
    R1 = 2;
    COMP(R0, R1);
    IF NE JUMP _ca_disable_clock;
    R0 = DM(_ca_voice);
    I2 = R0;
    R0 = 2;
    DM(1, I2) = R0;
_ca_have_members:
    R0 = DM(_ca_voice);
    I2 = R0;
    CALL _ca_clock_config;
    R0 = DM(_ca_clock);
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _ca_master;
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ca_fine;
    R0 = DM(11, I2);
    R0 = PASS R0;
    IF EQ JUMP _ca_first;
    R0 = DM(_ca_controls);
    I1 = R0;
    R0 = DM(7, I1);
    R1 = DM(8, I1);
    F0 = FLOAT R0;
    F1 = FLOAT R1;
    F0 = F0 * F1;
    R0 = FIX F0;
    R1 = DM(10, I2);
    R0 = R0 + R1;
_ca_phase_ready:
    DM(_ca_phase) = R0;
    CALL _ca_threshold;
    R1 = R0;
    R0 = DM(_ca_phase);
    COMP(R0, R1);
    IF GE JUMP _ca_due;
    DM(10, I2) = R0;
    F1 = FLOAT R1;
    R0 = DM(9, I1);
    F0 = FLOAT R0;
    F2 = 0.01;
    F0 = F0 * F2;
    F0 = F0 * F1;
    R0 = FIX F0;
    R1 = DM(10, I2);
    COMP(R1, R0);
    IF LT JUMP _ca_render;
    R0 = DM(1, I2);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ca_render;
    R0 = 0;
    DM(1, I2) = R0;
    JUMP _ca_render;
_ca_first:
    R0 = 1;
    DM(11, I2) = R0;
    R0 = DM(_ca_restart);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ca_first_continue;
    R0 = 0;
    DM(12, I2) = R0;
_ca_first_continue:
    R0 = 0;
    DM(22, I2) = R0;
    R1 = DM(_ca_swing);
    DM(23, I2) = R1;
    JUMP _ca_schedule;
_ca_due:
    R0 = R0 - R1;
    R2 = DM(22, I2);
    R3 = 1;
    R2 = R2 XOR R3;
    DM(22, I2) = R2;
    R2 = PASS R2;
    IF NE JUMP _ca_schedule;
    R2 = DM(_ca_swing);
    DM(23, I2) = R2;
_ca_schedule:
    DM(10, I2) = R0;
    R0 = DM(_ca_count);
    R0 = PASS R0;
    IF EQ JUMP _ca_render;
    CALL _ca_select;
    R0 = LSHIFT R0 BY 5;
    R1 = _ca_notes;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(13, I2);
    R0 = R0 + 1;
    IF EQ JUMP _ca_fault;
    DM(13, I2) = R0;
    DM(14, I2) = R0;
    R0 = DM(0, I1);
    DM(15, I2) = R0;
    R1 = DM(4, I1);
    R0 = R0 - R1;
    DM(20, I2) = R0;
    R0 = DM(1, I1);
    DM(16, I2) = R0;
    R0 = DM(3, I1);
    DM(17, I2) = R0;
    R0 = DM(2, I1);
    DM(18, I2) = R0;
    CALL _ca_save_tail;
    R0 = 2;
    DM(1, I2) = R0;
    JUMP _ca_render;
_ca_disable:
    R0 = DM(_ca_voice);
    I2 = R0;
    R0 = 0;
    DM(9, I2) = R0;
_ca_disable_clock:
    R0 = DM(_ca_state);
    R1 = 128;
    R0 = R0 + R1;
    I0 = R0;
    R2 = 2;
_ca_stop_tails:
    R0 = DM(1, I0);
    R1 = 4;
    R0 = R0 AND R1;
    IF EQ JUMP _ca_stop_next;
    R0 = 6;
    DM(1, I0) = R0;
_ca_stop_next:
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ca_stop_tails;
    R0 = DM(_ca_voice);
    I2 = R0;
    R0 = 0;
    DM(11, I2) = R0;
    DM(14, I2) = R0;
    R0 = 2;
    DM(1, I2) = R0;
_ca_render:
    CALL _ca_try_start;
    R4 = DM(_ca_voice);
    R8 = _ca_pcm;
    CALL _cp_render_one;
    DM(_ca_active) = R0;
    CALL _ca_try_start;
    R1 = DM(_ca_active);
    R0 = R0 OR R1;
    DM(_ca_active) = R0;
    CALL _ca_render_tails;
    R0 = DM(_ca_input);
    I0 = R0;
    R4 = DM(5, I0);
    R8 = _ca_pcm;
    R0 = DM(_ca_voice);
    R1 = 32;
    R12 = R0 + R1;
    CALL _cp_blend;
    R0 = PASS R0;
    IF EQ JUMP _ca_fault;
    R0 = DM(_ca_active);
    RTS;
_ca_fault:
    R0 = -1;
    RTS;
_ca_try_start:
    R4 = DM(_ca_voice);
    I2 = R4;
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF NE JUMP _ca_no_start;
    R8 = DM(14, I2);
    R8 = PASS R8;
    IF EQ JUMP _ca_no_start;
    R12 = DM(15, I2);
    R9 = DM(16, I2);
    CALL _cp_start_one;
    R0 = PASS R0;
    IF EQ RTS;
    R0 = DM(_ca_voice);
    I2 = R0;
    I0 = R0;
    R12 = DM(15, I2);
    R9 = DM(17, I2);
    CALL _cp_retune;
    R0 = DM(_ca_voice);
    I2 = R0;
    R0 = 0;
    DM(14, I2) = R0;
    R0 = DM(18, I2);
    DM(19, I2) = R0;
    R0 = DM(20, I2);
    DM(21, I2) = R0;
    R0 = 1;
    RTS;
_ca_no_start:
    R0 = 0;
    RTS;
// A delayed onset cannot outlive its candidate. Live bend follows the active
// candidate without changing its oscillator phase or envelope.
_ca_membership:
    R0 = DM(_ca_voice);
    I2 = R0;
    I1 = _ca_notes;
    R10 = DM(_ca_count);
    R11 = 0;
    R12 = 0;
    R10 = PASS R10;
    IF EQ JUMP _ca_membership_done;
_ca_member:
    R0 = DM(2, I1);
    R1 = DM(18, I2);
    COMP(R0, R1);
    R1 = 1;
    IF EQ R11 = R1;
    R1 = DM(19, I2);
    COMP(R0, R1);
    IF NE JUMP _ca_member_next;
    R12 = 1;
    R0 = DM(0, I1);
    DM(_ca_bend_note) = R0;
    R0 = DM(3, I1);
    DM(_ca_bend_value) = R0;
_ca_member_next:
    MODIFY(I1, 8) (NW);
    R10 = R10 - 1;
    IF NE JUMP _ca_member;
_ca_membership_done:
    R11 = PASS R11;
    IF NE JUMP _ca_member_bend;
    R0 = 0;
    DM(14, I2) = R0;
_ca_member_bend:
    R12 = PASS R12;
    IF EQ RTS;
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF EQ RTS;
    I0 = I2;
    R12 = DM(_ca_bend_note);
    R0 = DM(21, I2);
    R12 = R12 + R0;
    R9 = DM(_ca_bend_value);
    CALL _cp_retune;
    RTS;
_ca_candidates:
    R0 = 0;
    DM(_ca_count) = R0;
    DM(_ca_slot) = R0;
_ca_candidate:
    R0 = DM(_ca_slot);
    R1 = LSHIFT R0 BY 5;
    R2 = DM(_ca_state);
    R1 = R1 + R2;
    I0 = R1;
    R1 = LSHIFT R0 BY 4;
    R3 = 192;
    R1 = R1 + R3;
    R1 = R1 + R2;
    I1 = R1;
    R8 = DM(0, I1);
    R8 = PASS R8;
    IF NE JUMP _ca_pending_candidate;
    R0 = DM(1, I0);
    R1 = 7;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _ca_candidate_next;
    R8 = DM(0, I0);
    R8 = PASS R8;
    IF EQ JUMP _ca_candidate_next;
    F0 = DM(6, I0);
    F1 = 127.0;
    F0 = F0 * F1;
    R9 = FIX F0;
    JUMP _ca_find_pitch;
_ca_pending_candidate:
    R9 = DM(2, I1);
_ca_find_pitch:
    R0 = DM(_ca_state);
    R1 = 292; // controller bank + nine-word owner-table prefix
    R0 = R0 + R1;
    I0 = R0;
    R10 = 10;
_ca_find_owner:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _ca_found_owner;
    MODIFY(I0, 2) (NW);
    R10 = R10 - 1;
    IF NE JUMP _ca_find_owner;
    JUMP _ca_candidate_next;
_ca_found_owner:
    R0 = DM(1, I0);
    R12 = FEXT R0 BY 8:7;
    R0 = FEXT R0 BY 0:5;
    R0 = LSHIFT R0 BY 2;
    R1 = 372; // controller bank + 29 route-bend words
    R0 = R0 + R1;
    R1 = DM(_ca_state);
    R0 = R0 + R1;
    I0 = R0;
    R11 = DM(0, I0);
    R0 = DM(_ca_count);
    R1 = LSHIFT R0 BY 5;
    R2 = _ca_notes;
    R1 = R1 + R2;
    I1 = R1;
    DM(0, I1) = R12;
    DM(4, I1) = R12;
    DM(1, I1) = R9;
    DM(2, I1) = R8;
    DM(3, I1) = R11;
    R0 = R0 + 1;
    DM(_ca_count) = R0;
_ca_candidate_next:
    R0 = DM(_ca_slot);
    R0 = R0 + 1;
    DM(_ca_slot) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _ca_candidate;
    RTS;
// Expand at most four admitted notes to at most twelve octave candidates.
_ca_expand:
    R0 = DM(_ca_count);
    DM(_ca_base_count) = R0;
    R10 = 1;
_ca_octave:
    R0 = DM(_ca_octaves);
    COMP(R10, R0);
    IF GE JUMP _ca_sort;
    R11 = DM(_ca_base_count);
    I0 = _ca_notes;
_ca_expand_note:
    R11 = PASS R11;
    IF EQ JUMP _ca_next_octave;
    R0 = LSHIFT R10 BY 3;
    R1 = LSHIFT R10 BY 2;
    R0 = R0 + R1;
    R1 = DM(0, I0);
    R12 = R0 + R1;
    R1 = 127;
    COMP(R12, R1);
    IF GT JUMP _ca_expand_next;
    R0 = DM(_ca_count);
    R1 = LSHIFT R0 BY 5;
    R2 = _ca_notes;
    R1 = R1 + R2;
    I1 = R1;
    DM(0, I1) = R12;
    R1 = DM(1, I0);
    DM(1, I1) = R1;
    R1 = DM(2, I0);
    DM(2, I1) = R1;
    R1 = DM(3, I0);
    DM(3, I1) = R1;
    R1 = DM(4, I0);
    DM(4, I1) = R1;
    R0 = R0 + 1;
    DM(_ca_count) = R0;
_ca_expand_next:
    MODIFY(I0, 8) (NW);
    R11 = R11 - 1;
    JUMP _ca_expand_note;
_ca_next_octave:
    R10 = R10 + 1;
    JUMP _ca_octave;
_ca_sort:
    R10 = DM(_ca_count);
_ca_sort_pass:
    I0 = _ca_notes;
    R11 = DM(_ca_count);
    R11 = R11 - 1;
    IF LE RTS;
_ca_sort_pair:
    R0 = DM(_ca_order);
    R1 = 3;
    COMP(R0, R1);
    IF EQ JUMP _ca_sort_admission;
    R0 = DM(0, I0);
    R1 = DM(8, I0);
    JUMP _ca_sort_compare;
_ca_sort_admission:
    // Octaves ascend, admission order within each octave. Token serials
    // are unsigned and never wrap within an ownership generation.
    R0 = DM(0, I0);
    R1 = DM(4, I0);
    R0 = R0 - R1;
    R1 = DM(8, I0);
    R2 = DM(12, I0);
    R1 = R1 - R2;
    COMP(R0, R1);
    IF NE JUMP _ca_sort_compare;
    R0 = DM(2, I0);
    R1 = DM(10, I0);
    R2 = 0x80000000;
    R0 = R0 XOR R2;
    R1 = R1 XOR R2;
_ca_sort_compare:
    COMP(R0, R1);
    IF LE JUMP _ca_sorted;
    R12 = 5;
_ca_swap:
    R0 = DM(0, I0);
    R1 = DM(8, I0);
    DM(0, I0) = R1;
    DM(8, I0) = R0;
    MODIFY(I0, 1) (NW);
    R12 = R12 - 1;
    IF NE JUMP _ca_swap;
    MODIFY(I0, -5) (NW);
_ca_sorted:
    MODIFY(I0, 8) (NW);
    R11 = R11 - 1;
    IF NE JUMP _ca_sort_pair;
    R10 = R10 - 1;
    IF NE JUMP _ca_sort_pass;
    RTS;
// Phase units are BPM*notes/quarter per block. Pair thresholds sum to 180000.
// Swing changes are latched only on complete pair boundaries.
_ca_threshold:
    R0 = DM(23, I2);
    R1 = DM(22, I2);
    R1 = PASS R1;
    IF NE R0 = -R0;
    R1 = 100;
    R0 = R0 + R1;
    R1 = DM(_ca_clock);
    R2 = 2;
    COMP(R1, R2);
    IF EQ JUMP _ca_phase_threshold;
    F0 = FLOAT R0;
    F1 = 900.0;
    F0 = F0 * F1;
    R0 = FIX F0;
    RTS;
_ca_phase_threshold:
    R1 = 216000;
    R0 = R0 * R1 (SSI);
    RTS;
// R0 returns a bounded candidate index, I2 remains the arp instance.
_ca_select:
    R0 = DM(12, I2);
    R1 = DM(_ca_count);
    R2 = DM(_ca_order);
    R3 = 2;
    COMP(R2, R3);
    IF NE JUMP _ca_select_wrap;
    R3 = 1;
    COMP(R1, R3);
    IF LE JUMP _ca_select_wrap;
    R1 = LSHIFT R1 BY 1;
    R3 = 2;
    R1 = R1 - R3;
_ca_select_wrap:
    COMP(R0, R1);
    IF LT JUMP _ca_selected;
    R0 = R0 - R1;
    JUMP _ca_select_wrap;
_ca_selected:
    R3 = R0 + 1;
    DM(12, I2) = R3;
    R3 = 1;
    COMP(R2, R3);
    IF NE JUMP _ca_select_ping;
    R0 = R1 - R0;
    R0 = R0 - 1;
    RTS;
_ca_select_ping:
    R3 = 2;
    COMP(R2, R3);
    IF NE JUMP _ca_select_random;
    R2 = DM(_ca_count);
    COMP(R0, R2);
    IF LT RTS;
    R0 = R1 - R0;
    RTS;
_ca_select_random:
    R3 = 4;
    COMP(R2, R3);
    IF NE RTS;
    // Counter-based deterministic hash, no global random state. The
    // modulo loop is bounded by first reducing to eight unsigned bits.
    R0 = DM(13, I2);
    R0 = R0 + 1;
    R2 = LSHIFT R0 BY 13;
    R0 = R0 XOR R2;
    R2 = LSHIFT R0 BY -17;
    R0 = R0 XOR R2;
    R2 = LSHIFT R0 BY 5;
    R0 = R0 XOR R2;
    R2 = 255;
    R0 = R0 AND R2;
_ca_random_wrap:
    COMP(R0, R1);
    IF LT RTS;
    R0 = R0 - R1;
    JUMP _ca_random_wrap;
// Clock/rate changes start a new timing observation without replaying keys.
_ca_clock_config:
    R0 = DM(_ca_clock);
    R0 = R0 + 1;
    R1 = DM(24, I2);
    COMP(R0, R1);
    IF NE JUMP _ca_clock_changed;
    R1 = DM(_ca_controls);
    I1 = R1;
    R1 = DM(8, I1);
    R2 = DM(25, I2);
    COMP(R1, R2);
    IF EQ RTS;
_ca_clock_changed:
    DM(24, I2) = R0;
    R1 = DM(_ca_controls);
    I1 = R1;
    R1 = DM(8, I1);
    DM(25, I2) = R1;
    R0 = 0;
    DM(11, I2) = R0;
    DM(14, I2) = R0;
    DM(26, I2) = R0;
    DM(30, I2) = R0;
    DM(32, I2) = R0;
    R0 = 2;
    DM(1, I2) = R0;
    RTS;
// MASTER uses committed boundaries, not a guessed BPM or scheduler deadline.
// Observe one complete interval before the first onset. Lost/missed frames or
// boundaries invalidate the estimate; recovery requires a fresh interval.
_ca_master:
    R0 = DM(_ca_input);
    I1 = R0;
    R0 = DM(12, I1);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _ca_master_lost;
    R0 = DM(26, I2);
    R0 = PASS R0;
    IF EQ JUMP _ca_master_observe;
    R0 = DM(13, I1);
    R1 = DM(27, I2);
    COMP(R0, R1);
    IF NE JUMP _ca_master_observe;
    R0 = DM(14, I1);
    R1 = DM(28, I2);
    R0 = R0 - R1;
    IF EQ JUMP _ca_master_between;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _ca_master_observe;
    R0 = DM(4, I1);
    R1 = DM(29, I2);
    R0 = R0 - R1;
    R0 = PASS R0;
    IF LE JUMP _ca_master_lost;
    R1 = 12000;
    COMP(R0, R1);
    IF GT JUMP _ca_master_observe;
    DM(30, I2) = R0;
    R0 = DM(4, I1);
    DM(29, I2) = R0;
    R0 = DM(14, I1);
    DM(28, I2) = R0;
    R0 = 2;
    DM(34, I2) = R0;
    R0 = DM(_ca_restart);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _ca_master_count;
    R0 = DM(15, I1);
    R0 = PASS R0;
    IF NE JUMP _ca_master_count;
    R0 = 0;
    DM(12, I2) = R0;
    DM(11, I2) = R0;
_ca_master_count:
    R0 = DM(11, I2);
    R0 = PASS R0;
    IF EQ JUMP _ca_master_onset;
    R0 = DM(32, I2);
    R0 = R0 + 1;
    DM(32, I2) = R0;
    R1 = DM(25, I2);
    COMP(R0, R1);
    IF LT JUMP _ca_master_between;
_ca_master_onset:
    R0 = 0;
    DM(32, I2) = R0;
    R0 = DM(4, I1);
    DM(31, I2) = R0;
    R0 = DM(11, I2);
    R0 = PASS R0;
    IF NE JUMP _ca_master_schedule;
    R0 = DM(_ca_restart);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ca_master_schedule;
    R0 = 0;
    DM(12, I2) = R0;
_ca_master_schedule:
    R0 = 1;
    DM(11, I2) = R0;
    R0 = 0;
    JUMP _ca_schedule;
_ca_master_between:
    R0 = DM(4, I1);
    R1 = DM(29, I2);
    R0 = R0 - R1;
    R1 = DM(30, I2);
    R1 = LSHIFT R1 BY 1;
    R1 = PASS R1;
    IF EQ JUMP _ca_render; // still waiting for the first complete interval
    COMP(R0, R1);
    IF GT JUMP _ca_master_lost;
    R0 = DM(4, I1);
    R1 = DM(31, I2);
    R10 = R0 - R1;
    R2 = DM(30, I2);
    F0 = FLOAT R2;
    R2 = DM(25, I2);
    F1 = FLOAT R2;
    F0 = F0 * F1;
    R2 = DM(_ca_controls);
    I1 = R2;
    R2 = DM(9, I1);
    F1 = FLOAT R2;
    F0 = F0 * F1;
    F1 = 0.01;
    F0 = F0 * F1;
    R0 = FIX F0;
    R1 = 1;
    R0 = MAX(R0, R1);
    COMP(R10, R0);
    IF LT JUMP _ca_render;
    R0 = DM(1, I2);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ca_render;
    R0 = 0;
    DM(1, I2) = R0;
    JUMP _ca_render;
_ca_master_observe:
    R0 = DM(13, I1);
    DM(27, I2) = R0;
    R0 = DM(14, I1);
    DM(28, I2) = R0;
    R0 = DM(4, I1);
    DM(29, I2) = R0;
    R0 = 1;
    DM(26, I2) = R0;
    DM(34, I2) = R0;
    R0 = 0;
    DM(30, I2) = R0;
    JUMP _ca_disable_clock;
_ca_master_lost:
    R0 = 0;
    DM(26, I2) = R0;
    DM(30, I2) = R0;
    R0 = 3;
    DM(34, I2) = R0;
    JUMP _ca_disable_clock;
// Synchronized fine clock: firmware phase is 21600000 ticks/quarter.
// Integer multiply/carry preserves triplet and other rational divisions.
// State 169 tracks the highest observed phase; minor external-clock backward
// corrections wait to catch up instead of counting the same time twice.
_ca_fine:
    R0 = DM(_ca_input);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _ca_master_lost; // V5 retains the V4 phase fields.
    R0 = DM(12, I1);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _ca_master_lost;
    R0 = DM(26, I2);
    R0 = PASS R0;
    IF EQ JUMP _ca_fine_observe;
    R0 = DM(13, I1);
    R1 = DM(27, I2);
    COMP(R0, R1);
    IF NE JUMP _ca_fine_observe;
    R0 = DM(14, I1);
    R1 = DM(28, I2);
    R0 = R0 - R1;
    IF EQ JUMP _ca_fine_between;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _ca_fine_observe;
    R0 = DM(4, I1);
    R1 = DM(29, I2);
    R0 = R0 - R1;
    IF LE JUMP _ca_master_lost;
    R1 = 12000;
    COMP(R0, R1);
    IF GT JUMP _ca_fine_observe;
    DM(30, I2) = R0;
    R0 = DM(4, I1);
    DM(29, I2) = R0;
    R0 = DM(14, I1);
    DM(28, I2) = R0;
    R0 = 2;
    DM(34, I2) = R0;
    R0 = DM(11, I2);
    R0 = PASS R0;
    IF NE JUMP _ca_fine_between;
    // First onset waits for a fresh committed boundary. Its overshoot is
    // retained, so subsequent onsets stay on that boundary's musical grid.
    R0 = DM(20, I1);
    DM(33, I2) = R0;
    R1 = DM(21, I1);
    R0 = R0 - R1;
    IF LT JUMP _ca_fine_observe;
    R1 = 900000;
    COMP(R0, R1);
    IF GT JUMP _ca_fine_observe;
    R1 = DM(25, I2);
    R0 = R0 * R1 (SSI);
    DM(_ca_phase) = R0;
    R0 = DM(_ca_restart);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ca_fine_begin;
    R0 = 0;
    DM(12, I2) = R0;
_ca_fine_begin:
    R0 = 1;
    DM(11, I2) = R0;
    R0 = 0;
    DM(22, I2) = R0;
    R0 = DM(_ca_swing);
    DM(23, I2) = R0;
    R0 = DM(_ca_phase);
    JUMP _ca_schedule;
_ca_fine_between:
    R0 = DM(4, I1);
    R1 = DM(29, I2);
    R0 = R0 - R1;
    R1 = DM(30, I2);
    R1 = PASS R1;
    IF EQ JUMP _ca_render;
    R1 = LSHIFT R1 BY 1;
    COMP(R0, R1);
    IF GT JUMP _ca_master_lost;
    R0 = DM(11, I2);
    R0 = PASS R0;
    IF EQ JUMP _ca_render;
    R0 = DM(20, I1);
    R1 = DM(33, I2);
    R2 = R0 - R1;
    IF LT JUMP _ca_render;
    // More than one MIDI-clock tick is a discontinuity, not a burst of
    // overdue notes. Reobserve instead of catching up audibly.
    R1 = 900000;
    COMP(R2, R1);
    IF GT JUMP _ca_fine_observe;
    DM(33, I2) = R0;
    R1 = DM(25, I2);
    R0 = R2 * R1 (SSI);
    R1 = DM(10, I2);
    R0 = R0 + R1;
    // The shared phase gate uses controls through I1.
    R1 = DM(_ca_controls);
    I1 = R1;
    JUMP _ca_phase_ready;
_ca_fine_observe:
    R0 = DM(20, I1);
    DM(33, I2) = R0;
    JUMP _ca_master_observe;

// Share exactly the same two tail slots as the four pad foreground owners.
// If neither is free, keep the existing bounded same-slot steal ramp.
_ca_save_tail:
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = DM(7, I2);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = DM(_ca_state);
    R1 = 128;
    R0 = R0 + R1;
    I0 = R0;
    R2 = 2;
_ca_tail_find:
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF EQ JUMP _ca_tail_copy;
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ca_tail_find;
    RTS;
_ca_tail_copy:
    R10 = I0;
    R0 = DM(_ca_voice);
    I1 = R0;
    R2 = 8;
_ca_tail_word:
    R0 = DM(I1, M6);
    DM(I0, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ca_tail_word;
    I0 = R10;
    R0 = 4;
    DM(1, I0) = R0;
    R0 = 0;
    DM(0, I2) = R0;
    RTS;
_ca_render_tails:
    R0 = 0;
    DM(_ca_tail_index) = R0;
_ca_tail_render:
    R0 = DM(_ca_tail_index);
    R0 = LSHIFT R0 BY 5;
    R1 = 128;
    R0 = R0 + R1;
    R1 = DM(_ca_state);
    R4 = R0 + R1;
    I0 = R4;
    R0 = DM(1, I0);
    R1 = 4;
    R0 = R0 AND R1;
    IF EQ JUMP _ca_tail_next;
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF EQ JUMP _ca_tail_next;
    R8 = _ca_tail_pcm;
    CALL _cp_render_one;
    R1 = DM(_ca_active);
    R0 = R0 OR R1;
    DM(_ca_active) = R0;
    I0 = _ca_tail_pcm;
    I1 = _ca_pcm;
    R2 = 32;
_ca_tail_mix:
    F0 = DM(I0, M6);
    F1 = DM(I1, M5);
    F0 = F0 + F1;
    DM(I1, M6) = F0;
    R2 = R2 - 1;
    IF NE JUMP _ca_tail_mix;
_ca_tail_next:
    R0 = DM(_ca_tail_index);
    R0 = R0 + 1;
    DM(_ca_tail_index) = R0;
    R1 = 2;
    COMP(R0, R1);
    IF LT JUMP _ca_tail_render;
    RTS;
.PKBSS _ca_state, 4;
.PKBSS _ca_input, 4;
.PKBSS _ca_voice, 4;
.PKBSS _ca_controls, 4;
.PKBSS _ca_count, 4;
.PKBSS _ca_slot, 4;
.PKBSS _ca_active, 4;
.PKBSS _ca_notes, 384;
.PKBSS _ca_pcm, 128;

.PKBSS _ca_bend_note, 4;
.PKBSS _ca_bend_value, 4;

.PKBSS _ca_order, 4;
.PKBSS _ca_octaves, 4;
.PKBSS _ca_swing, 4;
.PKBSS _ca_base_count, 4;
.PKBSS _ca_phase, 4;


.PKBSS _ca_tail_index, 4;
.PKBSS _ca_tail_pcm, 128;

.PKBSS _ca_clock, 4;
.PKBSS _ca_restart, 4;
