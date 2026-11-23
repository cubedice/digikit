// Rolling extrema of the main tape's PCM16 writes since the last control reply.
// A negative start means the current window contains no writes.
.SECTION/PM seg_pmco;
_rt_wave_reset:
    R0 = -1; DM(_rt_wave_start) = R0; RTS;

// Merge one motor block's extrema. R9/R11 are L min/max, R13/R14 R min/max.
// R0 is scratch; neither row pointer nor the track index is changed.
_rt_wave_merge:
    R0 = DM(_rt_wave_start); R0 = PASS R0; IF LT JUMP _rt_wave_first;
    R0 = DM(_rt_wave_min_l); R9 = MIN(R9,R0);
    R0 = DM(_rt_wave_max_l); R11 = MAX(R11,R0);
    R0 = DM(_rt_wave_min_r); R13 = MIN(R13,R0);
    R0 = DM(_rt_wave_max_r); R14 = MAX(R14,R0);
    JUMP _rt_wave_store;
_rt_wave_first:
    R0 = DM(_t_wave_start); DM(_rt_wave_start) = R0;
_rt_wave_store:
    DM(_rt_wave_min_l) = R9; DM(_rt_wave_max_l) = R11;
    DM(_rt_wave_min_r) = R13; DM(_rt_wave_max_r) = R14;
    RTS;

// Round each signed PCM16 value away from zero to one of 16 bins.
_rt_wave_nibble:
    R1 = ABS R0; R2 = 4095; R1 = R1 + R2; R1 = LSHIFT R1 BY -12;
    R0 = PASS R0; IF LT R1 = -R1;
    R0 = 7; R1 = MIN(R1,R0); R2 = 8; R0 = R1 + R2; RTS;

// R0 returned. Bits 0..15 contain L min/max then R min/max, low nibble first.
_rt_wave_pack:
    R0 = DM(_rt_wave_min_l); CALL _rt_wave_nibble; R3 = R0;
    R0 = DM(_rt_wave_max_l); CALL _rt_wave_nibble; R0 = LSHIFT R0 BY 4; R3 = R3 OR R0;
    R0 = DM(_rt_wave_min_r); CALL _rt_wave_nibble; R0 = LSHIFT R0 BY 8; R3 = R3 OR R0;
    R0 = DM(_rt_wave_max_r); CALL _rt_wave_nibble; R0 = LSHIFT R0 BY 12; R0 = R3 OR R0;
    RTS;

.SECTION/DM seg_dmda;
.VAR _rt_wave_start = -1;
.VAR _rt_wave_min_l;
.VAR _rt_wave_max_l;
.VAR _rt_wave_min_r;
.VAR _rt_wave_max_r;
