#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_grain;

_prism_grain:
    I0 = DM(PS_OUTPUT,I5);
    R0 = 0;
    DM(PS_ACTIVE,I5) = R0;
    DM(PS_LIMIT,I5) = R0;
    LCNTR = 64, DO _pg_clear_output UNTIL LCE;
_pg_clear_output: DM(I0,M6) = R0;
    R0 = DM(PS_HELD_FRAMES,I5);
    R0 = PASS R0;
    IF NE JUMP _pg_held;
    // No phrase: the grains read the tape. Their window is its newest part, two seconds at most, ending one block
    // behind the write position (PS_TEMP6, in frames; 0 when there is too little tape for a grain).
    R0 = 0;
    DM(PS_TEMP6,I5) = R0;
    R0 = DM(PS_ROLL_BANK,I5);
    R0 = PASS R0;
    IF EQ JUMP _pg_voices;
    R0 = DM(PT_VALID,I5);
    R0 = LSHIFT R0 BY 5;
    R1 = 64;
    R0 = R0 - R1;
    R1 = 96000;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    R1 = 256;
    COMP(R0,R1);
    IF LT JUMP _pg_voices;
    DM(PS_TEMP6,I5) = R0;
    CALL _pg_schedule;
    JUMP _pg_voices;
_pg_held:
    // Crop proportions are live, bounded to the committed take. Preserve a
    // safe minimum; never make an interpolated read outside the crop.
    F8 = FLOAT R0;
    R1 = PRISM_MIN_CROP;
    R2 = R0 - R1;
    F9 = FLOAT R2;
    F0 = DM(PC_CROP_START,I5);
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    F1 = F0 * F8;
    F0 = MIN(F1,F9);
    R6 = TRUNC F0;
    F0 = DM(PC_CROP_LEN,I5);
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    F1 = F0 * F8;
    R7 = TRUNC F1;
    R1 = PRISM_MIN_CROP;
    COMP(R7,R1);
    IF LT R7 = PASS R1;
    R0 = DM(PS_HELD_FRAMES,I5);
    R1 = R0 - R6;
    COMP(R7,R1);
    IF GT R7 = PASS R1;
    R0 = DM(PS_CROP_START,I5);
    COMP(R0,R6);
    IF NE JUMP _pg_crop_changed;
    R0 = DM(PS_CROP_LENGTH,I5);
    COMP(R0,R7);
    IF EQ JUMP _pg_crop_done;
_pg_crop_changed:
    // A fixed two-block output bridge uses only the last emitted samples;
    // old crop audio is never read after a crop has excluded it.
    R0 = 64;
    DM(PS_SMOOTH,I5) = R0;
_pg_crop_done:
    DM(PS_CROP_START,I5) = R6;
    DM(PS_CROP_LENGTH,I5) = R7;

    // The scan head is separate from saved POS. POS edits move the head;
    // ordinary scan movement does not rewrite the user's parameter.
    R0 = DM(PC_POS,I5);
    R1 = DM(PS_LAST_POS,I5);
    COMP(R0,R1);
    IF EQ JUMP _pg_scan_advance;
    DM(PS_LAST_POS,I5) = R0;
    R0 = 0;
    DM(PS_SCAN,I5) = R0;
_pg_scan_advance:
    F0 = DM(PC_SCAN,I5);
    R1 = 0xc0000000;
    F0 = MAX(F0,F1);
    R1 = 0x40000000;
    F0 = MIN(F0,F1);
    R1 = 0x42000000;
    F1 = F0 * F1;
    F0 = DM(PS_SCAN,I5);
    F2 = F0 + F1;
    F3 = FLOAT R7;
    // Normalize once in constant time, including a huge live crop reduction.
    // Reciprocal refinement leaves at most one endpoint correction below.
    F0 = RECIPS F3;
    F1 = F0 * F3;
    R4 = 0x40000000;
    F5 = F4 - F1;
    F1 = F0 * F5;
    F0 = F2 * F1;
    R0 = TRUNC F0;
    F1 = FLOAT R0;
    F0 = F1 * F3;
    F1 = F2 - F0;
    R2 = R1;
