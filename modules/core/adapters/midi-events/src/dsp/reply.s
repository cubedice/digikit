// Bootstrap ACK-only reply, not the final CANOPY UI status schema.
// Called once per DSP block after receive/application. Scratch R0..R6/I0;
// caller pins non-SIMD, L0=0 and M5=0/M6=1. No firmware hook installed yet.
// Magic is published last; checksum covers words 0..14. The public DSP-link
// mailbox name is intentionally withheld until both frame directions are wired.
.SECTION/PM seg_pmco;
.GLOBAL _mi_reply_build;
.EXTERN _mi_epoch;
.EXTERN _mi_message;
.EXTERN _mi_reply;
_mi_reply_build:
    I0 = _mi_reply;
    R0 = 0;
    DM(0, I0) = R0;
    R0 = DM(_mi_epoch);
    DM(1, I0) = R0;
    R0 = DM(_mi_message);
    DM(2, I0) = R0;
    R0 = DM(_mi_heartbeat);
    R0 = R0 + 1;
    DM(_mi_heartbeat) = R0;
    DM(3, I0) = R0;
    MODIFY(I0, 4) (NW);
    R0 = 0;
    R3 = 11;
_mr_clear:
    DM(I0, M6) = R0;
    R3 = R3 - 1;
    IF NE JUMP _mr_clear;
    I0 = _mi_reply;
    MODIFY(I0, 1) (NW);
    R1 = 0x494ea101;
    R2 = R1;
    R3 = 14;
_mr_sum:
    R0 = DM(I0, M6);
    R1 = R1 + R0;
    R2 = R2 + R1;
    R3 = R3 - 1;
    IF NE JUMP _mr_sum;
    R0 = R1 XOR R2;
    DM(I0, M5) = R0;
    I0 = _mi_reply;
    R0 = 0x494ea101;
    DM(0, I0) = R0;
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _mi_heartbeat;
.VAR _mi_heartbeat = 0;
.PKBSS _mi_reply, 64;
