// Audio-block strum from committed mapper metadata, never physical keys.
// State words174/175 phrase/metadata;176..183 token/countdown;184 running;
// 185 pattern,186 run generation,187 boundary. No extra oscillator bank.
.SECTION/PM seg_pmco;
.GLOBAL _cs_prepare;
.GLOBAL _cs_restore;
_cs_prepare:
    DM(_cs_state) = R4;
    DM(_cs_input) = R8;
    I0 = _cs_hidden;
    R0 = 0;
    DM(0, I0) = R0;
    DM(1, I0) = R0;
    DM(2, I0) = R0;
    DM(3, I0) = R0;
    DM(_cs_strum) = R0;
    DM(_cs_restart) = R0;
    DM(_cs_stop) = R0;
    I0 = R8;
    R0 = DM(10, I0);
    R1 = 16;
    COMP(R0, R1);
    IF EQ JUMP _strum_full_controls;
    R1 = 32;
    COMP(R0, R1);
    IF NE JUMP _cs_tick_begin;
_strum_full_controls:
    R0 = DM(9, I0);
    I1 = R0;
    R0 = DM(15, I1);
    DM(_cs_strum) = R0;
    R0 = DM(14, I1);
    DM(_cs_restart) = R0;
    R0 = DM(0, I0);
    R1 = 3;
    COMP(R0, R1);
    IF LT JUMP _cs_tick_begin;
    R0 = DM(18, I0);
    I2 = R4;
    DM(175, I2) = R0;
    R1 = 0x7fffffff;
    R0 = R0 AND R1;
    R1 = DM(174, I2);
    COMP(R0, R1);
    IF EQ JUMP _cs_transport;
    DM(174, I2) = R0;
    R0 = DM(_cs_restart);
    R0 = PASS R0;
    IF NE JUMP _cs_transport;
    CALL _cs_restart_arp;
_cs_transport:
    R0 = DM(12, I0);
    R1 = 2;
    R0 = R0 AND R1;
    R1 = DM(184, I2);
    DM(184, I2) = R0;
    R0 = PASS R0;
    IF NE JUMP _cs_pattern;
    DM(_cs_stop) = R1;
_cs_pattern:
    R0 = DM(12, I0);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _cs_note_batch;
    R0 = DM(_cs_restart);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _cs_note_batch;
    R0 = DM(19, I0);
    R1 = DM(185, I2);
    DM(185, I2) = R0;
    COMP(R0, R1);
    IF NE JUMP _cs_pattern_restart;
    R0 = DM(13, I0);
    R1 = DM(186, I2);
    COMP(R0, R1);
    IF NE JUMP _cs_pattern_restart;
    R0 = DM(14, I0);
    R1 = DM(187, I2);
    COMP(R0, R1);
    IF EQ JUMP _cs_note_batch;
    DM(187, I2) = R0;
    R0 = DM(15, I0);
    R0 = PASS R0;
    IF NE JUMP _cs_note_batch;
_cs_pattern_restart:
    R0 = DM(13, I0);
    DM(186, I2) = R0;
    R0 = DM(14, I0);
    DM(187, I2) = R0;
    CALL _cs_restart_arp;
_cs_note_batch:
    R0 = DM(175, I2);
    R0 = PASS R0;
    IF GE JUMP _cs_tick_begin; // generated bit only, PLAY is never strummed
    R2 = DM(8, I0);
    R2 = PASS R2;
    IF EQ JUMP _cs_tick_begin;
    R0 = DM(7, I0);
    I1 = R0;
_cs_find_notes:
    R0 = DM(0, I1);
    R1 = 2;
    COMP(R0, R1);
    IF LE JUMP _cs_commit;
    MODIFY(I1, 6) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cs_find_notes;
    JUMP _cs_tick_begin;
_cs_restart_arp:
    R0 = 0;
    DM(147, I2) = R0;
    DM(148, I2) = R0;
    DM(150, I2) = R0;
    RTS;
_cs_commit:
    R0 = 0;
    DM(_cs_index) = R0;
    DM(_cs_count) = R0;
_cs_collect:
    CALL _cs_slot;
    R8 = DM(0, I1);
    R8 = PASS R8;
    IF NE JUMP _cs_collected;
    R0 = DM(1, I0);
    R1 = 7;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _cs_collected;
    R8 = DM(0, I0);
_cs_collected:
    R0 = DM(_cs_index);
    R0 = LSHIFT R0 BY 2;
    R1 = _cs_tokens;
    R0 = R0 + R1;
    I2 = R0;
    DM(0, I2) = R8;
    R8 = PASS R8;
    IF EQ JUMP _cs_collect_next;
    R0 = DM(_cs_count);
    R0 = R0 + 1;
    DM(_cs_count) = R0;
_cs_collect_next:
    R0 = DM(_cs_index);
    R0 = R0 + 1;
    DM(_cs_index) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _cs_collect;
    R0 = 0;
    DM(_cs_index) = R0;
_cs_schedule:
    CALL _cs_slot;
    R0 = 0;
    DM(0, I2) = R0; // invalidate every old delayed onset at chord commit
    R0 = DM(1, I0);
    R1 = 9;
    COMP(R0, R1);
    IF NE JUMP _cs_was_not_delayed;
    R0 = 1;
    DM(1, I0) = R0;
_cs_was_not_delayed:
    R8 = DM(0, I1);
    R8 = PASS R8;
    IF NE JUMP _cs_delay;
    R8 = DM(0, I0);
    R8 = PASS R8;
    IF EQ JUMP _cs_schedule_next;
    R0 = DM(1, I0);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _cs_schedule_next;
    R0 = DM(7, I0);
    R0 = PASS R0;
    IF NE JUMP _cs_schedule_next; // retain already sounding common tones