_pg_scan_wrap_low:
    R0 = 0;
    COMP(F2,F0);
    IF GE JUMP _pg_scan_wrap_high;
    F1 = F2 + F3;
    R2 = R1;
    JUMP _pg_scan_wrap_low;
_pg_scan_wrap_high:
    COMP(F2,F3);
    IF LT JUMP _pg_scan_store;
    F1 = F2 - F3;
    R2 = R1;
    JUMP _pg_scan_wrap_high;
_pg_scan_store:
    DM(PS_SCAN,I5) = R2;
    CALL _pg_schedule;

_pg_voices:
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R2 = R0 + R1;
    I4 = R2;
    R0 = PRISM_GRAINS;
    DM(PS_TEMP7,I5) = R0;
_pg_next:
    R0 = DM(PG_REMAIN,I4);
    R0 = PASS R0;
    IF LE JUMP _pg_advance;
    R1 = DM(PS_ACTIVE,I5);
    R1 = R1 + 1;
    DM(PS_ACTIVE,I5) = R1;
    CALL _pg_render;
_pg_advance:
    MODIFY(I4,PRISM_GRAIN_WORDS) (NW);
    R0 = DM(PS_TEMP7,I5);
    R0 = R0 - 1;
    DM(PS_TEMP7,I5) = R0;
    IF GT JUMP _pg_next;
    RTS;

_pg_schedule:
    R0 = DM(PC_RATE,I5);
    R0 = PASS R0;
    IF LE RTS;
    R1 = DM(PS_LAUNCH_VALID,I5);
    R1 = PASS R1;
    IF EQ JUMP _pg_due;
    R1 = DM(PC_BEAT,I5);
    R2 = DM(PS_LAUNCH_PHASE,I5);
    R3 = R1 - R2;
    R3 = PASS R3;
    IF LT RTS;
_pg_due:
    // Jitter is bounded to +/-half an interval, so every interval is positive.
    F8 = FLOAT R0;
    CALL _pg_random;
    F1 = DM(PC_JITTER,I5);
    R2 = 0;
    F1 = MAX(F1,F2);
    R2 = 0x3f800000;
    F1 = MIN(F1,F2);
    F2 = F0 * F1;
    R0 = 0x3f000000;
    F1 = F2 * F0;
    R0 = 0x3f800000;
    F2 = F1 + F0;
    F1 = F8 * F2;
    R0 = TRUNC F1;
    R1 = DM(PC_BEAT,I5);
    R2 = R1 + R0;
    DM(PS_LAUNCH_PHASE,I5) = R2;
    R0 = 1;
    DM(PS_LAUNCH_VALID,I5) = R0;
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R2 = R0 + R1;
    I4 = R2;
    R8 = PRISM_GRAINS;
_pg_find:
    R0 = DM(PG_REMAIN,I4);
    R0 = PASS R0;
    IF LE JUMP _pg_launch;
    MODIFY(I4,PRISM_GRAIN_WORDS) (NW);
    R8 = R8 - 1;
    IF GT JUMP _pg_find;
    R0 = 1;
    DM(PS_LIMIT,I5) = R0;
    R0 = DM(PS_HANDOFF,I5);
    R0 = PASS R0;
    IF EQ RTS;
    // A full pool releases one old reader this block. Retry on the next
    // block instead of waiting another musical interval to start the take.
    R0 = 0;
    DM(PS_LAUNCH_VALID,I5) = R0;
    RTS;
