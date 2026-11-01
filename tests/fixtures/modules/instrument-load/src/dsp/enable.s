// Emulator-only lifecycle gate; production images never contain this hook.
// BIND must finish while readiness is zero. The test uses one bounded row.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_carrier_prepare;
.EXTERN _instrument_session;
.EXTERN _ir_lifecycle_ready;
_instrument_carrier_prepare:
    R0 = DM(_instrument_session);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = 1;
    DM(_ir_lifecycle_ready) = R0;
    R0 = DM(_ilp_dispatched);
    R0 = PASS R0;
    IF NE RTS;
    I0 = _ir_rows;
    R0 = DM(0, I0);
    R1 = 3;
    COMP(R0, R1);
    IF NE RTS;
    I0 = _ilp_process_input;
    R0 = 2;
    DM(0, I0) = R0;
    R0 = 72;
    DM(1, I0) = R0;
    R0 = _ilp_process_output;
    DM(5, I0) = R0;
    R4 = 0;
    R5 = 0;
    R8 = _ilp_process_input;
    CALL _ir_process_protected;
    DM(_ilp_process_result) = R0;
    R0 = 1;
    DM(_ilp_dispatched) = R0;
    RTS;

.EXTERN _ir_rows;
.EXTERN _ir_process_protected;
.PKDATA _ilp_dispatched, 4;
.PKDATA _ilp_process_result, 4;
.PKDATA _ilp_process_input, 72;
.PKDATA _ilp_process_output, 128;
