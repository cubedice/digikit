// Internal V2 row lifecycle. Public admission and audio dispatch remain unwired.
// Trusted admission alone may write metadata, while the row is unreachable.
// 16 words: phase,owner,generation,tracks,state_base,state_words,arena_start,
// arena_end,reset_entry,code_start,code_end,cursor,error,touched,reserved,reserved.
// Phases: 0 empty,1 reserved,2 initializing,3 ready,4 detaching,5 fault,6 detached.
// begin: R4 row index, R5 owner, R6 generation, R8 operation (2 init/3 detach).
// tick: R4 row index. R0=0 success/progress, -1 invalid, callback negative on fault.
// Caller pins the same context and protects the same clobbers as lifecycle.s.
// Fixed scratch makes ticks non-reentrant. One callback maximum per tick.
.SECTION/PM seg_pmco;
.GLOBAL _ir_row_begin;
.GLOBAL _ir_row_tick;
.EXTERN _ir_reset_instance;
_ir_row_address:
    R0 = 7;
    R0 = R4 AND R0;
    COMP(R0, R4);
    IF NE JUMP _irr_invalid;
    R0 = LSHIFT R4 BY 6;
    R1 = _ir_rows;
    R0 = R0 + R1;
    I0 = R0;
    R0 = 0;
    RTS;
// Trusted internal install: R4 index, I1 immutable private 16-word snapshot.
// Mailbox INSTALL supplies a private snapshot. Caller owns the allocation,
// validates instruction boundaries/budgets, drains image writes and serializes
// installs/ticks. No callbacks. Clobbers R0..R3, I0/I1; linear L0/L1, M6=1.
// Accept empty rows or a strictly newer unsigned generation after full detach.
// Validate everything before writing; publish RESERVED last. R0=0/-1.
.GLOBAL _ir_row_install;
_ir_row_install:
    CALL _ir_row_address;
    R0 = PASS R0;
    IF NE RTS;
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF EQ JUMP _iri_metadata;
    R1 = 6;
    COMP(R0, R1);
    IF NE JUMP _irr_invalid;
    R0 = DM(2, I0);
    R1 = DM(2, I1);
    R2 = 0x80000000;
    R0 = R0 XOR R2;
    R1 = R1 XOR R2;
    COMP(R1, R0);
    IF LE JUMP _irr_invalid;
