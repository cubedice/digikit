// DT2 envelope entry interposer, non-reentrant. Not enabled by module manifest.
// R4 is the stock envelope record, I6 the stock call frame.
// workspace.s captures _mi_envelope_records; the frame owner advances the stamp.
// A zero base, non-record pointer or missing/currently unowned pair falls through.
// Save SIMD pairs in 8-byte slots; restore status and all touched input registers.
.SECTION/PM seg_pmco;
.GLOBAL _mi_envelope_entry;
.EXTERN _mi_envelope_next;
.EXTERN _mi_carrier_flags;
_mi_envelope_entry:
    DM(_me_sv_r0) = R0;
    DM(_me_sv_r1) = R1;
    DM(_me_sv_r2) = R2;
    DM(_me_sv_r3) = R3;
    DM(_me_sv_r4) = R4;
    DM(_me_sv_r9) = R9;
    DM(_me_sv_r11) = R11;
    DM(_me_sv_r12) = R12;
    R0 = I0;
    DM(_me_sv_i0) = R0;
    R0 = L0;
    DM(_me_sv_l0) = R0;
    R0 = M5;
    DM(_me_sv_m5) = R0;
    R0 = MODE1;
    DM(_me_sv_mode1) = R0;
    R0 = ASTATX;
    DM(_me_sv_astatx) = R0;
    R0 = ASTATY;
    DM(_me_sv_astaty) = R0;
    R0 = STKYX;
    DM(_me_sv_stkyx) = R0;
    R0 = STKYY;
    DM(_me_sv_stkyy) = R0;
    BIT CLR MODE1 0x202000;
    L0 = 0;
    M5 = 0;
    R1 = DM(_mi_envelope_records);
    R2 = 0;
    COMP(R1, R2);
    IF EQ JUMP _me_restore;
    R0 = R4 - R1;
    COMP(R0, R2);
    IF LT JUMP _me_restore;
    R1 = 1408; // 32 records * 44 bytes
    COMP(R0, R1);
    IF GE JUMP _me_restore;
    R4 = 0;
    R1 = 44;
_me_index:
    COMP(R0, R2);
    IF EQ JUMP _me_found;
    R0 = R0 - R1;
    IF LT JUMP _me_restore;
    R4 = R4 + 1;
    JUMP _me_index;
_me_found:
    R11 = DM(_mi_carrier_stamp);
    R12 = DM(1, I6);
    R9 = DM(2, I6);
    CALL _mi_carrier_flags;
    R3 = 0;
    COMP(R2, R3);
    IF EQ JUMP _me_restore;
    DM(1, I6) = R0;
    DM(2, I6) = R1;
_me_restore:
    R0 = DM(_me_sv_i0);
    I0 = R0;
    R0 = DM(_me_sv_l0);
    L0 = R0;
    R0 = DM(_me_sv_m5);
    M5 = R0;
    R0 = DM(_me_sv_mode1);
    MODE1 = R0;
    R0 = DM(_me_sv_astatx);
    ASTATX = R0;
    R0 = DM(_me_sv_astaty);
    ASTATY = R0;
    R0 = DM(_me_sv_stkyx);
    STKYX = R0;
    R0 = DM(_me_sv_stkyy);
    STKYY = R0;
    R1 = DM(_me_sv_r1);
    R2 = DM(_me_sv_r2);
    R3 = DM(_me_sv_r3);
    R4 = DM(_me_sv_r4);
    R9 = DM(_me_sv_r9);
    R11 = DM(_me_sv_r11);
    R12 = DM(_me_sv_r12);
    R0 = DM(_me_sv_r0);
    MODIFY(I7, -16) (NW); // displaced first instruction of DT2 1.17 0x1cbb57
    JUMP _mi_envelope_next;
.PKDATA _mi_envelope_records, 4;
.PKDATA _mi_carrier_stamp, 4;
.PKDATA _me_sv_r0, 8;
.PKDATA _me_sv_r1, 8;
.PKDATA _me_sv_r2, 8;
.PKDATA _me_sv_r3, 8;
.PKDATA _me_sv_r4, 8;
.PKDATA _me_sv_r9, 8;
.PKDATA _me_sv_r11, 8;
.PKDATA _me_sv_r12, 8;
.PKDATA _me_sv_i0, 8;
.PKDATA _me_sv_l0, 8;
.PKDATA _me_sv_m5, 8;
.PKDATA _me_sv_mode1, 8;
.PKDATA _me_sv_astatx, 8;
.PKDATA _me_sv_astaty, 8;
.PKDATA _me_sv_stkyx, 8;
.PKDATA _me_sv_stkyy, 8;
