// Optional early-frame callback, called under block.s's saved scalar context.
// Expire every publication explicitly, including at stamp wrap. Keep transition
// history: a held note must not wake again just because a new frame began.
.SECTION/PM seg_pmco;
.GLOBAL _mi_carrier_begin;
.GLOBAL _mi_carrier_rendered;
.EXTERN _mi_carrier_stamp;
.EXTERN _mi_carrier_pairs;
.EXTERN _mi_carrier_active;
.EXTERN _mi_carrier_publish;
.EXTERN _mi_velocity_owners;
_mi_carrier_begin:
    R0 = DM(_mi_carrier_stamp);
    R0 = R0 + 1;
    DM(_mi_carrier_stamp) = R0;
    I0 = _mi_carrier_pairs;
    R1 = 16;
    R0 = 0;
_mi_carrier_expire:
    DM(2, I0) = R0;
    MODIFY(I0, 3) (NW);
    R1 = R1 - 1;
    IF NE JUMP _mi_carrier_expire;
    I0 = _mi_velocity_owners;
    R1 = 16;
_mi_velocity_expire:
    DM(1, I0) = R0;
    MODIFY(I0, 2) (NW);
    R1 = R1 - 1;
    IF NE JUMP _mi_velocity_expire;
    RTS;

// Internal render completion: R4=track, R8=ACTIVE, R10=configured/ready.
// Retain ownership through retirement tails and their final release edge.
// Same scalar context as carrier_publish; clobbers R0..3,R10..12,R9,I0.
_mi_carrier_rendered:
    R0 = 0;
    COMP(R4, R0);
    IF LT RTS;
    R0 = 16;
    COMP(R4, R0);
    IF GE RTS;
    R0 = LSHIFT R4 BY 2;
    R1 = _mi_carrier_active;
    R1 = R1 + R0;
    I0 = R1;
    R1 = DM(0, I0);
    R10 = R10 OR R1;
    R11 = DM(_mi_carrier_stamp);
    R12 = 0;
    R9 = 0;
    JUMP _mi_carrier_publish;
