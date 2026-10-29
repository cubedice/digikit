// Bounded configuration staging. Call only after packet validation and the
// ordered receiver's epoch/generation checks. R4 = CHUNK/COMMIT packet.
// Return R0=1 buffered/applied, 0 rejected. Scratch R0..R15/I0..I2;
// caller pins non-SIMD, L0..L2=0, M5=0/M6=1. Non-reentrant.
// _mi_config_publish(R4=64-word staging buffer, R8=7-word metadata) validates
// schema/ownership and atomically copies or swaps into consumer-owned storage.
// It must NOT retain staging pointers: the next transaction reuses this buffer.
// Metadata: track, generation, revision, zero, byte length, schema, checksum.
// Return 1 only when applied. Missing/rejecting publisher leaves active settings
// untouched and does not acknowledge COMMIT. RESET must call _mi_config_abort.
.SECTION/PM seg_pmco;
.GLOBAL _mi_config_apply;
.GLOBAL _mi_config_abort;
.EXTERN _mi_config_publish;
_mi_config_abort:
    R0 = 0;
    DM(_mi_config_active) = R0;
    DM(_mi_config_next) = R0;
    RTS;
_mi_config_apply:
    I0 = R4;
    R0 = DM(1, I0);
    R1 = 3;
    COMP(R0, R1);
    IF EQ JUMP _mc_chunk;
    R1 = 4;
    COMP(R0, R1);
    IF NE JUMP _mc_reject;
    JUMP _mc_match;
_mc_chunk:
    R0 = DM(11, I0);
    R0 = PASS R0;
    IF NE JUMP _mc_match;
    // First chunk starts/replaces staging. Never changes the applied bank.
    I1 = _mi_config_meta;
    MODIFY(I0, 8) (NW);
    R5 = 7;
_mc_meta_copy:
    R0 = DM(I0, M6);
    DM(I1, M6) = R0;
    R5 = R5 - 1;
    IF NE JUMP _mc_meta_copy;
    R0 = 1;
    DM(_mi_config_active) = R0;
    R0 = 0;
    DM(_mi_config_next) = R0;
    I0 = R4;
_mc_match:
    R0 = DM(_mi_config_active);
    R0 = PASS R0;
    IF EQ JUMP _mc_reject;
    R0 = DM(11, I0);
    R1 = DM(_mi_config_next);
    COMP(R0, R1);
    IF NE JUMP _mc_reject;
    I1 = _mi_config_meta;
    MODIFY(I0, 8) (NW);
    R5 = 0;
_mc_meta_check:
    R0 = DM(I0, M6);
    R1 = DM(I1, M6);
    R2 = 3; // ordinal changes across chunks, other metadata cannot
    COMP(R5, R2);
    IF EQ JUMP _mc_meta_next;
    COMP(R0, R1);
    IF NE JUMP _mc_reject;
_mc_meta_next:
    R5 = R5 + 1;
    R0 = 7;
    COMP(R5, R0);
    IF LT JUMP _mc_meta_check;
    I0 = R4;
    R0 = DM(1, I0);
    R1 = 4;
    COMP(R0, R1);
    IF EQ JUMP _mc_commit;
    R5 = DM(15, I0);
    R0 = DM(11, I0);
    R0 = LSHIFT R0 BY 6; // chunk * 16 words * 4 bytes
    R1 = _mi_config_data;
    R0 = R0 + R1;
    I1 = R0;
    MODIFY(I0, 16) (NW);
_mc_data_copy:
    R0 = DM(I0, M6);
    DM(I1, M6) = R0;
    R5 = R5 - 1;
    IF NE JUMP _mc_data_copy;
    R0 = DM(_mi_config_next);
    R0 = R0 + 1;
    DM(_mi_config_next) = R0;
    R0 = 1;
    RTS;
_mc_commit:
    R5 = DM(12, I0);
    R5 = LSHIFT R5 BY -2;
    R6 = DM(14, I0);
    I1 = _mi_config_data;
    R1 = 0;
    R2 = 0;
_mc_sum:
    R0 = DM(I1, M6);
    R1 = R1 + R0;
    R2 = R2 + R1;
    R5 = R5 - 1;
    IF NE JUMP _mc_sum;
    R0 = R1 XOR R2;
    COMP(R0, R6);
    IF NE JUMP _mc_reject;
    R0 = _mi_config_publish;
    R0 = PASS R0;
    IF EQ JUMP _mc_reject;
    R4 = _mi_config_data;
    R8 = _mi_config_meta;
    CALL _mi_config_publish;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _mc_reject;
    R0 = 0;
    DM(_mi_config_active) = R0;
    DM(_mi_config_next) = R0;
    R0 = 1;
    RTS;
_mc_reject:
    R0 = 0;
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _mi_config_active;
.GLOBAL _mi_config_next;
.EXTERN _mi_config_meta;
.EXTERN _mi_config_data;
.VAR _mi_config_active = 0;
.VAR _mi_config_next = 0;
.PKBSS _mi_config_meta, 28;
.PKBSS _mi_config_data, 256;
