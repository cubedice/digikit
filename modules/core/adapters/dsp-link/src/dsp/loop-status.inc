// DTII OVL2 carrier: the established spare cells +0x44/+0x48 in rows
// 17..20 and 29..31. ACK owns row0, diagnostics1..16, instruments21..28.
// Preserve the module's 16-word OVL1 mailbox: logical7/14/15 are three
// 7-bit step controls packed into bytes0/1/2 of carrier7 (14 words total).
// FX breadcrumbs write both reply rings immediately, not only at the tail;
// their row13 word297 must never alias the loop magic again.
.SECTION/PM seg_pmco;
.EXTERN _loop_ui_status;
_dl_loop_status:
    R0 = _loop_ui_status;
    R0 = PASS R0;
    IF EQ JUMP _dl_loop_status_return;
    I3 = R0;
    R0 = DM(0, I3);
    R3 = 0x4f564c31;
    COMP(R0, R3);
    IF NE JUMP _dl_loop_status_return;
    R0 = DM(7, I3);
    R3 = DM(14, I3);
    R4 = DM(15, I3);
    R5 = R0 OR R3;
    R5 = R5 OR R4;
    R6 = 0xffffff80;
    R5 = R5 AND R6;
    IF NE JUMP _dl_loop_status_return;
    R3 = LSHIFT R3 BY 8;
    R4 = LSHIFT R4 BY 16;
    R5 = R0 OR R3;
    R5 = R5 OR R4;
    R3 = I4;
    I2 = R3;
    R0 = 0x4f564c32; DM(381, I2) = R0;
    R0 = DM(1, I3); DM(382, I2) = R0;
    R0 = DM(2, I3); DM(402, I2) = R0;
    R0 = DM(3, I3); DM(403, I2) = R0;
    R0 = DM(4, I3); DM(423, I2) = R0;
    R0 = DM(5, I3); DM(424, I2) = R0;
    R0 = DM(6, I3); DM(444, I2) = R0;
    DM(445, I2) = R5;
    R0 = DM(8, I3); DM(633, I2) = R0;
    R0 = DM(9, I3); DM(634, I2) = R0;
    R0 = DM(10, I3); DM(654, I2) = R0;
    R0 = DM(11, I3); DM(655, I2) = R0;
    R0 = DM(12, I3); DM(675, I2) = R0;
    R0 = DM(13, I3); DM(676, I2) = R0;
    JUMP _dl_loop_status_return;