_pg_launch:
    R0 = DM(PC_SIZE,I5);
    R1 = 240;
    COMP(R0,R1);
    IF LT R0 = PASS R1;
    R1 = 24000;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    DM(PG_REMAIN,I4) = R0;
    DM(PG_DURATION,I4) = R0;
    F1 = FLOAT R0;
    F0 = RECIPS F1;
    // Refine the reciprocal once; envelope endpoints remain exact below.
    F2 = F0 * F1;
    R3 = 0x40000000;
    F1 = F3 - F2;
    F2 = F0 * F1;
    DM(PG_INV_DURATION,I4) = R2;
    R0 = 0;
    DM(PG_AGE,I4) = R0;
    DM(PG_OLD,I4) = R0;
    DM(PG_RETIRE,I4) = R0;
    R0 = DM(PS_HELD_BANK,I5);
    DM(PG_BANK,I4) = R0;
    R0 = DM(PS_HELD_START,I5);
    DM(PG_START,I4) = R0;
    // PG_FRAMES is the window of a grain on the tape. A grain of the held phrase follows the live crop: no window.
    R0 = 0;
    DM(PG_FRAMES,I4) = R0;
    R0 = DM(PC_SHAPE,I5);
    R1 = 3;
    R2 = R0 AND R1;
    DM(PG_SHAPE,I4) = R2;
    F8 = DM(PC_PITCH,I5);
    R0 = 0x3e800000;
    F8 = MAX(F8,F0);
    R0 = 0x40800000;
    F8 = MIN(F8,F0);
    CALL _pg_random;
    R1 = 0x3f800000;
    F2 = F0 + F1;
    R1 = 0x3f000000;
    F0 = F2 * F1;
    F1 = DM(PC_REVERSE,I5);
    R2 = 0x3f800000;
    COMP(F1,F2);
    IF LT JUMP _pg_reverse_chance;
    F8 = -F8;
    JUMP _pg_reverse_done;
_pg_reverse_chance:
    COMP(F0,F1);
    IF LT F8 = -F8;
_pg_reverse_done:
    DM(PG_RATE,I4) = R8;
    CALL _pg_random;
    F1 = DM(PC_SPRAY,I5);
    R2 = 0x3f000000;
    F3 = F1 * F2;
    F1 = F0 * F3;
    F0 = DM(PC_POS,I5);
    F2 = F0 + F1;
    R0 = DM(PS_HELD_FRAMES,I5);
    R0 = PASS R0;
    IF EQ JUMP _pg_place_tape;
    R0 = DM(PS_CROP_LENGTH,I5);
    F1 = FLOAT R0;
    F0 = F2 * F1;
    F2 = DM(PS_SCAN,I5);
    F3 = F0 + F2;
    DM(PG_PHASE,I4) = R3;
    DM(PG_ORIGIN,I4) = R3;
    JUMP _pg_placed;
_pg_place_tape:
    // On the tape POS runs from the newest audio (0) to the oldest of the window (1). The grain's whole read, its
    // length times its speed, lies inside the window, so it reads only tape that was written before it started.
    R0 = 0;
    F2 = MAX(F2,F0);
    R0 = 0x3f800000;
    F2 = MIN(F2,F0);
    R0 = DM(PG_DURATION,I4);
    F0 = FLOAT R0;
    F1 = DM(PG_RATE,I4);
    F3 = -F1;
    F1 = MAX(F1,F3);
    F0 = F0 * F1;
    R1 = 0x40800000;
    F0 = F0 + F1; // the read span
    R1 = DM(PS_TEMP6,I5);
    F1 = FLOAT R1; // the window
    F3 = F1 - F0;
    R4 = 0;
    COMP(F3,F4);
    IF GT JUMP _pg_tape_fits;
    // The window is shorter than this grain's read: no grain this time.
    R0 = 0;
    DM(PG_REMAIN,I4) = R0;
    RTS;
_pg_tape_fits:
    R4 = 0x3f800000;
    F4 = F4 - F2;
    F3 = F3 * F4;
    F4 = DM(PG_RATE,I4);
    R5 = 0;
    COMP(F4,F5);
    IF GE JUMP _pg_tape_forward;
    // A reverse grain starts at the far end of its read.
    F3 = F3 + F0;
    R5 = 0x3f800000;
    F4 = F1 - F5;
    F3 = MIN(F3,F4);
