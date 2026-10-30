// Pre-render ownership, separate from the later envelope publication.
// No firmware hook is installed by the ordinary module manifest.
.SECTION/PM seg_pmco;
.GLOBAL _mi_velocity_prepare;
.GLOBAL _mi_velocity_select;
.EXTERN _mi_carrier_stamp;

// R4=track, R10=admitted or retiring. Scalar; clobbers R0..2,I0.
_mi_velocity_prepare:
    R0 = 0;
    COMP(R4, R0);
    IF LT RTS;
    R0 = 16;
    COMP(R4, R0);
    IF GE RTS;
    R0 = LSHIFT R4 BY 3;
    R1 = _mi_velocity_owners;
    R1 = R1 + R0;
    I0 = R1;
    R2 = DM(_mi_carrier_stamp);
    DM(0, I0) = R2;
    DM(1, I0) = R10;
    RTS;

// R4=stock voice 0..31, R8=stock velocity bits. R0=selected bits.
// Both stereo members use one immutable admission snapshot. Missing/stale or
// unowned snapshots pass stock through. Neutral is stock curve 0's 127/128.
// Scalar; clobbers R0..3,I0. Does not alter the stock cache or other gain inputs.
_mi_velocity_select:
    R0 = R8;
    R1 = 0;
    COMP(R4, R1);
    IF LT RTS;
    R1 = 32;
    COMP(R4, R1);
    IF GE RTS;
    R1 = LSHIFT R4 BY -1;
    R1 = LSHIFT R1 BY 3;
    R2 = _mi_velocity_owners;
    R1 = R1 + R2;
    I0 = R1;
    R1 = DM(0, I0);
    R2 = DM(_mi_carrier_stamp);
    COMP(R1, R2);
    IF NE RTS;
    R1 = DM(1, I0);
    R2 = 0;
    COMP(R1, R2);
    IF EQ RTS;
    R0 = 0x3f7e0000;
    RTS;
.PKDATA _mi_velocity_owners, 128;
