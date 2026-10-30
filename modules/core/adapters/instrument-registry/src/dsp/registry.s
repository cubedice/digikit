// V2 mailbox scheduler. Lifecycle remains disabled until trusted admission is ready.
// Called by the protected instrument block wrapper: non-SIMD, L0/L1=0,
// M5=0/M6=1. Clobbers R0..R8/I0/I1 and arithmetic flags only.
// Command: magic, version, sequence, owner, generation, operation, row,
// nine remaining words: reserved 7..14, XOR checksum 15 (includes magic).
// Writer clears magic, writes body/check, then writes magic last. One outstanding
// command. Sequence never wraps within a boot. Invalid or old commands retain ACK.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_registry_block;
.EXTERN _ir_rows;
.EXTERN _ir_row_begin;
.EXTERN _ir_row_tick_protected;
_instrument_registry_block:
    R0 = DM(_ir_pending);
    R0 = PASS R0;
    IF NE JUMP _ir_poll;
    I0 = _dl_irows;
    R0 = DM(0, I0);
    R1 = 0x49524731;
    COMP(R0, R1);
    IF NE RTS;
    R2 = R1;
    R3 = 1;
    MODIFY(I0, 1) (NW);
_ir_sum:
    R0 = DM(I0, M6);
    R2 = R2 XOR R0;
    R3 = R3 + 1;
    R1 = 16;
    COMP(R3, R1);
    IF LT JUMP _ir_sum;
    R2 = PASS R2;
    IF NE RTS;
    I0 = _dl_irows;
    R0 = DM(1, I0);
    R1 = 1;
    COMP(R0, R1);
    IF NE RTS;
    R4 = DM(2, I0);
    R4 = PASS R4;
    IF EQ RTS;
    R0 = DM(_ir_sequence);
    R1 = 0x80000000;
    R0 = R0 XOR R1;
    R2 = R4 XOR R1;
    COMP(R2, R0);
    IF LE RTS;
    R5 = DM(3, I0);
    R0 = 31;
    R0 = R5 AND R0;
    COMP(R0, R5);
    IF NE RTS;
    R6 = DM(4, I0);
    R6 = PASS R6;
    IF EQ RTS;
    R7 = DM(5, I0);
    R8 = DM(6, I0);
    R0 = 7;
    R0 = R8 AND R0;
    COMP(R0, R8);
    IF NE RTS;
    MODIFY(I0, 7) (NW);
    R3 = 8;
_ir_reserved:
    R0 = DM(I0, M6);
    R0 = PASS R0;
    IF NE RTS;
    R3 = R3 - 1;
    IF NE JUMP _ir_reserved;
    // Status: 0 query accepted; 1 unsupported lifecycle; 2 unknown command.
    // Operations: 1 QUERY, 2 INITIALIZE, 3 DETACH. None enables a live instance.
    R2 = 2;
    R0 = 1;
    COMP(R7, R0);
    IF EQ JUMP _ir_query;
    R0 = 2;
    COMP(R7, R0);
    IF EQ JUMP _ir_start;
    R0 = 3;
    COMP(R7, R0);
    IF EQ JUMP _ir_start;
    JUMP _ir_reply;
_ir_query:
    R2 = 0;
    JUMP _ir_reply;
_ir_start:
    R0 = DM(_ir_lifecycle_ready);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _ir_unsupported;
    // Snapshot before callbacks. Later writes cannot change the active ticket.
    DM(_ir_ticket + 0) = R4;
    DM(_ir_ticket + 4) = R5;
    DM(_ir_ticket + 8) = R6;
    DM(_ir_ticket + 12) = R7;
    DM(_ir_ticket + 16) = R8;
    DM(_ir_sequence) = R4;
    DM(_ir_pending) = R1;
    R4 = R8;
    R8 = R7;
    CALL _ir_row_begin;
    R0 = PASS R0;
    IF EQ JUMP _ir_poll;
    R2 = 3; // owner/generation/phase rejected; no successful lifecycle ACK
    JUMP _ir_finish;
_ir_poll:
    R4 = DM(_ir_ticket + 16);
    CALL _ir_row_tick_protected;
    R0 = PASS R0;
    IF NE JUMP _ir_failed;
    R0 = LSHIFT R4 BY 6;
    R1 = _ir_rows;
    R0 = R0 + R1;
    I0 = R0;
    R0 = DM(0, I0);
    R1 = DM(_ir_ticket + 12);
    R2 = 2;
    COMP(R1, R2);
    IF NE JUMP _ir_detaching;
    R1 = 3; // READY only after all CREATE callbacks have returned
    JUMP _ir_phase;
_ir_detaching:
    R1 = 6; // DETACHED, retaining owner/generation tombstone
_ir_phase:
    COMP(R0, R1);
    IF NE RTS;
    R2 = 0;
    JUMP _ir_finish;
_ir_failed:
    R2 = 4; // row fault retained for cleanup/retry
_ir_finish:
    R0 = 0;
    DM(_ir_pending) = R0;
    R4 = DM(_ir_ticket + 0);
    R5 = DM(_ir_ticket + 4);
    R6 = DM(_ir_ticket + 8);
    R7 = DM(_ir_ticket + 12);
    R8 = DM(_ir_ticket + 16);
    JUMP _ir_reply;
_ir_unsupported:
    R2 = 1;
_ir_reply:
    I1 = _instrument_registry_reply;
    R0 = 0;
    DM(0, I1) = R0;
    DM(1, I1) = R4;
    DM(2, I1) = R5;
    DM(3, I1) = R6;
    DM(4, I1) = R7;
    DM(5, I1) = R8;
    DM(6, I1) = R2;
    R3 = DM(_ir_lifecycle_ready);
    R1 = 1;
    COMP(R3, R1);
    IF NE JUMP _ir_caps;
    R0 = 3; // reset/detach supported; does not advertise V2 audio dispatch
_ir_caps:
    DM(7, I1) = R0;
    DM(_ir_sequence) = R4;
    R0 = 0x49525231;
    DM(0, I1) = R0;
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _dl_iwords;
.VAR _dl_iwords = 16;
.VAR _ir_sequence = 0;
.PKBSS _dl_irows, 64;
.PKBSS _instrument_registry_reply, 32;

// Trusted admission only; not part of writable window 3. Zero on cold boot.
.PKBSS _ir_lifecycle_ready, 4;
.PKBSS _ir_pending, 4;
.PKBSS _ir_ticket, 20;
