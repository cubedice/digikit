// Admitted V2 process call. R4=row index, R5=instance ordinal, R8=trusted
// adapter-owned dk_instrument_in in L2. Called through the protected wrapper.
// A READY row and matching installed owner/generation are both mandatory.
// Input/event/control storage is owned and bounded by the adapter, never MIDI.
// Output is a dedicated 32-float L2 scratch buffer. No note clears state.
.SECTION/PM seg_pmco;
.GLOBAL _ir_process_row;
.EXTERN _ir_rows;
.EXTERN _ir_process_rows;
_ir_process_row:
    R0 = 7;
    R0 = R4 AND R0;
    COMP(R0, R4);
    IF NE JUMP _ip_invalid;
    R0 = LSHIFT R4 BY 6;
    R1 = _ir_rows;
    R0 = R0 + R1;
    DM(_ip_row) = R0;
    I0 = R0;
    R0 = DM(0, I0);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _ip_invalid;
    R0 = PASS R5;
    IF LT JUMP _ip_invalid;
    R0 = DM(3, I0);
    COMP(R5, R0);
    IF GE JUMP _ip_invalid;
    R0 = LSHIFT R4 BY 5;
    R1 = _ir_process_rows;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(0, I1);
    R1 = DM(1, I0);
    COMP(R0, R1);
    IF NE JUMP _ip_invalid;
    R0 = DM(1, I1);
    R1 = DM(2, I0);
    COMP(R0, R1);
    IF NE JUMP _ip_invalid;
    R0 = 3;
    R0 = R8 AND R0;
    IF NE JUMP _ip_invalid;
    R0 = 0x20000000;
    COMP(R8, R0);
    IF LT JUMP _ip_invalid;
    // Adapter buffers may be statically linked above the dynamic code arena.
    // Bound them by bootable L2, excluding the boot ROM workspace at 0x200fe000.
    R0 = 0x200fdfb0;
    COMP(R8, R0);
    IF GT JUMP _ip_invalid;
    I2 = R8;
    R0 = DM(0, I2);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _ip_v2;
    R1 = 3;
    COMP(R0, R1);
    IF EQ JUMP _ip_v3;
    R1 = 4;
    COMP(R0, R1);
    IF NE JUMP _ip_invalid;
    R0 = I2;
    R1 = 0x200fdfa8;
    COMP(R0, R1);
    IF GT JUMP _ip_invalid;
    R0 = DM(1, I2);
    R1 = 88;
    JUMP _ip_size;
_ip_v3:
    R0 = DM(1, I2);
    R1 = 80;
    JUMP _ip_size;
_ip_v2:
    R0 = DM(1, I2);
    R1 = 72;
_ip_size:
    COMP(R0, R1);
    IF NE JUMP _ip_invalid;
    R0 = DM(8, I2);
    R1 = DM(6, I1);
    COMP(R0, R1);
    IF GT JUMP _ip_invalid;
    R0 = PASS R0;
    IF LT JUMP _ip_invalid;
    R0 = DM(10, I2);
    R1 = DM(7, I1);
    COMP(R0, R1);
    IF GT JUMP _ip_invalid;
    R0 = PASS R0;
    IF LT JUMP _ip_invalid;
    R2 = DM(5, I2);
    R0 = 3;
    R0 = R2 AND R0;
    IF NE JUMP _ip_invalid;
    R0 = 0x20000000;
    COMP(R2, R0);
    IF LT JUMP _ip_invalid;
    R0 = 0x200fdf80;
    COMP(R2, R0);
    IF GT JUMP _ip_invalid;
    DM(_ip_output) = R2;
    R4 = DM(4, I0);
    R0 = DM(5, I0);
    R0 = R0 + 1;
    R0 = LSHIFT R0 BY -1;
    R0 = LSHIFT R0 BY 3;
    R5 = PASS R5;
    IF EQ JUMP _ip_call;
_ip_state:
    R4 = R4 + R0;
    R5 = R5 - 1;
    IF NE JUMP _ip_state;
_ip_call:
    R0 = DM(3, I1);
    I12 = R0;
    I13 = _ip_return;
    JUMP (M13, I12);
_ip_return:
    R0 = PASS R0;
    IF LT JUMP _ip_fault;
    R1 = 1;
    COMP(R0, R1);
    IF LE RTS;
    R0 = -1;
_ip_fault:
    DM(_ip_error) = R0;
    R1 = DM(_ip_row);
    I0 = R1;
    R1 = 5;
    DM(0, I0) = R1;
    DM(12, I0) = R0;
    R1 = DM(_ip_output);
    I0 = R1;
    R1 = 32;
    R0 = 0;
_ip_silence:
    DM(I0, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _ip_silence;
    R0 = DM(_ip_error);
    RTS;
_ip_invalid:
    R0 = -1;
    RTS;
.PKBSS _ip_row, 4;
.PKBSS _ip_output, 4;
.PKBSS _ip_error, 4;
