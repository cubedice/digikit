// Called after receive/configuration and before stock coefficient construction.
// Runs under block.s's saved scalar context. Kernel tokens retain ownership
// through reset tails, even when a note was reset before its first render.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_carrier_prepare;
.EXTERN _cp_ready;
.EXTERN _cp_banks;
.EXTERN _mi_velocity_prepare;
_instrument_carrier_prepare:
    R4 = 0;
    I1 = _cp_ready;
    R5 = _cp_banks;
_cp_prepare_track:
    R10 = DM(0, I1);
    I0 = R5;
    R6 = 6;
_cp_prepare_slots:
    R0 = DM(0, I0);
    R10 = R10 OR R0;
    MODIFY(I0, 32);
    R6 = R6 - 1;
    IF NE JUMP _cp_prepare_slots;
    R6 = 4;
_cp_prepare_pending:
    R0 = DM(0, I0);
    R10 = R10 OR R0;
    MODIFY(I0, 16);
    R6 = R6 - 1;
    IF NE JUMP _cp_prepare_pending;
    CALL _mi_velocity_prepare;
    MODIFY(I1, 4);
    R0 = 256;
    R5 = R5 + R0;
    R4 = R4 + 1;
    R0 = 16;
    COMP(R4, R0);
    IF LT JUMP _cp_prepare_track;
    RTS;
