// Borrowed V5 output. No publication before the complete audio block succeeds.
// The core supplies a disjoint 13-word sink. Cursor word188 belongs to this
// instance; all other storage below is scratch for this non-reentrant callback.
// Physical-key/pedal/latch ownership is deliberately not inferred from gates.
.SECTION/PM seg_pmco;
.GLOBAL _cu_publish;
_cu_publish:
    I3 = R8;
    R0 = DM(0, I3);
    R1 = 5;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(22, I3);
    I4 = R0;
    R0 = 0;
    DM(0, I4) = R0;
    R0 = DM(10, I3);
    R1 = 32;
    COMP(R0, R1);
    IF NE RTS; // Only CPS6 supplies presentation values and mode.
    I2 = R4;
    I0 = I4;
    MODIFY(I0, 1) (NW);
    R0 = 0;
    R1 = 12;
_cu_clear:
    DM(I0, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _cu_clear;
    R0 = DM(4, I3);
    DM(1, I4) = R0;
    R0 = DM(2, I3);
    DM(2, I4) = R0;
    R0 = DM(3, I3);
    DM(3, I4) = R0;
    R0 = DM(11, I3);
    DM(4, I4) = R0;
    R0 = DM(9, I3);
    I1 = R0;
    // Hold each fast/detail choice for four callbacks. Either wire parity sees
    // each fragment; 88 blocks per bank stays below 25 complete banks/second.
    R0 = DM(188, I2);
    R1 = R0 + 1;
    R2 = 88;
    COMP(R1, R2);
    R2 = 0;
    IF GE R1 = R2;
    DM(188, I2) = R1;
    R1 = 4;
    R1 = R0 AND R1;
    IF NE JUMP _cu_detail;
    R0 = DM(0, I1);
    R0 = LSHIFT R0 BY -8;
    R0 = LSHIFT R0 BY 6;
    R1 = DM(2, I4);
    R0 = R0 OR R1;
    R1 = DM(28, I1);
    R1 = LSHIFT R1 BY 9;
    R0 = R0 OR R1;
    DM(2, I4) = R0;
    R0 = DM(174, I2);
    DM(5, I4) = R0;
    F0 = DM(144, I2);
    F1 = 127.0;
    F0 = F0 * F1;
    R0 = FIX F0;
    DM(9, I4) = R0;
    R0 = DM(13, I1);
    R1 = DM(8, I1);
    R1 = LSHIFT R1 BY 24;
    R0 = R0 OR R1;
    DM(10, I4) = R0;
    R0 = DM(11, I3);
    DM(12, I4) = R0;
    R0 = 0;
    DM(_cu_slot) = R0;
    I0 = _cu_tokens;
    DM(0, I0) = R0;
    DM(1, I0) = R0;
    DM(2, I0) = R0;
    DM(3, I0) = R0;
_cu_note:
    R5 = DM(_cu_slot);
    R0 = LSHIFT R5 BY 5;
    R1 = I2;
    R0 = R0 + R1;
    I0 = R0;
    R0 = LSHIFT R5 BY 4;
    R0 = R0 + R1;
    R1 = 192;
    R0 = R0 + R1;
    I1 = R0;
    R8 = DM(0, I1);
    R8 = PASS R8;
    IF NE JUMP _cu_owner;
    R0 = DM(1, I0);
    R1 = 7;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _cu_next;
    R8 = DM(0, I0);
    R8 = PASS R8;
    IF EQ JUMP _cu_next;
_cu_owner:
    I0 = I2;
    MODIFY(I0, 73) (NW);
    R7 = 10;
_cu_find:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cu_found;
    MODIFY(I0, 2) (NW);
    R7 = R7 - 1;
    IF NE JUMP _cu_find;
    JUMP _cu_next;
_cu_found:
    R0 = LSHIFT R5 BY 2;
    R1 = _cu_tokens;
    R0 = R0 + R1;
    I1 = R0;
    DM(0, I1) = R8;
    R0 = DM(1, I0);
    R0 = FEXT R0 BY 8:7;
    R6 = LSHIFT R5 BY 3;
    R0 = LSHIFT R0 BY R6;
    R1 = DM(6, I4);
    R0 = R0 OR R1;
    DM(6, I4) = R0;
    R0 = LSHIFT R5 BY 3;
    R0 = LSHIFT R0 BY R6;
    R1 = DM(7, I4);
    R0 = R0 OR R1;
    DM(7, I4) = R0;
    R0 = 0x10000;
    R0 = LSHIFT R0 BY R5;
    R1 = DM(2, I4);
    R0 = R0 OR R1;
    R1 = 0x100000;
    R0 = R0 + R1;
    DM(2, I4) = R0;
_cu_next:
    R0 = DM(_cu_slot);
    R0 = R0 + 1;
    DM(_cu_slot) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _cu_note;
    // Release tails are not admitted chord members.
    I0 = I2;
    R6 = 6;
    R7 = 0;
_cu_tail:
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF EQ JUMP _cu_tail_next;
    R0 = DM(7, I0);
    R0 = PASS R0;
    IF EQ JUMP _cu_tail_next;
    R0 = DM(1, I0);
    R1 = 7;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _cu_tail_next;
    R7 = R7 + 1;
_cu_tail_next:
    MODIFY(I0, 8) (NW);
    R6 = R6 - 1;
    IF NE JUMP _cu_tail;
    R0 = LSHIFT R7 BY 23;
    R1 = DM(2, I4);
    R0 = R0 OR R1;
    DM(2, I4) = R0;
    R8 = DM(155, I2); // Active arp source token, not the scheduled candidate.
    R8 = PASS R8;
    IF EQ JUMP _cu_ready;
    R0 = DM(136, I2);
    R0 = PASS R0;
    IF EQ JUMP _cu_ready;
    I0 = I2;
    MODIFY(I0, 73) (NW);
    R7 = 10;
_cu_arp_owner:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _cu_arp_pitch;
    MODIFY(I0, 2) (NW);
    R7 = R7 - 1;
    IF NE JUMP _cu_arp_owner;
    JUMP _cu_ready;
_cu_arp_pitch:
    R0 = DM(1, I0);
    R0 = FEXT R0 BY 8:7;
    R1 = DM(157, I2);
    R0 = R0 + R1;
    R1 = 127;
    COMP(R0, R1);
    IF GT JUMP _cu_ready;
    R1 = 256;
    R0 = R0 OR R1;
    R1 = DM(148, I2);
    R1 = LSHIFT R1 BY 16;
    R0 = R0 OR R1;
    DM(8, I4) = R0;
    I0 = _cu_tokens;
    R6 = 0;
_cu_arp_match:
    R0 = DM(I0, M6);
    COMP(R0, R8);
    IF EQ JUMP _cu_arp_matched;
    R6 = R6 + 1;
    R1 = 4;
    COMP(R6, R1);
    IF LT JUMP _cu_arp_match;
    JUMP _cu_arp_gate;
_cu_arp_matched:
    R0 = LSHIFT R6 BY 10;
    R1 = 512;
    R0 = R0 OR R1;
    R1 = DM(8, I4);
    R0 = R0 OR R1;
    DM(8, I4) = R0;
_cu_arp_gate:
    R0 = DM(137, I2);
    R1 = 7;
    R0 = R0 AND R1;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _cu_ready;
    R0 = DM(2, I4);
    R1 = 0x04000000;
    R0 = R0 OR R1;
    R2 = DM(9, I4);
    R2 = PASS R2;
    R1 = 0x08000000;
    IF NE R0 = R0 OR R1;
    DM(2, I4) = R0;
    JUMP _cu_ready;
_cu_detail:
    R0 = LSHIFT R0 BY -3;
    R1 = 0x10b00;
    R1 = R0 OR R1;
    DM(5, I4) = R1;
    R1 = DM(2, I4);
    R2 = 0x40000000;
    R1 = R1 OR R2;
    DM(2, I4) = R1;
    R1 = DM(11, I3);
    DM(6, I4) = R1;
    R1 = 6;
    R5 = R0 * R1 (SSI);
    I0 = I4;
    MODIFY(I0, 7) (NW);
    R6 = 6;
_cu_detail_word:
    R0 = 0;
    R1 = 16;
    COMP(R5, R1);
    IF GE JUMP _cu_detail_store;
    R1 = R5 + R1;
    R1 = LSHIFT R1 BY 2;
    R2 = DM(9, I3);
    R1 = R1 + R2;
    I1 = R1;
    R0 = DM(0, I1);
_cu_detail_store:
    DM(I0, M6) = R0;
    R5 = R5 + 1;
    R6 = R6 - 1;
    IF NE JUMP _cu_detail_word;
_cu_ready:
    R0 = 1;
    DM(0, I4) = R0;
    RTS;
.PKBSS _cu_slot, 4;
.PKBSS _cu_tokens, 16;
