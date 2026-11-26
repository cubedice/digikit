// DT2 1.17 gain interposer at 0x1c6a2e, after stock velocity operand load.
// Non-reentrant, uninstalled proof hook. Preserve square-law R2, status, SIMD
// partners and every input except owned R1. No stock cache writes.
.SECTION/PM seg_pmco;
.GLOBAL _mi_velocity_entry;
.EXTERN _mi_velocity_next;
.EXTERN _mi_velocity_select;
.EXTERN _mi_envelope_records;
_mi_velocity_entry:
    DM(_vg_r0) = R0;
    DM(_vg_r1) = R1;
    DM(_vg_r2) = R2;
    DM(_vg_r3) = R3;
    DM(_vg_r4) = R4;
    DM(_vg_r8) = R8;
    R0 = I0;
    DM(_vg_i0) = R0;
    R0 = L0;
    DM(_vg_l0) = R0;
    R0 = MODE1;
    DM(_vg_mode) = R0;
    R0 = ASTATX;
    DM(_vg_ax) = R0;
    R0 = ASTATY;
    DM(_vg_ay) = R0;
    R0 = STKYX;
    DM(_vg_sx) = R0;
    R0 = STKYY;
    DM(_vg_sy) = R0;
    BIT CLR MODE1 0x202000;
    L0 = 0;
    R1 = DM(_mi_envelope_records);
    R2 = 0;
    COMP(R1, R2);
    IF EQ JUMP _vg_restore;
    R2 = 0x6e4; // workspace+dce8 minus captured workspace+d604
    R1 = R1 + R2;
    R0 = I1;
    R0 = R0 - R1;
    R2 = 0;
    COMP(R0, R2);
    IF LT JUMP _vg_restore;
    R2 = 128;
    COMP(R0, R2);
    IF GE JUMP _vg_restore;
    R2 = 3;
    R2 = R0 AND R2;
    IF NE JUMP _vg_restore;
    R4 = LSHIFT R0 BY -2;
    R8 = DM(_vg_r1);
    CALL _mi_velocity_select;
    I0 = _vg_r1;
    DM(0, I0) = R0; // scalar store: retain the saved S1 partner
_vg_restore:
    R0 = DM(_vg_i0);
    I0 = R0;
    R0 = DM(_vg_l0);
    L0 = R0;
    R0 = DM(_vg_mode);
    MODE1 = R0;
    R0 = DM(_vg_ax);
    ASTATX = R0;
    R0 = DM(_vg_ay);
    ASTATY = R0;
    R0 = DM(_vg_sx);
    STKYX = R0;
    R0 = DM(_vg_sy);
    STKYY = R0;
    R1 = DM(_vg_r1);
    R2 = DM(_vg_r2);
    R3 = DM(_vg_r3);
    R4 = DM(_vg_r4);
    R8 = DM(_vg_r8);
    R0 = DM(_vg_r0);
    R7 = 0x3f800000; // displaced stock instruction
    JUMP _mi_velocity_next;
.PKDATA _vg_r0, 8;
.PKDATA _vg_r1, 8;
.PKDATA _vg_r2, 8;
.PKDATA _vg_r3, 8;
.PKDATA _vg_r4, 8;
.PKDATA _vg_r8, 8;
.PKDATA _vg_i0, 8;
.PKDATA _vg_l0, 8;
.PKDATA _vg_mode, 8;
.PKDATA _vg_ax, 8;
.PKDATA _vg_ay, 8;
.PKDATA _vg_sx, 8;
.PKDATA _vg_sy, 8;
