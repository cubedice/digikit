// DTII only: entries 11/12 of rows 13..20. ACK is row 0,
// diagnostics rows 1..12; header, trailer and stock values stay untouched.
.SECTION/PM seg_pmco;
.EXTERN _loop_ui_status;
_dl_loop_status:
    R0 = _loop_ui_status;
    R0 = PASS R0;
    IF EQ JUMP _dl_loop_status_return;
    I3 = R0;
    R3 = I4;
    I2 = R3;
    R0 = DM(0, I3); DM(297, I2) = R0;
    R0 = DM(1, I3); DM(298, I2) = R0;
    R0 = DM(2, I3); DM(318, I2) = R0;
    R0 = DM(3, I3); DM(319, I2) = R0;
    R0 = DM(4, I3); DM(339, I2) = R0;
    R0 = DM(5, I3); DM(340, I2) = R0;
    R0 = DM(6, I3); DM(360, I2) = R0;
    R0 = DM(7, I3); DM(361, I2) = R0;
    R0 = DM(8, I3); DM(381, I2) = R0;
    R0 = DM(9, I3); DM(382, I2) = R0;
    R0 = DM(10, I3); DM(402, I2) = R0;
    R0 = DM(11, I3); DM(403, I2) = R0;
    R0 = DM(12, I3); DM(423, I2) = R0;
    R0 = DM(13, I3); DM(424, I2) = R0;
    R0 = DM(14, I3); DM(444, I2) = R0;
    R0 = DM(15, I3); DM(445, I2) = R0;
    JUMP _dl_loop_status_return;
