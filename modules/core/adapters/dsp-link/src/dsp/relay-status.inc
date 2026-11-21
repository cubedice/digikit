// One status every sixteen blocks. During file transfer, a co-resident PRISM
// gets seven consecutive native replies (enough for a complete snapshot) and
// Overloop gets its unmodified fourth reply. RELAY alone can use the other 15.
.SECTION/PM seg_pmco;
_dl_relay_status:
    R0 = _relay_status;
    R0 = PASS R0;
    IF EQ JUMP _dl_relay_status_return;
    I3 = R0;
    R0 = DM(_dl_beat);
    R3 = 15;
    R0 = R0 AND R3;
    IF EQ JUMP _dl_relay_control_reply;
    R5 = R0;
    I3 = _relay_request;
    R0 = DM(0,I3);
    R3 = 0x524c5831;
    COMP(R0,R3);
    IF NE JUMP _dl_relay_status_return;
    R3 = 8;
    COMP(R5,R3);
    IF GE JUMP _dl_relay_transfer_reply;
    R0 = _phrase_control_info;
    R0 = PASS R0;
    IF EQ JUMP _dl_relay_keep_loop;
    I3 = R0;
    R3 = DM(4,I3);
    R3 = PASS R3;
    IF NE JUMP _dl_relay_status_return;
_dl_relay_keep_loop:
    R3 = 4;
    COMP(R5,R3);
    IF NE JUMP _dl_relay_transfer_reply;
    R0 = _loop_ui_status;
    R0 = PASS R0;
    IF NE JUMP _dl_relay_status_return;
_dl_relay_transfer_reply:
    I3 = _relay_response;
    R0 = DM(0,I3);
    R3 = 0x524c5832;
    COMP(R0,R3);
    IF NE JUMP _dl_relay_status_return;
    JUMP _dl_relay_copy;
_dl_relay_control_reply:
    R0 = DM(0,I3);
    R3 = 0x524c5932;
    COMP(R0,R3);
    IF NE JUMP _dl_relay_status_return;
_dl_relay_copy:
    R0 = I4;
    I2 = R0;
    R0 = DM(0,I3); DM(381,I2) = R0;
    R0 = DM(1,I3); DM(382,I2) = R0;
    R0 = DM(2,I3); DM(402,I2) = R0;
    R0 = DM(3,I3); DM(403,I2) = R0;
    R0 = DM(4,I3); DM(423,I2) = R0;
    R0 = DM(5,I3); DM(424,I2) = R0;
    R0 = DM(6,I3); DM(444,I2) = R0;
    R0 = DM(7,I3); DM(445,I2) = R0;
    R0 = DM(8,I3); DM(633,I2) = R0;
    R0 = DM(9,I3); DM(634,I2) = R0;
    R0 = DM(10,I3); DM(654,I2) = R0;
    R0 = DM(11,I3); DM(655,I2) = R0;
    R0 = DM(12,I3); DM(675,I2) = R0;
    R0 = DM(13,I3); DM(676,I2) = R0;
    JUMP _dl_relay_status_return;