_pg_tape_forward:
    DM(PG_PHASE,I4) = R3;
    DM(PG_ORIGIN,I4) = R3;
    R0 = DM(PS_ROLL_BANK,I5);
    DM(PG_BANK,I4) = R0;
    R0 = DM(PS_TEMP6,I5);
    DM(PG_FRAMES,I4) = R0;
    R1 = DM(PS_WRITE_BLOCK,I5);
    R1 = LSHIFT R1 BY 5;
    R2 = 32;
    R1 = R1 - R2;
    R1 = R1 - R0;
    R1 = PASS R1;
    IF GE JUMP _pg_tape_start;
    R2 = DM(PS_CAPACITY,I5);
    R1 = R1 + R2;
_pg_tape_start:
    DM(PG_START,I4) = R1;
_pg_placed:
    CALL _pg_random;
    F1 = DM(PC_WIDTH,I5);
    F2 = F0 * F1;
    R0 = 0;
    F1 = MAX(F2,F0);
    F3 = MIN(F2,F0);
    R0 = 0x3f800000;
    F2 = F0 - F1;
    F1 = F0 + F3;
    DM(PG_PAN_LEFT,I4) = R2;
    DM(PG_PAN_RIGHT,I4) = R1;
    // Expected overlap from the musical launch interval, compensated at
    // launch instead of pumping the gain whenever an individual grain ends.
    // overlap = size * (BPM*120) / (rateQ16 * 5273.4375).
    R0 = DM(PC_SIZE,I5);
    F0 = FLOAT R0;
    R1 = DM(PS_CONTROL + PH_C_TEMPO_120 / 4,I5);
    F1 = FLOAT R1;
    F2 = F0 * F1;
    R0 = DM(PC_RATE,I5);
    F0 = FLOAT R0;
    F1 = RECIPS F0;
    F3 = F2 * F1;
    R0 = 0x3946d751; // 1/5273.4375
    F2 = F3 * F0;
    R0 = 0x3f800000;
    F1 = MAX(F2,F0);
    F2 = RSQRTS F1;
    R0 = 0x3f19999a; // 0.6 wet headroom
    F1 = F2 * F0;
    DM(PG_GAIN,I4) = R1;
    RTS;

_pg_random:
    R0 = DM(PS_RNG,I5);
    R1 = LSHIFT R0 BY 13;
    R2 = R0 XOR R1;
    R1 = LSHIFT R2 BY -17;
    R0 = R2 XOR R1;
    R1 = LSHIFT R0 BY 5;
    R2 = R0 XOR R1;
    DM(PS_RNG,I5) = R2;
    R1 = -31;
    F0 = FLOAT R2 BY R1;
    RTS;

_pg_render:
    // A grain on the tape reads its own window; a grain of a phrase the live crop of that phrase.
    R1 = DM(PG_FRAMES,I4);
    R1 = PASS R1;
    IF EQ JUMP _pg_phrase_crop;
    R0 = 0;
    JUMP _pg_crop;
_pg_phrase_crop:
    R0 = DM(PG_OLD,I4);
    R0 = PASS R0;
    IF NE JUMP _pg_old_crop;
    R0 = DM(PS_CROP_START,I5);
    R1 = DM(PS_CROP_LENGTH,I5);
    JUMP _pg_crop;
_pg_old_crop:
    R0 = DM(PS_OLD_CROP_START,I5);
    R1 = DM(PS_OLD_CROP_LENGTH,I5);
_pg_crop:
    DM(PS_TEMP,I5) = R0;
    DM(PS_TEMP1,I5) = R1;
    // PS_TEMP6: one over the edge of the window in samples. 32 for the two shapes that are smooth by themselves; RISE
    // and FALL have one steep side, which is an eighth of the grain, 32 to 256 samples (about 5 ms), not a click.
    R0 = 0x3d000000;
    R1 = DM(PG_SHAPE,I4);
    R2 = 2;
    COMP(R1,R2);
    IF LT JUMP _pg_edge_set;
    R1 = DM(PG_DURATION,I4);
    R1 = LSHIFT R1 BY -3;
    R2 = 32;
    COMP(R1,R2);
    IF LT R1 = PASS R2;
    R2 = 256;
    COMP(R1,R2);
    IF GT R1 = PASS R2;
    F1 = FLOAT R1;
    F0 = RECIPS F1;
    F2 = F0 * F1;
    R3 = 0x40000000;
    F2 = F3 - F2;
    F0 = F0 * F2;
