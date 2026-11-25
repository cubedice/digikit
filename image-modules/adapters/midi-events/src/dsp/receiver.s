// Internal ordered receiver. Same register context as _mi_packet_valid; R4 is
// the packet. Non-reentrant. Caller preserves scratch registers around this.
// _mi_apply_packet returns R0=1 ONLY after applying the entire validated packet
// atomically. It validates track/generation/configuration and rejects without
// side effects otherwise. A reset with a newer epoch clears ALL old instances.
// No consumer means no ACK and no routing readiness.
// Returns 0 rejected/unready, 1 applied, 2 duplicate, 3 gap, 4 stale identity.
.SECTION/PM seg_pmco;
.GLOBAL _mi_receive;
.EXTERN _mi_packet_valid;
.EXTERN _mi_apply_packet;
_mi_receive:
    DM(_mi_packet) = R4;
    CALL _mi_packet_valid;
    R0 = PASS R0;
    IF EQ JUMP _mi_rejected;
    R4 = DM(_mi_packet);
    I0 = R4;
    R5 = DM(2, I0);
    R6 = DM(3, I0);
    R7 = DM(_mi_epoch);
    R8 = DM(_mi_message);
    R0 = 0x80000000;
    R1 = R5 XOR R0;
    R2 = R7 XOR R0;
    COMP(R1, R2);
    IF LT JUMP _mi_stale;
    IF GT JUMP _mi_new_epoch;
    R1 = R6 XOR R0;
    R2 = R8 XOR R0;
    COMP(R1, R2);
    IF LT JUMP _mi_stale;
    IF EQ JUMP _mi_duplicate;
    R0 = DM(1, I0);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _mi_apply; // reset may jump a gap
    R8 = R8 + 1;
    COMP(R6, R8);
    IF NE JUMP _mi_gap;
    JUMP _mi_apply;
_mi_new_epoch:
    R0 = DM(1, I0);
    R1 = 2;
    COMP(R0, R1);
    IF NE JUMP _mi_gap; // a fresh stream must establish reset before notes
_mi_apply:
    R0 = _mi_apply_packet;
    R0 = PASS R0;
    IF EQ JUMP _mi_rejected;
    CALL _mi_apply_packet;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _mi_rejected;
    R4 = DM(_mi_packet);
    I0 = R4;
    R0 = DM(2, I0);
    DM(_mi_epoch) = R0;
    R0 = DM(3, I0);
    DM(_mi_message) = R0;
    R0 = 1;
    RTS;
_mi_duplicate:
    R0 = 2;
    RTS;
_mi_gap:
    R0 = 3;
    RTS;
_mi_stale:
    R0 = 4;
    RTS;
_mi_rejected:
    R0 = 0;
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _mi_epoch;
.GLOBAL _mi_message;
.VAR _mi_epoch = 0;
.VAR _mi_message = 0;
.VAR _mi_packet = 0;
