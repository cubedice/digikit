// Trusted adapter boundary. normalize: R4=ordered packet, R8=borrowed V2 input,
// R12=bound transport epoch. Returns count (1..8), or -1 without changing input
// or destination events. The receiver still owns replay/order/ACK processing.
// Scalar context, M5=0/M6=1/L0..2=0; clobbers R0..15/I0..2. Not reentrant.
.SECTION/PM seg_pmco;
.GLOBAL _ir_normalize_packet;
.EXTERN _mi_packet_valid;
_ir_normalize_packet:
    DM(_in_packet) = R4;
    DM(_in_input) = R8;
    DM(_in_epoch) = R12;
    CALL _mi_packet_valid;
    R0 = PASS R0;
    IF EQ JUMP _in_bad;
    R0 = DM(_in_input);
    R1 = 3;
    R1 = R0 AND R1;
    IF NE JUMP _in_bad;
    R1 = 0x20000000;
    COMP(R0, R1);
    IF LT JUMP _in_bad;
    R1 = 0x200fdfb0;
    COMP(R0, R1);
    IF GT JUMP _in_bad;
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _in_v2;
    R1 = 3;
    COMP(R0, R1);
    IF EQ JUMP _in_v3;
    R1 = 4;
    COMP(R0, R1);
    IF EQ JUMP _in_v4;
    R1 = 5;
    COMP(R0, R1);
    IF NE JUMP _in_bad;
    // The process owner negotiates V5; normalization never accesses its sink.
    R0 = I1;
    R1 = 0x200fdfa0;
    COMP(R0, R1);
    IF GT JUMP _in_bad;
    R0 = DM(1, I1);
    R1 = 96;
    JUMP _in_size;
_in_v4:
    R0 = I1;
    R1 = 0x200fdfa8;
    COMP(R0, R1);
    IF GT JUMP _in_bad;
    R0 = DM(1, I1);
    R1 = 88;
    JUMP _in_size;
_in_v3:
    R0 = DM(1, I1);
    R1 = 80;
    JUMP _in_size;
_in_v2:
    R0 = DM(1, I1);
    R1 = 72;
_in_size:
    COMP(R0, R1);
    IF NE JUMP _in_bad;
    R0 = DM(_in_packet);
    I0 = R0;
    R0 = DM(1, I0);
    R1 = FEXT R0 BY 0:8;
    R2 = 1;
    COMP(R1, R2);
    IF EQ JUMP _in_plain;
    R2 = 5;
    COMP(R1, R2);
    IF NE JUMP _in_bad;
    R2 = DM(0, I1);
    R1 = 3;
    COMP(R2, R1);
    IF LT JUMP _in_bad;
    R1 = DM(4, I0);
    JUMP _in_meta;
_in_plain:
    R1 = 0;
_in_meta:
    DM(_in_movement) = R1;
    R2 = FEXT R0 BY 8:4;
    DM(_in_count) = R2;
    R0 = DM(2, I0);
    R1 = DM(_in_epoch);
    COMP(R0, R1);
    IF NE JUMP _in_bad;
    R0 = DM(5, I0);
    R1 = DM(11, I1);
    COMP(R0, R1);
    IF NE JUMP _in_bad;
    R0 = DM(9, I0);
    R1 = DM(3, I1);
    COMP(R0, R1);
    IF NE JUMP _in_bad;
    R0 = DM(10, I0);
    R0 = FEXT R0 BY 10:4;
    R1 = DM(2, I1);
    COMP(R0, R1);
    IF NE JUMP _in_bad;
    R0 = DM(7, I1);
    R1 = 3;
    R1 = R0 AND R1;
    IF NE JUMP _in_bad;
    R1 = 0x20000000;
    COMP(R0, R1);
    IF LT JUMP _in_bad;
    R1 = 0x200fdf40;
    COMP(R0, R1);
    IF GT JUMP _in_bad;
    MODIFY(I0, 8) (NW);
    I2 = _in_staging;
_in_event:
    R6 = DM(I0, M6);
    MODIFY(I0, 1) (NW);
    R7 = DM(I0, M6);
    R3 = FEXT R7 BY 0:4;
    R4 = FEXT R7 BY 4:2;
    R4 = LSHIFT R4 BY 8;
    R5 = FEXT R7 BY 6:4;
    R4 = R4 OR R5;
    R8 = FEXT R7 BY 14:7;
    R9 = FEXT R7 BY 21:7;
    R0 = 2;
    COMP(R3, R0);
    IF GT JUMP _in_controller;
    R0 = 1;
    COMP(R3, R0);
    IF NE JUMP _in_note;
    R9 = PASS R9;
    IF EQ JUMP _in_bad; // MIDI zero-velocity ON is normalized to OFF by CF