_iri_metadata:
    R0 = DM(0, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _irr_invalid;
    R0 = DM(1, I1);
    R1 = 31;
    R1 = R0 AND R1;
    COMP(R0, R1);
    IF NE JUMP _irr_invalid;
    R0 = DM(2, I1);
    R0 = PASS R0;
    IF EQ JUMP _irr_invalid;
    R3 = DM(3, I1);
    R3 = PASS R3;
    IF LE JUMP _irr_invalid;
    R0 = 16;
    COMP(R3, R0);
    IF GT JUMP _irr_invalid;
    R2 = DM(5, I1);
    R2 = PASS R2;
    IF LE JUMP _irr_invalid;
    R0 = 1024;
    COMP(R2, R0);
    IF GT JUMP _irr_invalid;
    R2 = R2 + 1;
    R2 = LSHIFT R2 BY -1;
    R2 = LSHIFT R2 BY 3; // eight-byte stride
    R1 = DM(4, I1);
    R0 = 7;
    R0 = R1 AND R0;
    IF NE JUMP _irr_invalid;
    R0 = DM(6, I1);
    COMP(R1, R0);
    IF LT JUMP _irr_invalid;
    R0 = 0x20000000;
    COMP(R1, R0);
    IF LT JUMP _irr_invalid;
    R1 = DM(6, I1);
    COMP(R1, R0);
    IF LT JUMP _irr_invalid;
    R0 = 7;
    R0 = R1 AND R0;
    IF NE JUMP _irr_invalid;
    R1 = DM(7, I1);
    R0 = 0x20080000;
    COMP(R1, R0);
    IF GT JUMP _irr_invalid;
    R0 = 7;
    R0 = R1 AND R0;
    IF NE JUMP _irr_invalid;
    R0 = DM(6, I1);
    COMP(R1, R0);
    IF LE JUMP _irr_invalid;
    R1 = DM(4, I1);
_iri_extent:
    R1 = R1 + R2;
    R0 = PASS R1;
    IF LE JUMP _irr_invalid;
    R3 = R3 - 1;
    IF NE JUMP _iri_extent;
    R0 = DM(7, I1);
    COMP(R1, R0);
    IF GT JUMP _irr_invalid;
    R1 = DM(9, I1);
    R0 = 0x00b80000;
    COMP(R1, R0);
    IF LT JUMP _irr_invalid;
    R2 = DM(10, I1);
    R0 = 0x00bc0000;
    COMP(R2, R0);
    IF GT JUMP _irr_invalid;
    R3 = DM(8, I1);
    COMP(R3, R1);
    IF LT JUMP _irr_invalid;
    COMP(R3, R2);
    IF GE JUMP _irr_invalid;
    R0 = 3; // complete six-byte callback instruction
    R3 = R3 + R0;
    COMP(R3, R2);
    IF GT JUMP _irr_invalid;
    R0 = DM(11, I1);
    R1 = DM(12, I1);
    R0 = R0 OR R1;
    R1 = DM(13, I1);
    R0 = R0 OR R1;
    R1 = DM(14, I1);
    R0 = R0 OR R1;
    R1 = DM(15, I1);
    R0 = R0 OR R1;
    IF NE JUMP _irr_invalid;
    // The snapshot cannot alias any row or be changed concurrently by transport.
    // Caller supplies private storage; destination remains unreachable throughout.
    MODIFY(I0, 1) (NW);
    MODIFY(I1, 1) (NW);
    R3 = 15;
_iri_copy:
    R0 = DM(I1, M6);
    DM(I0, M6) = R0;
    R3 = R3 - 1;
    IF NE JUMP _iri_copy;
    MODIFY(I0, -16) (NW);
    R0 = 1;
    DM(0, I0) = R0;
    JUMP _irr_ok;
_ir_row_begin:
    CALL _ir_row_address;
    R0 = PASS R0;
    IF NE RTS;
    R0 = 31;
    R0 = R5 AND R0;
    COMP(R0, R5);
    IF NE JUMP _irr_invalid;
    R0 = PASS R6;
    IF EQ JUMP _irr_invalid;
    // After transport drainage, DETACH also retires a never-installed ticket.
    // Only EMPTY or an older DETACHED tombstone may be claimed this way.
    R0 = 3;
    COMP(R8, R0);
    IF NE JUMP _irr_identity;
    R1 = DM(0, I0);
    R0 = PASS R1;
    IF EQ JUMP _irr_retire_empty;
    R0 = 6;
    COMP(R1, R0);
    IF NE JUMP _irr_identity;
    R0 = DM(2, I0);
    R1 = 0x80000000;
    R0 = R0 XOR R1;
    R2 = R6 XOR R1;
    COMP(R2, R0);
    IF GT JUMP _irr_retire_empty;
_irr_identity:
    R0 = DM(1, I0);
    COMP(R0, R5);
    IF NE JUMP _irr_invalid;
    R0 = DM(2, I0);
    R0 = PASS R0;
    IF EQ JUMP _irr_invalid;
    COMP(R0, R6);
    IF NE JUMP _irr_invalid;
    R1 = DM(0, I0);
    R0 = 2;
    COMP(R8, R0);
    IF EQ JUMP _irr_begin_init;
    R0 = 3;
    COMP(R8, R0);
    IF NE JUMP _irr_invalid;
    // Detach is idempotent while pending or after completion for this owner.
    R0 = 6;
    COMP(R1, R0);
    IF EQ JUMP _irr_ok;
    R0 = 4;
    COMP(R1, R0);
    IF EQ JUMP _irr_ok;
    R0 = 1;
    COMP(R1, R0);
    IF LT JUMP _irr_invalid;
    R0 = 5;
    COMP(R1, R0);
    IF GT JUMP _irr_invalid;
    R0 = 4;
    JUMP _irr_start;
_irr_retire_empty:
    // Retain the cancelled generation so a delayed INSTALL cannot resurrect it.
    DM(1, I0) = R5;
    DM(2, I0) = R6;
    MODIFY(I0, 3) (NW);
    R2 = 13;
    R0 = 0;
_irr_retire_clear:
    DM(I0, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _irr_retire_clear;
    MODIFY(I0, -16) (NW);
    R0 = 6;
    DM(0, I0) = R0;
    JUMP _irr_ok;
_irr_begin_init:
    R0 = 1;
    COMP(R1, R0);
    IF NE JUMP _irr_invalid;
    R0 = DM(13, I0);
    R0 = PASS R0;
    IF NE JUMP _irr_invalid;
    R0 = 2;
_irr_start:
    DM(0, I0) = R0;
    R0 = 0;
    DM(11, I0) = R0;
    DM(12, I0) = R0;
_irr_ok:
    R0 = 0;
    RTS;
_irr_invalid:
    R0 = -1;
    RTS;
_ir_row_tick:
    CALL _ir_row_address;
    R0 = PASS R0;
    IF NE RTS;
    R0 = I0;
    DM(_irr_current) = R0;
    R1 = DM(0, I0);
    R0 = 2;
    COMP(R1, R0);
    IF EQ JUMP _irr_active;
    R0 = 4;
    COMP(R1, R0);
    IF NE JUMP _irr_invalid;
_irr_active:
    R2 = DM(3, I0);
    R2 = PASS R2;
    IF LE JUMP _irr_bad;
    R0 = 16;
    COMP(R2, R0);
    IF GT JUMP _irr_bad;
    R3 = DM(13, I0);
    R0 = PASS R3;
    IF LT JUMP _irr_bad;
    COMP(R3, R2);
    IF GT JUMP _irr_bad;
    R0 = 4;
    COMP(R1, R0);
    IF NE JUMP _irr_limit;
    R2 = R3; // detach only instances whose CREATE was attempted
_irr_limit:
    R3 = DM(11, I0);
    R0 = PASS R3;
    IF LT JUMP _irr_bad;
    R0 = 2;
    COMP(R1, R0);
    IF NE JUMP _irr_cursor_valid;
    R0 = DM(13, I0);
    COMP(R3, R0);
    IF NE JUMP _irr_bad;
_irr_cursor_valid:
    COMP(R3, R2);
    IF GT JUMP _irr_bad;
    IF EQ JUMP _irr_complete;
    R5 = DM(5, I0);
    R0 = PASS R5;
    IF LE JUMP _irr_bad;
    R0 = 1024;
    COMP(R5, R0);
    IF GT JUMP _irr_bad;
    R4 = DM(4, I0);
    R4 = PASS R4;
    IF LE JUMP _irr_bad;
    R6 = DM(6, I0);
    R7 = DM(7, I0);
    R12 = DM(8, I0);
    R9 = DM(9, I0);
    R10 = DM(10, I0);
    // Descriptor allocation uses eight-byte instance alignment.
    R0 = R5 + 1;
    R0 = LSHIFT R0 BY -1;
    R0 = LSHIFT R0 BY 3;
    R2 = PASS R3;
    IF EQ JUMP _irr_offset_done;
_irr_offset:
    R4 = R4 + R0;
    R4 = PASS R4;
    IF LE JUMP _irr_bad;
    R2 = R2 - 1;
    IF NE JUMP _irr_offset;
_irr_offset_done:
    R8 = 4;
    R0 = 4;
    COMP(R1, R0);
    IF EQ JUMP _irr_call;
    R8 = 1;
    // Retain this instance even if its callback faults after a partial write.
    R0 = R3 + 1;
    DM(13, I0) = R0;
_irr_call:
    CALL _ir_reset_instance;
    I0 = DM(_irr_current);
    R0 = PASS R0;
    IF NE JUMP _irr_fault;
    R0 = DM(11, I0);
    R0 = R0 + 1;
    DM(11, I0) = R0;
    // Completion is reported by a subsequent tick, never before callback return.
    JUMP _irr_ok;
_irr_bad:
    R0 = -1;
_irr_fault:
    DM(12, I0) = R0;
    R1 = 5;
    DM(0, I0) = R1;
    RTS;
_irr_complete:
    R0 = 2;
    COMP(R1, R0);
    IF EQ JUMP _irr_ready;
    R0 = 6;
    DM(0, I0) = R0;
    JUMP _irr_ok;
_irr_ready:
    R0 = 3;
    DM(0, I0) = R0;
    JUMP _irr_ok;
.SECTION/DM seg_dmda;
.VAR _irr_current = 0;
// INSTALL reads the initial row phase before writing metadata.
.PKDATA _ir_rows, 512;
