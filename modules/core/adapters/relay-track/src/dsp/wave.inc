// Rolling extrema of the main tape's PCM16 writes since the last control reply.
// A negative start means the current window contains no writes.
.SECTION/PM seg_pmco;
_rt_wave_reset:
    R0 = -1; DM(_rt_wave_start) = R0; RTS;

// Merge one motor block's extrema. R9/R11 are min/max across both channels.
// R0 is scratch; neither row pointer nor the track index is changed.
_rt_wave_merge:
    R0 = DM(_rt_wave_start); R0 = PASS R0; IF LT JUMP _rt_wave_first;
    R0 = DM(_rt_wave_min); R9 = MIN(R9,R0);
    R0 = DM(_rt_wave_max); R11 = MAX(R11,R0);
    JUMP _rt_wave_store;
_rt_wave_first:
    R0 = DM(_t_wave_start); DM(_rt_wave_start) = R0;
_rt_wave_store:
    DM(_rt_wave_min) = R9; DM(_rt_wave_max) = R11;
    RTS;

_rt_wave_pack:
    R4 = DM(_rt_wave_min); R5 = DM(_rt_wave_max);
// R4/R5 are signed PCM16 min/max. Return biased bytes in R3, low first.
_rt_wave_pair:
    R0 = R4; CALL _rt_wave_encode; R3 = R0;
    R0 = R5; CALL _rt_wave_encode; R0 = LSHIFT R0 BY 8; R3 = R3 OR R0;
    RTS;

// Round signed PCM16 away from zero, then bias it for unsigned comparison.
_rt_wave_encode:
    R1 = 255; R0 = PASS R0; IF GT R0 = R0 + R1;
    R0 = ASHIFT R0 BY -8; R1 = 127; R0 = MIN(R0,R1); R1 = 128; R0 = R0 + R1;
    RTS;

.SECTION/DM seg_dmda;
.VAR _rt_wave_start = -1;
.VAR _rt_wave_min;
.VAR _rt_wave_max;