_in_note:
    R8 = LSHIFT R8 BY 8;
    R9 = LSHIFT R9 BY 8;
    JUMP _in_write;
_in_controller:
    R0 = 4;
    COMP(R3, R0);
    IF NE JUMP _in_write;
    R9 = LSHIFT R9 BY 7;
    R9 = R9 OR R8;
    R8 = 0;
_in_write:
    DM(I2, M6) = R3;
    DM(I2, M6) = R4;
    DM(I2, M6) = R6;
    DM(I2, M6) = R8;
    DM(I2, M6) = R9;
    R0 = 0;
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _in_event;
    R0 = DM(_in_input);
    I0 = R0;
    R0 = DM(7, I0);
    I1 = R0;
    I2 = _in_staging;
    R2 = DM(_in_count);
    R0 = LSHIFT R2 BY 1;
    R2 = R2 + R0;
    R2 = LSHIFT R2 BY 1;
_in_commit:
    R0 = DM(I2, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _in_commit;
    R0 = DM(0, I0);
    R1 = 3;
    COMP(R0, R1);
    IF LT JUMP _in_committed;
    R0 = DM(_in_movement);
    DM(18, I0) = R0;
_in_committed:
    R0 = DM(_in_count);
    DM(8, I0) = R0;
    RTS;
_in_bad:
    R0 = -1;
    RTS;
.PKBSS _in_packet, 4;
.PKBSS _in_input, 4;
.PKBSS _in_epoch, 4;
.PKBSS _in_count, 4;
.PKBSS _in_staging, 192;

// One frame: R4=row, R5=instance, R8=trusted input, R12=ordered packet or zero,
// R9=bound epoch. Dedicated adapter event storage is 8*6 words. Preclear audio,
// normalize before calling, never invoke an instrument for a rejected packet.
// The caller owns route/revision lifetime and calls this at most once per block.
.SECTION/PM seg_pmco;
.GLOBAL _ir_process_packet;
.EXTERN _ir_process_protected;
_ir_process_packet:
    DM(_if_row) = R4;
    DM(_if_instance) = R5;
    DM(_if_input) = R8;
    DM(_if_packet) = R12;
    DM(_if_epoch) = R9;
    R0 = R8;
    R1 = 3;
    R1 = R0 AND R1;
    IF NE JUMP _in_bad;
    R1 = 0x20000000;
    COMP(R0, R1);
    IF LT JUMP _in_bad;
    R1 = 0x200fdfb0;
    COMP(R0, R1);
    IF GT JUMP _in_bad;
    I0 = R8;
    R2 = DM(5, I0);
    R0 = 3;
    R0 = R2 AND R0;
    IF NE JUMP _in_bad;
    R0 = 0x20000000;
    COMP(R2, R0);
    IF LT JUMP _in_bad;
    R0 = 0x200fdf80;
    COMP(R2, R0);
    IF GT JUMP _in_bad;
    I1 = R2;
    R0 = 0;
    DM(8, I0) = R0;
    R1 = 32;
_if_clear:
    DM(I1, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _if_clear;
    R4 = DM(_if_packet);
    R4 = PASS R4;
    IF EQ JUMP _if_render;
    R2 = DM(7, I0);
    R0 = 3;
    R0 = R2 AND R0;
    IF NE JUMP _in_bad;
    R0 = 0x20000000;
    COMP(R2, R0);
    IF LT JUMP _in_bad;
    R0 = 0x200fdf40;
    COMP(R2, R0);
    IF GT JUMP _in_bad;
    R8 = DM(_if_input);
    R12 = DM(_if_epoch);
    CALL _ir_normalize_packet;
    R0 = PASS R0;
    IF LT RTS;
_if_render:
    R4 = DM(_if_row);
    R5 = DM(_if_instance);
    R8 = DM(_if_input);
    CALL _ir_process_protected;
    RTS;
.PKBSS _if_row, 4;
.PKBSS _if_instance, 4;
.PKBSS _if_input, 4;
.PKBSS _if_packet, 4;
.PKBSS _if_epoch, 4;

.PKBSS _in_movement, 4;
