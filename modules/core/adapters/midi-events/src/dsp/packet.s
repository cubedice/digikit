// Internal instrument packet validator. R4 = 32-word packet byte address;
// returns R0 = 1 valid, 0 invalid. Does not publish/ACK or change memory.
// Caller pins non-SIMD, L0=0, M5=0, M6=1. Clobbers R0..R15/I0 only.
// No firmware hook until the ordered receiver and V2 consumer are installed.
.SECTION/PM seg_pmco;
.GLOBAL _mi_packet_valid;
_mi_packet_valid:
    I0 = R4;
    R0 = DM(0, I0);
    R1 = 0x494e0101;
    COMP(R0, R1);
    IF NE JUMP _mi_bad;
    R0 = DM(2, I0);
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    R0 = DM(3, I0);
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    // Fletcher-style checksum, excluding word 7, includes zero padding.
    R1 = 0;
    R2 = 0;
    R3 = 0;
_mi_sum:
    R0 = 7;
    COMP(R3, R0);
    IF EQ JUMP _mi_skip_check;
    R0 = DM(I0, M5);
    R1 = R1 + R0;
    R2 = R2 + R1;
_mi_skip_check:
    MODIFY(I0, 1) (NW);
    R3 = R3 + 1;
    R0 = 32;
    COMP(R3, R0);
    IF LT JUMP _mi_sum;
    I0 = R4;
    R0 = DM(7, I0);
    R1 = R1 XOR R2;
    COMP(R0, R1);
    IF NE JUMP _mi_bad;
    R0 = DM(1, I0);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _mi_reset_packet;
    R1 = 3;
    COMP(R0, R1);
    IF EQ JUMP _mi_config_packet;
    R1 = 4;
    COMP(R0, R1);
    IF EQ JUMP _mi_config_packet;
    R5 = FEXT R0 BY 8:4; // count
    R1 = LSHIFT R5 BY 8;
    R1 = BSET R1 BY 0;
    COMP(R0, R1); // exactly EVENTS | count << 8
    IF NE JUMP _mi_bad;
    R5 = PASS R5;
    IF EQ JUMP _mi_bad;
    R1 = 8;
    COMP(R5, R1);
    IF GT JUMP _mi_bad;
    R0 = DM(5, I0); // committed configuration revision
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    R0 = DM(4, I0); // first event sequence; range cannot wrap
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    R1 = R5 - 1;
    R1 = R0 + R1;
    R2 = 0x80000000;
    R1 = R1 XOR R2;
    R0 = R0 XOR R2;
    COMP(R1, R0);
    IF LT JUMP _mi_bad;
    R6 = DM(9, I0); // generation shared by entire batch
    R6 = PASS R6;
    IF EQ JUMP _mi_bad;
    R7 = DM(10, I0);
    R7 = FEXT R7 BY 10:4;
    MODIFY(I0, 8) (NW);
    R8 = R5;
_mi_event:
    R9 = DM(I0, M6); // token
    R0 = DM(I0, M6); // generation
    COMP(R0, R6);
    IF NE JUMP _mi_bad;
    R10 = DM(I0, M6); // arguments
    R0 = FEXT R10 BY 28:4;
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R0 = FEXT R10 BY 4:2;
    R1 = 1;
    COMP(R0, R1);
    IF GT JUMP _mi_bad;
    R0 = FEXT R10 BY 10:4;
    COMP(R0, R7);
    IF NE JUMP _mi_bad;
    R0 = FEXT R10 BY 0:4;
    R1 = 1;
    COMP(R0, R1);
    IF LT JUMP _mi_bad;
    R1 = 4;
    COMP(R0, R1);
    IF GT JUMP _mi_bad;
    R1 = 2;
    COMP(R0, R1);
    IF GT JUMP _mi_controller;
    R9 = PASS R9;
    IF EQ JUMP _mi_bad;
    JUMP _mi_next_event;
_mi_controller:
    R9 = PASS R9;
    IF NE JUMP _mi_bad;
_mi_next_event:
    R8 = R8 - 1;
    IF NE JUMP _mi_event;
    R0 = LSHIFT R5 BY 1;
    R0 = R0 + R5;
    R1 = 24;
    R8 = R1 - R0; // remaining zero padding
    JUMP _mi_padding;
_mi_reset_packet:
    R0 = DM(4, I0);
    R1 = DM(5, I0);
    R0 = R0 OR R1;
    IF NE JUMP _mi_bad;
    R0 = DM(8, I0);
    R0 = LSHIFT R0 BY -4;
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R0 = DM(9, I0);
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    MODIFY(I0, 10) (NW);
    R8 = 22;
_mi_padding:
    R8 = PASS R8;
    IF EQ JUMP _mi_good;
    R0 = DM(I0, M6);
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R8 = R8 - 1;
    JUMP _mi_padding;
_mi_good:
    R0 = 1;
    RTS;
_mi_bad:
    R0 = 0;
    RTS;

_mi_config_packet:
    R10 = R0; // CHUNK or COMMIT
    R0 = DM(4, I0);
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R0 = DM(8, I0);
    R0 = LSHIFT R0 BY -4;
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R0 = DM(9, I0);
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    R0 = DM(10, I0);
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    R1 = DM(5, I0);
    COMP(R0, R1);
    IF NE JUMP _mi_bad;
    R0 = DM(13, I0); // schema, interpreted by consumer
    R0 = PASS R0;
    IF EQ JUMP _mi_bad;
    R5 = DM(12, I0); // bounded byte length, 4..256, whole words
    R0 = 0x3;
    R0 = R5 AND R0;
    IF NE JUMP _mi_bad;
    R0 = LSHIFT R5 BY -9;
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R0 = 4;
    COMP(R5, R0);
    IF LT JUMP _mi_bad;
    R0 = 256;
    COMP(R5, R0);
    IF GT JUMP _mi_bad;
    R5 = LSHIFT R5 BY -2;
    R6 = DM(11, I0); // chunk ordinal
    R0 = LSHIFT R6 BY -3;
    R0 = PASS R0;
    IF NE JUMP _mi_bad;
    R7 = DM(15, I0); // payload words
    R0 = 4;
    COMP(R10, R0);
    IF EQ JUMP _mi_config_commit;
    R0 = LSHIFT R6 BY 4;
    R8 = R5 - R0;
    R0 = 1;
    COMP(R8, R0);
    IF LT JUMP _mi_bad;
    R0 = 16;
    COMP(R8, R0);
    IF GT R8 = R0;
    COMP(R7, R8);
    IF NE JUMP _mi_bad;
    JUMP _mi_config_padding;
_mi_config_commit:
    R7 = PASS R7;
    IF NE JUMP _mi_bad;
    R0 = 15;
    R0 = R5 + R0;
    R0 = LSHIFT R0 BY -4;
    COMP(R6, R0);
    IF NE JUMP _mi_bad;
_mi_config_padding:
    R0 = LSHIFT R7 BY 2;
    R0 = R4 + R0;
    I0 = R0;
    MODIFY(I0, 16) (NW);
    R0 = 16;
    R8 = R0 - R7;
    JUMP _mi_padding;
