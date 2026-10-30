// V2 carrier transition policy. Not linked or advertised ready until the stock
// envelope call-site integration and final-output proof are complete.
// R4 track, R8 ACTIVE (includes tails/pending onsets), R10 ownership ready,
// R12 stock trigger, R9 stock release. Returns R0 trigger, R1 release.
// Unowned/invalid tracks pass stock flags through verbatim. Owned tracks emit
// only idle->active and active->quiet edges, never individual MIDI key edges.
// Call once per track per block; apply the returned pair to BOTH stereo voices.
// Caller: non-SIMD, L0=0, M5=0. Clobbers R0..3, I0, flags; other registers held.
.SECTION/PM seg_pmco;
.GLOBAL _mi_carrier_step;
.GLOBAL _mi_carrier_publish;
.GLOBAL _mi_carrier_flags;
// Publish once after a track render: same inputs as step, R11=block stamp.
// The future envelope hook reads this immutable pair for each stereo voice.
_mi_carrier_publish:
    CALL _mi_carrier_step;
    R2 = 16;
    COMP(R4, R2);
    IF GE RTS;
    R2 = 0;
    COMP(R4, R2);
    IF LT RTS;
    R2 = LSHIFT R4 BY 3;
    R3 = LSHIFT R4 BY 2;
    R2 = R2 + R3;
    R3 = _mi_carrier_pairs;
    R2 = R2 + R3;
    I0 = R2;
    R2 = 0;
    COMP(R10, R2);
    IF EQ JUMP _mi_carrier_unpublish;
    R2 = LSHIFT R1 BY 1;
    R2 = R2 OR R0;
    DM(0, I0) = R11;
    DM(1, I0) = R2;
    R2 = 1;
_mi_carrier_unpublish:
    DM(2, I0) = R2;
    RTS;

// R4=voice 0..31, R11=block stamp, R12/R9=stock trigger/release.
// R0/R1=flags, R2=1 iff a current owned pair was found. No state writes.
// Missing/stale/unowned records preserve THIS voice's stock flags verbatim.
_mi_carrier_flags:
    R0 = R12;
    R1 = R9;
    R2 = 32;
    COMP(R4, R2);
    IF GE JUMP _mi_carrier_miss;
    R2 = 0;
    COMP(R4, R2);
    IF LT JUMP _mi_carrier_miss;
    R2 = LSHIFT R4 BY -1;
    R3 = LSHIFT R2 BY 3;
    R2 = LSHIFT R2 BY 2;
    R2 = R2 + R3;
    R3 = _mi_carrier_pairs;
    R2 = R2 + R3;
    I0 = R2;
    R2 = DM(2, I0);
    R3 = 0;
    COMP(R2, R3);
    IF EQ JUMP _mi_carrier_miss;
    R2 = DM(0, I0);
    COMP(R2, R11);
    IF NE JUMP _mi_carrier_miss;
    R3 = DM(1, I0);
    R0 = FEXT R3 BY 0:1;
    R1 = FEXT R3 BY 1:1;
    R2 = 1;
    RTS;
_mi_carrier_miss:
    R2 = 0;
    RTS;

_mi_carrier_step:
    R0 = R12;
    R1 = R9;
    R2 = 16;
    COMP(R4, R2);
    IF GE RTS;
    R2 = 0;
    COMP(R4, R2);
    IF LT RTS;
    R2 = R4;
    R2 = LSHIFT R2 BY 2;
    R3 = _mi_carrier_active;
    R2 = R2 + R3;
    I0 = R2;
    R3 = 0;
    COMP(R10, R3);
    IF EQ JUMP _mi_carrier_unowned;
    R2 = DM(I0, M5);
    R0 = 0;
    R1 = 0;
    COMP(R8, R0);
    IF EQ JUMP _mi_carrier_quiet;
    R3 = 1;
    DM(I0, M5) = R3;
    COMP(R2, R0);
    IF EQ R0 = R3;
    RTS;
_mi_carrier_quiet:
    R3 = 0;
    DM(I0, M5) = R3;
    COMP(R2, R3);
    R3 = 1;
    IF NE R1 = R3;
    RTS;
_mi_carrier_unowned:
    // The stock owner resumes its own lifecycle. Forget our transition history
    // so a later V2 acquisition can wake even if this track was previously held.
    R2 = 0;
    DM(I0, M5) = R2;
    RTS;

.SECTION/DM seg_dmda;
.PKDATA _mi_carrier_active, 64;
.PKDATA _mi_carrier_pairs, 192;
