// A slow motor receives several input samples while passing one tape cell.
// Average the dry signal over that cell and print once, when the head leaves.
// The phase advance and the first-pass timer still run at the output rate.
_tape_slow_write:
    R0 = DM(RT_POS,I4); R1 = DM(T_WRITE_POSITION,I5);
    R2 = DM(T_WRITE_COUNT,I5); COMP(R0,R1); IF NE R2 = R2 XOR R2;
    DM(T_WRITE_POSITION,I5) = R0;
    R0 = DM(_t_rate); R0 = ASHIFT R0 BY -31; R1 = DM(T_WRITE_DIRECTION,I5);
    COMP(R0,R1); IF NE R2 = R2 XOR R2;
    DM(T_WRITE_DIRECTION,I5) = R0;
    F0 = DM(_t_dry_l); F1 = DM(_t_dry_r);
    R2 = PASS R2; IF EQ JUMP _tape_slow_accumulate;
    F3 = DM(T_WRITE_SUM_L,I5); F0 = F0 + F3;
    F3 = DM(T_WRITE_SUM_R,I5); F1 = F1 + F3;
_tape_slow_accumulate:
    DM(T_WRITE_SUM_L,I5) = F0; DM(T_WRITE_SUM_R,I5) = F1;
    R2 = R2 + 1; DM(T_WRITE_COUNT,I5) = R2;
    R0 = DM(_t_separation); R0 = PASS R0; IF EQ JUMP _tape_slow_monitor;
    F0 = DM(_t_wet_l); F1 = DM(_t_wet_r);
    DM(T_WRITE_FEEDBACK_L,I5) = F0; DM(T_WRITE_FEEDBACK_R,I5) = F1;
    JUMP _tape_slow_departure;
_tape_slow_monitor:
    // At zero spacing monitor each input sample immediately. The preceding
    // monitor sample, not the current one, feeds the causal feedback path.
    F0 = DM(T_LAST_L,I5); F1 = DM(T_LAST_R,I5);
    DM(T_WRITE_FEEDBACK_L,I5) = F0; DM(T_WRITE_FEEDBACK_R,I5) = F1;
    F2 = DM(T_EFF_FEEDBACK,I5); F0 = F0 * F2; F1 = F1 * F2;
    F2 = DM(_t_dry_l); F0 = F0 + F2; F2 = DM(_t_dry_r); F1 = F1 + F2;
    CALL _tape_slow_clip;
    DM(_t_wet_l) = F0; DM(_t_wet_r) = F1;
_tape_slow_departure:
    R0 = DM(_t_delta); R0 = PASS R0; IF NE JUMP _tape_slow_pack;
    // Retain a final partial cell when the fixed first pass completes.
    R0 = DM(_t_gate); R1 = 1; COMP(R0,R1); IF NE JUMP _tape_advance;
    R0 = DM(_rt_written); R0 = R0 + 1; R1 = DM(_rt_goal);
    COMP(R0,R1); IF NE JUMP _tape_advance;
_tape_slow_pack:
    CALL _tape_slow_print;
    JUMP _tape_pack;

// End of a punch can leave the head partway through a cell. Write that partial
// cell before publishing the private tape. No transport or monitor advances.
_tape_slow_close:
    R0 = DM(T_WRITE_COUNT,I5); R0 = PASS R0; IF EQ RTS;
    R12 = DM(RT_POS,I4); R0 = DM(T_WRITE_POSITION,I5);
    COMP(R0,R12); IF NE JUMP _tape_slow_clear;
    DM(_t_track) = R8; R0 = DM(RT_SLOT,I4); DM(_t_slot) = R0;
    R0 = -1; DM(_t_key0) = R0; DM(_t_key1) = R0; DM(_t_key2) = R0;
    CALL _tape_slow_print;
    R2 = 0x47000000; F0 = F0 * F2; F1 = F1 * F2; R0 = FIX F0; R1 = FIX F1;
    R2 = 65535; R1 = R1 AND R2; R0 = LSHIFT R0 BY 16; R0 = R0 OR R1;
    DM(_t_word) = R0;
    R12 = DM(RT_POS,I4); CALL _tape_address; R0 = DM(_t_word); DM(0,I0) = R0;
    RTS;
_tape_slow_clear:
    R0 = 0; DM(T_WRITE_COUNT,I5) = R0; RTS;

_tape_slow_print:
    // FULL with coincident integer heads must preserve the old destination
    // at unity feedback, even when the audible playback is interpolated.
    R0 = DM(T_EFF_SPACING,I5); R1 = 65536; COMP(R0,R1);
    IF NE JUMP _tape_slow_feedback;
    R0 = DM(T_READ_OFFSET,I5); R0 = PASS R0; IF NE JUMP _tape_slow_feedback;
    R12 = DM(RT_POS,I4); CALL _tape_pcm;
    JUMP _tape_slow_average;
_tape_slow_feedback:
    F0 = DM(T_WRITE_FEEDBACK_L,I5); F1 = DM(T_WRITE_FEEDBACK_R,I5);
_tape_slow_average:
    // Nonzero Q16 speed crosses one cell in at most 65536 arrivals.
    R2 = DM(T_WRITE_COUNT,I5); F2 = FLOAT R2; F3 = RECIPS F2;
    R4 = 0x40000000; F6 = F2 * F3; F6 = F4 - F6; F3 = F3 * F6;
    F6 = F2 * F3; F6 = F4 - F6; F3 = F3 * F6;
    F2 = DM(T_EFF_FEEDBACK,I5); F0 = F0 * F2; F1 = F1 * F2;
    F4 = DM(T_WRITE_SUM_L,I5); F4 = F4 * F3; F0 = F0 + F4;
    F4 = DM(T_WRITE_SUM_R,I5); F4 = F4 * F3; F1 = F1 + F4;
    R2 = 0; DM(T_WRITE_COUNT,I5) = R2;
_tape_slow_clip:
    R2 = 0xbf800000; F0 = MAX(F0,F2); F1 = MAX(F1,F2);
    R2 = 0x3f7ffe00; F0 = MIN(F0,F2); F1 = MIN(F1,F2); RTS;
