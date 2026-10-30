// Internal V2 row lifecycle. Not wired to the public mailbox or audio dispatch.
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
_ir_row_begin:
    CALL _ir_row_address;
    R0 = PASS R0;
    IF NE RTS;
    R0 = 31;
    R0 = R5 AND R0;
    COMP(R0, R5);
    IF NE JUMP _irr_invalid;
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
.PKBSS _ir_rows, 512;
