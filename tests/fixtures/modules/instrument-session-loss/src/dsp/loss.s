// Emulator-only fault injection before the production reply builder.
// Lose the bound token, then restore it with the original ACK still present.
// This is not a hardware reset: code, mailbox sequence and CF remain alive.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_carrier_prepare;
.EXTERN _instrument_session;
_instrument_carrier_prepare:
    R0 = DM(_isp_loss_phase);
    R0 = PASS R0;
    IF NE JUMP _isp_count;
    R0 = DM(_instrument_session);
    R0 = PASS R0;
    IF EQ RTS;
    DM(_isp_old_session) = R0;
    R0 = 1;
    DM(_isp_loss_phase) = R0;
    RTS;
_isp_count:
    R1 = 3;
    COMP(R0, R1);
    IF EQ RTS;
    R0 = DM(_isp_loss_blocks);
    R0 = R0 + 1;
    DM(_isp_loss_blocks) = R0;
    R1 = DM(_isp_loss_phase);
    R1 = R1 - 1;
    IF NE JUMP _isp_restore;
    R1 = 2048;
    COMP(R0, R1);
    IF LT RTS;
    R0 = 0;
    DM(_instrument_session) = R0;
    DM(_isp_loss_blocks) = R0;
    R0 = 2;
    DM(_isp_loss_phase) = R0;
    RTS;
_isp_restore:
    R1 = 64;
    COMP(R0, R1);
    IF LT RTS;
    R0 = DM(_isp_old_session);
    DM(_instrument_session) = R0;
    R0 = 3;
    DM(_isp_loss_phase) = R0;
    RTS;
.SECTION/DM seg_dmda;
.VAR _isp_loss_phase = 0;
.VAR _isp_loss_blocks = 0;
.VAR _isp_old_session = 0;