_pg_edge_set:
    DM(PS_TEMP6,I5) = R0;
    F10 = DM(PG_PHASE,I4);
    F11 = DM(PG_RATE,I4);
    F12 = DM(PG_INV_DURATION,I4);
    R9 = DM(PG_AGE,I4);
    R0 = DM(PG_REMAIN,I4);
    R1 = 32;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    DM(PS_TEMP2,I5) = R0;
    DM(PS_TEMP3,I5) = R0;
    R0 = DM(PG_RETIRE,I4);
    R0 = R0 - 1;
    R0 = PASS R0;
    IF LE JUMP _pg_retire_recip_zero;
    F0 = FLOAT R0;
    F1 = RECIPS F0;
    JUMP _pg_retire_recip;
_pg_retire_recip_zero:
    R1 = 0;
_pg_retire_recip:
    DM(PS_TEMP5,I5) = R1;
    R0 = DM(PS_TEMP1,I5);
    F8 = FLOAT R0;
    F0 = RECIPS F8;
    F1 = F0 * F8;
    R2 = 0x40000000;
    F3 = F2 - F1;
    F1 = F0 * F3;
    F0 = F10 * F1;
    R0 = TRUNC F0;
    F1 = FLOAT R0;
    F0 = F1 * F8;
    F1 = F10 - F0;
    R10 = R1;
    I0 = DM(PS_OUTPUT,I5);
    R1 = I0;
    R2 = 128;
    R3 = R1 + R2;
    I1 = R3;
_pg_sample:
    R0 = DM(PS_TEMP1,I5);
    F8 = FLOAT R0;
_pg_phase_low:
    R0 = 0;
    COMP(F10,F0);
    IF GE JUMP _pg_phase_high;
    F1 = F10 + F8;
    R10 = R1;
    JUMP _pg_phase_low;
_pg_phase_high:
    COMP(F10,F8);
    IF LT JUMP _pg_phase_ready;
    F1 = F10 - F8;
    R10 = R1;
    JUMP _pg_phase_high;
_pg_phase_ready:
    R6 = TRUNC F10;
    F0 = FLOAT R6;
    F7 = F10 - F0;
    DM(PS_TEMP4,I5) = R7;
    R7 = R6 + 1;
    R0 = DM(PS_TEMP1,I5);
    COMP(R7,R0);
    IF LT JUMP _pg_pair_crop;
    R7 = 0;
_pg_pair_crop:
    R0 = DM(PS_TEMP,I5);
    R1 = R6 + R0;
    R2 = R7 + R0;
    R0 = DM(PG_START,I4);
    R6 = R1 + R0;
    R7 = R2 + R0;
    R0 = DM(PS_CAPACITY,I5);
    COMP(R6,R0);
    IF LT JUMP _pg_pair_first;
    R1 = R6 - R0;
    R6 = R1;
_pg_pair_first:
    COMP(R7,R0);
    IF LT JUMP _pg_pair_second;
    R1 = R7 - R0;
    R7 = R1;
_pg_pair_second:
    R1 = LSHIFT R6 BY 2;
    R2 = LSHIFT R7 BY 2;
    R0 = DM(PG_BANK,I4);
    R3 = R0 + R1;
    I2 = R3;
    R3 = R0 + R2;
    I3 = R3;
    F7 = DM(PS_TEMP4,I5);
    F0 = DM(0,I2);
    F1 = DM(0,I3);
    F2 = F1 - F0;
    F1 = F2 * F7;
    F5 = F0 + F1;
    R0 = DM(PS_CAPACITY,I5);
    R1 = LSHIFT R0 BY 2;
    R0 = I2;
    R2 = R0 + R1;
    I2 = R2;
    R0 = I3;
    R2 = R0 + R1;
    I3 = R2;
    F0 = DM(0,I2);
    F1 = DM(0,I3);
    F2 = F1 - F0;
    F1 = F2 * F7;
    F6 = F0 + F1;
    // Four bounded windows. The first/last sample are exactly zero, and every
    // shape includes an edge of at least 32 samples (PS_TEMP6), also the directional RISE/FALL.
    F0 = FLOAT R9;
    F1 = F0 * F12;
    R0 = 0x3f800000;
    F2 = F0 - F1;
    R0 = DM(PG_SHAPE,I4);
    R3 = 1;
    COMP(R0,R3);
    IF EQ JUMP _pg_tri;
    R3 = 2;
    COMP(R0,R3);
    IF EQ JUMP _pg_rise;
    R3 = 3;
    COMP(R0,R3);
    IF EQ JUMP _pg_fall;
    F3 = F1 * F2;
    F0 = F3 * F3;
    R3 = 0x41800000;
    F4 = F0 * F3;
    JUMP _pg_window_edge;