_cs_delay:
    R0 = DM(_cs_strum);
    R0 = PASS R0;
    IF EQ JUMP _cs_schedule_next;
    R0 = DM(_cs_count);
    R0 = R0 - 1;
    IF LE JUMP _cs_schedule_next;
    F3 = FLOAT R0;
    I1 = _cs_tokens;
    R10 = 4;
    R11 = 0;
    R1 = 0x80000000;
    R9 = R8 XOR R1;
_cs_rank:
    R0 = DM(I1, M6);
    R0 = PASS R0;
    IF EQ JUMP _cs_rank_next;
    R0 = R0 XOR R1;
    COMP(R0, R9);
    IF LT R11 = R11 + 1;
_cs_rank_next:
    R10 = R10 - 1;
    IF NE JUMP _cs_rank;
    // Reciprocal for the only possible divisors 1,2,3; bounded scalar work.
    R0 = DM(_cs_count);
    R0 = R0 - 1;
    F1 = 1.0;
    R4 = 2;
    COMP(R0, R4);
    F2 = 0.5;
    IF EQ F1 = F2;
    R4 = 3;
    COMP(R0, R4);
    F2 = 0.33333334;
    IF EQ F1 = F2;
    R0 = DM(_cs_strum);
    F2 = FLOAT R0;
    F2 = F2 * F1;
    F0 = FLOAT R11;
    F0 = F0 * F2;
    R0 = FIX F0;
    R0 = PASS R0;
    IF EQ JUMP _cs_schedule_next;
    R0 = R0 + 1; // first tick is the commit block, not elapsed time
    DM(0, I2) = R8;
    DM(1, I2) = R0;
    // Pending replacements are hidden during rendering; foreground delay is
    // explicit gate bit 3 and cannot consume an audible release tail.
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF NE JUMP _cs_schedule_next;
    R0 = 9;
    DM(1, I0) = R0;
_cs_schedule_next:
    R0 = DM(_cs_index);
    R0 = R0 + 1;
    DM(_cs_index) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _cs_schedule;
_cs_tick_begin:
    R0 = 0;
    DM(_cs_index) = R0;
_cs_tick:
    CALL _cs_slot;
    R8 = DM(0, I2);
    R8 = PASS R8;
    IF EQ JUMP _cs_tick_next;
    R0 = DM(0, I1);
    COMP(R0, R8);
    R9 = 1;
    IF EQ JUMP _cs_owned_delay;
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF NE JUMP _cs_forget;
    R0 = DM(1, I0);
    R1 = 9;
    COMP(R0, R1);
    IF NE JUMP _cs_forget;
    R9 = 0;
_cs_owned_delay:
    R0 = DM(_cs_stop);
    R0 = PASS R0;
    IF NE JUMP _cs_cancel;
    R0 = DM(_cs_strum);
    R0 = PASS R0;
    IF EQ JUMP _cs_ready;
    R0 = DM(1, I2);
    R0 = R0 - 1;
    DM(1, I2) = R0;
    IF LE JUMP _cs_ready;
    R9 = PASS R9;
    IF EQ JUMP _cs_tick_next;
    R0 = DM(_cs_index);
    R0 = LSHIFT R0 BY 2;
    R1 = _cs_hidden;
    R0 = R0 + R1;
    I0 = R0;
    DM(0, I0) = R8;
    R0 = 0;
    DM(0, I1) = R0;
    JUMP _cs_tick_next;
_cs_cancel:
    R0 = 0;
    R9 = PASS R9;
    IF EQ JUMP _cs_cancel_foreground;
    DM(0, I1) = R0;
    JUMP _cs_forget;
_cs_cancel_foreground:
    DM(0, I0) = R0;
    JUMP _cs_forget;
_cs_ready:
    R9 = PASS R9;
    IF NE JUMP _cs_forget;
    R0 = 1;
    DM(1, I0) = R0;
_cs_forget:
    R0 = 0;
    DM(0, I2) = R0;
_cs_tick_next:
    R0 = DM(_cs_index);
    R0 = R0 + 1;
    DM(_cs_index) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _cs_tick;
    RTS;
_cs_restore:
    R0 = DM(_cs_state);
    R1 = 192;
    R0 = R0 + R1;
    I0 = R0;
    I1 = _cs_hidden;
    R2 = 4;
_cs_restore_slot:
    R0 = DM(I1, M6);
    R0 = PASS R0;
    IF EQ JUMP _cs_restore_next;
    DM(0, I0) = R0;
_cs_restore_next:
    MODIFY(I0, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _cs_restore_slot;
    RTS;
_cs_slot:
    R0 = DM(_cs_index);
    R1 = LSHIFT R0 BY 5;
    R2 = DM(_cs_state);
    R1 = R1 + R2;
    I0 = R1;
    R1 = LSHIFT R0 BY 4;
    R3 = 192;
    R1 = R1 + R3;
    R1 = R1 + R2;
    I1 = R1;
    R1 = LSHIFT R0 BY 3;
    R3 = 704;
    R1 = R1 + R3;
    R1 = R1 + R2;
    I2 = R1;
    RTS;
.PKBSS _cs_state, 4;
.PKBSS _cs_input, 4;
.PKBSS _cs_strum, 4;
.PKBSS _cs_restart, 4;
.PKBSS _cs_stop, 4;
.PKBSS _cs_index, 4;
.PKBSS _cs_count, 4;
.PKBSS _cs_tokens, 16;
.PKBSS _cs_hidden, 16;
