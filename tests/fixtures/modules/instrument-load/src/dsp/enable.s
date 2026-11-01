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
    RTS;