_pg_tri:
    F0 = MIN(F1,F2);
    F4 = F0 + F0;
    JUMP _pg_window_edge;
_pg_rise:
    R4 = R1;
    JUMP _pg_window_edge;
_pg_fall:
    R4 = R2;
_pg_window_edge:
    R0 = DM(PG_DURATION,I4);
    R1 = R0 - R9;
    R1 = R1 - 1;
    F0 = FLOAT R9;
    F1 = FLOAT R1;
    F2 = MIN(F0,F1);
    F0 = DM(PS_TEMP6,I5);
    F1 = F2 * F0;
    R0 = 0x3f800000;
    F2 = MIN(F1,F0);
    F0 = F4 * F2;
    F1 = DM(PG_GAIN,I4);
    F4 = F0 * F1;
    R0 = DM(PG_RETIRE,I4);
    R0 = PASS R0;
    IF EQ JUMP _pg_reader_fade_done;
    R0 = DM(PS_TEMP2,I5);
    R0 = R0 - 1;
    F0 = FLOAT R0;
    F1 = DM(PS_TEMP5,I5);
    F2 = F0 * F1;
    F0 = F4 * F2;
    R4 = R0;
_pg_reader_fade_done:
    // Complementary old/new wet handoff uses the same published progress as
    // the router. Per-sample interpolation avoids a block-rate gain staircase.
    R0 = DM(PS_HANDOFF,I5);
    R0 = PASS R0;
    IF EQ JUMP _pg_handoff_done;
    F0 = FLOAT R0;
    R1 = 0x3d088889;
    F8 = F0 * F1;
    R0 = DM(PS_TEMP3,I5);
    R1 = DM(PS_TEMP2,I5);
    R2 = R0 - R1;
    F0 = FLOAT R2;
    R1 = 0x3a888889; // 1/960
    F3 = F0 * F1;
    F1 = F8 - F3;
    R0 = DM(PG_OLD,I4);
    R0 = PASS R0;
    IF NE JUMP _pg_handoff_old;
    R0 = 0x3f800000;
    F2 = F0 - F1;
    R1 = R2;
_pg_handoff_old:
    F0 = F4 * F1;
    R4 = R0;
_pg_handoff_done:
    F0 = DM(PG_PAN_LEFT,I4);
    F1 = F4 * F0;
    F2 = F5 * F1;
    F0 = DM(I0,M5);
    F1 = F0 + F2;
    DM(I0,M6) = R1;
    F0 = DM(PG_PAN_RIGHT,I4);
    F1 = F4 * F0;
    F2 = F6 * F1;
    F0 = DM(I1,M5);
    F1 = F0 + F2;
    DM(I1,M6) = R1;
    F0 = F10 + F11;
    R10 = R0;
    R9 = R9 + 1;
    R0 = DM(PS_TEMP2,I5);
    R0 = R0 - 1;
    DM(PS_TEMP2,I5) = R0;
    IF GT JUMP _pg_sample;
    DM(PG_PHASE,I4) = R10;
    DM(PG_AGE,I4) = R9;
    R0 = DM(PG_REMAIN,I4);
    R1 = DM(PS_TEMP3,I5);
    R2 = R0 - R1;
    DM(PG_REMAIN,I4) = R2;
    RTS;
