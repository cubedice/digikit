#include "dk_phrase.inc"
.SECTION/PM seg_pmco;
.GLOBAL _phrase_receive;
.GLOBAL _phrase_reply;
// Called inside the position hook's saved-register envelope. I3 is RX ring.
_phrase_receive:
    R0 = I3;
    R1 = 0x8b4;
    R5 = R0 + R1;
    R0 = R5;
    I0 = R0;
    R1 = DM(0,I0);
    R2 = PH_CONTROL_MAGIC;
    COMP(R1,R2);
    IF NE JUMP _ph_rx_bad;
    R1 = DM(1,I0);
    R2 = PH_VERSION;
    COMP(R1,R2);
    IF NE JUMP _ph_rx_bad;
    R1 = DM(2,I0);
    R2 = PH_CONTROL_WORDS * 4;
    COMP(R1,R2);
    IF NE JUMP _ph_rx_bad;
    R1 = DM(3,I0);
    R2 = DM(62,I0);
    COMP(R1,R2);
    IF NE JUMP _ph_rx_bad;
    R2 = DM(_ph_last_sequence);
    R5 = R1 - R2;
    R2 = R5;
    R2 = PASS R2;
    IF LE JUMP _ph_rx_bad;
    R3 = PH_CHECK_SALT;
    R4 = 63;
_ph_rx_check:
    R2 = DM(0,I0);
    R5 = R3 XOR R2;
    R3 = R5;
    MODIFY(I0,1) (NW);
    R4 = R4 - 1;
    IF GT JUMP _ph_rx_check;
    R2 = DM(0,I0);
    COMP(R3,R2);
    IF NE JUMP _ph_rx_bad;
    DM(_ph_last_sequence) = R1;
    R2 = 0;
    DM(_ph_refused) = R2;
    I0 = R0;
    I1 = _phrase_control_info;
    R4 = PH_CONTROL_WORDS;
_ph_rx_copy:
    R2 = DM(0,I0);
    DM(0,I1) = R2;
    MODIFY(I0,1) (NW);
    MODIFY(I1,1) (NW);
    R4 = R4 - 1;
    IF GT JUMP _ph_rx_copy;
    RTS;
// A refused frame leaves the last good mailbox in place: on the device a frame can come twice or late when the ColdFire's
// block interrupt is held off, and the engine goes on with the controls it has. Only PH_RX_GRACE refusals in a row end
// the link: then the mailbox loses its magic and enabled word, and the engine is silent until a good frame comes.
_ph_rx_bad:
    R0 = DM(_ph_refused);
    R2 = PH_RX_GRACE;
    COMP(R0,R2);
    IF GE JUMP _ph_rx_dead;
    R0 = R0 + 1;
    DM(_ph_refused) = R0;
    COMP(R0,R2);
    IF LT RTS;
_ph_rx_dead:
    R0 = 0;
    DM(_phrase_control_info + PH_C_MAGIC) = R0;
    DM(_phrase_control_info + PH_C_ENABLED) = R0;
    RTS;

// Called after the legacy loop carrier has been written. Three of each four
// frames replace only those14 established cells; diagnostics and instruments
// keep all their cells. A full snapshot is latched once, not torn across pages.
_phrase_reply:
    R0 = DM(_ph_reply_phase);
    R0 = R0 + 1;
    R3 = 3;
    R5 = R0 AND R3;
    R0 = R5;
    DM(_ph_reply_phase) = R0;
    IF EQ RTS;
    R3 = DM(_phrase_control_info + PH_C_ENABLED);
    R3 = PASS R3;
    IF EQ RTS;
    R3 = L5;
    DM(_ph_reply_saved_l5) = R3;
    L5 = 0;
    R3 = 1;
    COMP(R0,R3);
    IF NE JUMP _ph_reply_page;
    I2 = _phrase_status;
    I3 = _ph_reply_snapshot;
    R3 = PH_STATUS_WORDS;
_ph_reply_latch:
    R4 = DM(0,I2);
    DM(0,I3) = R4;
    MODIFY(I2,1) (NW);
    MODIFY(I3,1) (NW);
    R3 = R3 - 1;
    IF GT JUMP _ph_reply_latch;
    R3 = DM(_ph_reply_serial);
    R3 = R3 + 1;
    DM(_ph_reply_serial) = R3;
_ph_reply_page:
    R0 = R0 - 1;
    R3 = DM(_ph_reply_serial);
    R3 = LSHIFT R3 BY 2;
    R5 = R3 OR R0;
    R3 = R5;
    DM(_ph_reply_packet + 4) = R3;
    R3 = PH_REPLY_MAGIC;
    DM(_ph_reply_packet) = R3;
    R3 = LSHIFT R0 BY 5;
    R4 = LSHIFT R0 BY 4;
    R0 = R3 + R4;
    R3 = _ph_reply_snapshot;
    R5 = R0 + R3;
    R0 = R5;
    I2 = R0;
    R3 = _ph_reply_packet;
    R4 = 8;
    R5 = R3 + R4;
    I3 = R5;
    R3 = 12;
_ph_reply_payload:
    R4 = DM(0,I2);
    DM(0,I3) = R4;
    MODIFY(I2,1) (NW);
    MODIFY(I3,1) (NW);
    R3 = R3 - 1;
    IF GT JUMP _ph_reply_payload;
    I3 = _ph_reply_packet;
    I5 = _ph_reply_offsets;
    R3 = 14;
_ph_reply_emit:
    R4 = DM(0,I5);
    R0 = I4;
    R5 = R0 + R4;
    R4 = R5;
    I2 = R4;
    R4 = DM(0,I3);
    DM(0,I2) = R4;
    MODIFY(I3,1) (NW);
    MODIFY(I5,1) (NW);
    R3 = R3 - 1;
    IF GT JUMP _ph_reply_emit;
    R3 = DM(_ph_reply_saved_l5);
    L5 = R3;
    RTS;

.SECTION/DM seg_dmda;
.VAR _ph_reply_offsets = 0x5f4;
.VAR _ph_reply_offset1 = 0x5f8;
.VAR _ph_reply_offset2 = 0x648;
.VAR _ph_reply_offset3 = 0x64c;
.VAR _ph_reply_offset4 = 0x69c;
.VAR _ph_reply_offset5 = 0x6a0;
.VAR _ph_reply_offset6 = 0x6f0;
.VAR _ph_reply_offset7 = 0x6f4;
.VAR _ph_reply_offset8 = 0x9e4;
.VAR _ph_reply_offset9 = 0x9e8;
.VAR _ph_reply_offset10 = 0xa38;
.VAR _ph_reply_offset11 = 0xa3c;
.VAR _ph_reply_offset12 = 0xa8c;
.VAR _ph_reply_offset13 = 0xa90;
.PKDATA _phrase_control_info, 256;
.PKDATA _phrase_status, 128;
.PKDATA _ph_last_sequence, 4;
.PKDATA _ph_refused, 4;
.PKDATA _ph_reply_phase, 4;
.PKDATA _ph_reply_serial, 4;
.PKDATA _ph_reply_snapshot, 144;
.PKDATA _ph_reply_packet, 56;
.PKDATA _ph_reply_saved_l5, 4;
