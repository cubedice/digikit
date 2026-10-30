// DTII instrument reply: entries 11/12 in rows 21..28. The stock record is
// 0xabc bytes; its full copy reaches CF. ACK/diagnostics (0..12), Overloop
// (13..20), other row entries, header and trailer remain untouched.
// Uses only the block-tail wrapper's already-saved/free registers.
.SECTION/PM seg_pmco;
_dl_instrument_status:
    R0 = _instrument_reply;
    R0 = PASS R0;
    IF EQ JUMP _dl_instrument_status_return;
    I3 = R0;
    R3 = I4;
    I2 = R3;
    R0 = DM(0, I3); DM(465, I2) = R0;
    R0 = DM(1, I3); DM(466, I2) = R0;
    R0 = DM(2, I3); DM(486, I2) = R0;
    R0 = DM(3, I3); DM(487, I2) = R0;
    R0 = DM(4, I3); DM(507, I2) = R0;
    R0 = DM(5, I3); DM(508, I2) = R0;
    R0 = DM(6, I3); DM(528, I2) = R0;
    R0 = DM(7, I3); DM(529, I2) = R0;
    R0 = DM(8, I3); DM(549, I2) = R0;
    R0 = DM(9, I3); DM(550, I2) = R0;
    R0 = DM(10, I3); DM(570, I2) = R0;
    R0 = DM(11, I3); DM(571, I2) = R0;
    R0 = DM(12, I3); DM(591, I2) = R0;
    R0 = DM(13, I3); DM(592, I2) = R0;
    R0 = DM(14, I3); DM(612, I2) = R0;
    R0 = DM(15, I3); DM(613, I2) = R0;
    JUMP _dl_instrument_status_return;
