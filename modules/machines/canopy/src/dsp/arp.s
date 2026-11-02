// CPS4 local-clock UP arpeggiator. Runs only at the audio process boundary.
// R4=640-byte instance, R8=V2 input. Returns active(1)/quiet(0), negative fault.
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
    R0 = DM(10, I2);
    R1 = 10;
    COMP(R0, R1);
    IF NE JUMP _ca_disable;
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
    R0 = DM(_ca_count);
    R0 = PASS R0;
    IF EQ JUMP _ca_disable_clock;
    R0 = DM(_ca_voice);
    I2 = R0;
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
    R1 = 90000;
    COMP(R0, R1);
    IF GE JUMP _ca_due;
    DM(10, I2) = R0;
    R0 = DM(9, I1);
    F0 = FLOAT R0;
    F1 = 900.0;
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
    R0 = 0;
    DM(12, I2) = R0;
    JUMP _ca_schedule;
_ca_due:
    R0 = R0 - R1;
_ca_schedule:
    DM(10, I2) = R0;
    R0 = DM(12, I2);
    R1 = DM(_ca_count);
_ca_wrap:
    COMP(R0, R1);
    IF LT JUMP _ca_index;
    R0 = R0 - R1;
    JUMP _ca_wrap;
_ca_index:
    R1 = R0 + 1;
    DM(12, I2) = R1;
    R0 = LSHIFT R0 BY 4;
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
    R0 = DM(1, I1);
    DM(16, I2) = R0;
    R0 = DM(3, I1);
    DM(17, I2) = R0;
    R0 = DM(2, I1);
    DM(18, I2) = R0;
    R0 = 2;
    DM(1, I2) = R0;
    JUMP _ca_render;
_ca_disable:
    R0 = DM(_ca_voice);
    I2 = R0;
    R0 = 0;
    DM(9, I2) = R0;
_ca_disable_clock:
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
    MODIFY(I1, 4) (NW);
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
    R1 = LSHIFT R0 BY 4;
    R2 = _ca_notes;
    R1 = R1 + R2;
    I1 = R1;
    DM(0, I1) = R12;
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
    R10 = 3;
_ca_sort_pass:
    I0 = _ca_notes;
    R11 = DM(_ca_count);
    R11 = R11 - 1;
    IF LE RTS;
_ca_sort_pair:
    R0 = DM(0, I0);
    R1 = DM(4, I0);
    COMP(R0, R1);
    IF LE JUMP _ca_sorted;
    R2 = DM(1, I0);
    R3 = DM(5, I0);
    R4 = DM(2, I0);
    R5 = DM(6, I0);
    R6 = DM(3, I0);
    R7 = DM(7, I0);
    DM(0, I0) = R1;
    DM(4, I0) = R0;
    DM(1, I0) = R3;
    DM(5, I0) = R2;
    DM(2, I0) = R5;
    DM(6, I0) = R4;
    DM(3, I0) = R7;
    DM(7, I0) = R6;
_ca_sorted:
    MODIFY(I0, 4) (NW);
    R11 = R11 - 1;
    IF NE JUMP _ca_sort_pair;
    R10 = R10 - 1;
    IF NE JUMP _ca_sort_pass;
    RTS;
.PKBSS _ca_state, 4;
.PKBSS _ca_input, 4;
.PKBSS _ca_voice, 4;
.PKBSS _ca_controls, 4;
.PKBSS _ca_count, 4;
.PKBSS _ca_slot, 4;
.PKBSS _ca_active, 4;
.PKBSS _ca_notes, 64;
.PKBSS _ca_pcm, 128;

.PKBSS _ca_bend_note, 4;
.PKBSS _ca_bend_value, 4;
