// PRISM read heads: two a side. Head A is the current one; when the read position has to jump, head B takes over what A
// was reading and the two crossfade with equal power (the table _prism_fade), so a jump neither clicks nor bends the pitch.
//
// A head is a delay in frames from the reference frame (integer and fraction) and a direction on the tape. The reference
// is the write position of the sample (running tape) or the frame where the tape stands (HOLD). Per sample the delay
// moves by the reference's advance (1 or 0) minus the direction (1 or -1):
//
//   forward, running   0   the delay stays; a free RATE or SKEW move glides it (the pitch bends, like tape)
//   reverse, running  +2   a segment of TIME samples read backwards from 32 frames behind the write position
//   forward, standing -1   the held loop
//   reverse, standing +1   the held loop backwards
//
// A reverse segment and a held loop end in a seam: head B plays on past it while head A starts the next turn, and the two
// crossfade over at most half the turn. A held loop's seam fades into the tape before the loop's start, so a turn is exactly
// the loop's length and the tape is only read.
//
// _prism_program decides, once a block and only while no crossfade runs, whether head A may go on (a glide, the same loop)
// or has to jump (a zone change, a SYNC step, FLIP, HOLD, a loop that no longer holds the head, tape that is not written).
#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_program;
.GLOBAL _prism_heads;
.EXTERN _pr_recip;

// I3 one side, PS_ARG_T its time in frames. Sets the side's seam and, if head A cannot go on, starts a crossfade to a new
// head A. Uses R0 to R15.
_prism_program:
    R0 = DM(CH_FADE_LEFT,I3);
    R0 = PASS R0;
    IF NE JUMP _pp_finish;
    R15 = DM(PS_MODE,I5);
    R4 = DM(PS_ARG_T,I5);
    R5 = DM(PS_XFADE,I5);
    R14 = DM(PS_LIMIT,I5);
    R0 = PM_HOLD;
    COMP(R15,R0);
    IF GE JUMP _pp_hold;
    R0 = PM_REVERSE;
    COMP(R15,R0);
    IF EQ JUMP _pp_reverse;

    // ---- forward on the running tape. The deepest read is the larger of the head's delay and its target.
    R0 = R4;
    R1 = DM(CH_A_KIND,I3);
    R1 = PASS R1;
    IF EQ JUMP _pp_forward_need;
    R1 = DM(CH_A_DINT,I3);
    COMP(R1,R0);
    IF GT R0 = PASS R1;
_pp_forward_need:
    R1 = 70;
    R0 = R0 + R1;
    COMP(R0,R14);
    IF GT JUMP _pp_mute;
    R0 = 0xc0000000;
    DM(CH_SEAM,I3) = R0;
    DM(CH_HARD,I3) = R0;
    DM(CH_TARGET,I3) = R4;
    // A forward head of this program glides to the new time unless the key changed.
    R0 = DM(CH_A_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _pp_forward_jump;
    R0 = DM(CH_A_DIR,I3);
    R0 = PASS R0;
    IF LT JUMP _pp_forward_jump;
    R0 = DM(CH_MODE,I3);
    R0 = PASS R0;
    IF NE JUMP _pp_forward_jump;
    R0 = DM(CH_KEYGEN,I3);
    R1 = DM(PS_KEYGEN,I5);
    COMP(R0,R1);
    IF EQ JUMP _pp_set;
_pp_forward_jump:
    R0 = 1;
    R1 = R4;
    R2 = 1;
    R3 = R5;
    CALL _pp_jump;
    JUMP _pp_set;

    // ---- reverse on the running tape: head A starts 32 frames behind the write position and its delay grows by 2 a
    // sample; after TIME samples the next segment starts. Head B runs on for the crossfade.
_pp_reverse:
    R6 = LSHIFT R4 BY -1;
    COMP(R6,R5);
    IF GT R6 = PASS R5;
    R0 = R4 + R5;
    R0 = LSHIFT R0 BY 1;
    R1 = 70;
    R0 = R0 + R1;
    COMP(R0,R14);
    IF GT JUMP _pp_mute;
    R0 = LSHIFT R4 BY 1;
    R1 = 32;
    R2 = R0 + R1;
    DM(CH_SEAM,I3) = R2;
    R1 = LSHIFT R5 BY 1;
    R2 = R2 + R1;
    DM(CH_HARD,I3) = R2;
    R0 = -R0;
    DM(CH_JUMP,I3) = R0;
    CALL _pp_seam_fade;
    R0 = DM(CH_A_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _pp_reverse_jump;
    R0 = DM(CH_A_DIR,I3);
    R0 = PASS R0;
    IF GT JUMP _pp_reverse_jump;
    R0 = DM(CH_MODE,I3);
    R1 = PM_REVERSE;
    COMP(R0,R1);
    IF NE JUMP _pp_reverse_jump;
    R0 = DM(CH_KEYGEN,I3);
    R1 = DM(PS_KEYGEN,I5);
    COMP(R0,R1);
    IF EQ JUMP _pp_set;
_pp_reverse_jump:
    R0 = 1;
    R1 = 32;
    R2 = -1;
    R3 = R6;
    CALL _pp_jump;
    JUMP _pp_set;

    // ---- the standing tape. The loop's natural end lies PRISM_WFADE frames before the reference; its start is where
    // head A read when HOLD was taken (CH_HOLD_SPAN), or further back if the time asks for more. A shorter time keeps the
    // start and moves the end. The loop, the stretch before it (the seam's crossfade) and a head running out lie on
    // written tape.
_pp_hold:
    R0 = LSHIFT R5 BY 1;
    R1 = PRISM_WFADE + 70;
    R0 = R0 + R1;
    R0 = R14 - R0;
    COMP(R4,R0);
    IF GT R4 = PASS R0;
    R1 = PRISM_TIME_MIN;
    COMP(R4,R1);
    IF LT JUMP _pp_mute;
    R6 = LSHIFT R4 BY -1;
    COMP(R6,R5);
    IF GT R6 = PASS R5;
    R7 = DM(CH_HOLD_SPAN,I3);
    COMP(R7,R0);
    IF GT R7 = PASS R0;
    COMP(R7,R4);
    IF LT R7 = PASS R4;
    R1 = PRISM_WFADE;
    R2 = R7 + R1;
    R3 = R2 - R4;
    DM(PS_TEMP,I5) = R2;
    DM(PS_TEMP1,I5) = R3;
    CALL _pp_seam_fade;
    R2 = DM(PS_TEMP,I5);
    R3 = DM(PS_TEMP1,I5);
    R0 = PM_HOLD_REVERSE;
    COMP(R15,R0);
    IF EQ JUMP _pp_hold_reverse;
    // Forward: the seam starts its crossfade before the end, and head A goes on one loop back.
    R0 = R3 + R6;
    DM(CH_SEAM,I3) = R0;
    DM(CH_HARD,I3) = R3;
    DM(CH_JUMP,I3) = R4;
    R1 = DM(CH_A_KIND,I3);
    R1 = PASS R1;
    IF EQ JUMP _pp_hold_jump;
    R1 = DM(CH_A_DIR,I3);
    R1 = PASS R1;
    IF LT JUMP _pp_hold_jump;
    R1 = DM(CH_A_DINT,I3);
    COMP(R1,R0);
    IF LE JUMP _pp_hold_jump;
    R0 = R2 + R6;
    COMP(R1,R0);
    IF LE JUMP _pp_set;
_pp_hold_jump:
    R0 = 1;
    R1 = R2;
    R2 = 1;
    R3 = R6;
    CALL _pp_jump;
    JUMP _pp_set;
_pp_hold_reverse:
    // Reverse: head A runs from the end to the start; head B goes on into the stretch before the start.
    R0 = R2 + 1;
    DM(CH_SEAM,I3) = R0;
    R0 = R0 + R6;
    DM(CH_HARD,I3) = R0;
    R0 = -R4;
    DM(CH_JUMP,I3) = R0;
    R1 = DM(CH_A_KIND,I3);
    R1 = PASS R1;
    IF EQ JUMP _pp_hold_reverse_jump;
    R1 = DM(CH_A_DIR,I3);
    R1 = PASS R1;
    IF GT JUMP _pp_hold_reverse_jump;
    R1 = DM(CH_A_DINT,I3);
    COMP(R1,R3);
    IF LE JUMP _pp_hold_reverse_jump;
    COMP(R1,R2);
    IF LE JUMP _pp_set;
_pp_hold_reverse_jump:
    R0 = 1;
    R1 = R3 + 1;
    R2 = -1;
    R3 = R6;
    CALL _pp_jump;
    JUMP _pp_set;

    // ---- no written tape for this program yet: head A falls silent, and comes back with a crossfade.
_pp_mute:
    R0 = 0;
    R1 = 0;
    R2 = 1;
    R3 = R5;
    CALL _pp_jump;
_pp_set:
    R0 = DM(PS_MODE,I5);
    DM(CH_MODE,I3) = R0;
    R0 = DM(PS_KEYGEN,I5);
    DM(CH_KEYGEN,I3) = R0;
_pp_finish:
    // Head A glides this block if it is a forward head on the running tape that is not at rest on its target.
    R2 = 0;
    R0 = DM(PS_MODE,I5);
    R0 = PASS R0;
    IF NE JUMP _pp_glide;
    R0 = DM(CH_MODE,I3);
    R0 = PASS R0;
    IF NE JUMP _pp_glide;
    R0 = DM(CH_A_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _pp_glide;
    R0 = DM(CH_A_DIR,I3);
    R0 = PASS R0;
    IF LT JUMP _pp_glide;
    R2 = 1;
    R0 = DM(CH_A_DINT,I3);
    R1 = DM(CH_TARGET,I3);
    COMP(R0,R1);
    IF NE JUMP _pp_glide;
    R0 = DM(CH_A_FRAC,I3);
    R0 = PASS R0;
    IF NE JUMP _pp_glide;
    R0 = DM(CH_VEL,I3);
    R0 = PASS R0;
    IF NE JUMP _pp_glide;
    R0 = DM(CH_DRIFT,I3);
    R0 = PASS R0;
    IF NE JUMP _pp_glide;
    R2 = 0;
_pp_glide:
    DM(CH_GLIDE,I3) = R2;
    RTS;

// I3 one side, R6 the seam's crossfade in samples: kept with 128 / R6. Uses F0, F1, F12, F13.
_pp_seam_fade:
    DM(CH_SEAM_FADE,I3) = R6;
    F0 = FLOAT R6;
    CALL _pr_recip;
    R1 = 0x43000000;
    F0 = F0 * F1;
    DM(CH_SEAM_INV,I3) = R0;
    RTS;

// I3 one side. Head A becomes R0 (1 playing, 0 silent) at delay R1 with direction R2, and head B goes on with what A was
// for a crossfade of at most R3 samples: no longer than B can go on reading written tape. Nothing happens when head A is
// that head already. Uses R0 to R3, R7 to R13.
_pp_jump:
    R7 = DM(CH_A_KIND,I3);
    COMP(R0,R7);
    IF NE JUMP _pj_go;
    R0 = PASS R0;
    IF EQ RTS;
    R8 = DM(CH_A_DINT,I3);
    COMP(R1,R8);
    IF NE JUMP _pj_go;
    R8 = DM(CH_A_DIR,I3);
    COMP(R2,R8);
    IF NE JUMP _pj_go;
    R8 = DM(CH_A_FRAC,I3);
    R8 = PASS R8;
    IF EQ RTS;
_pj_go:
    DM(CH_B_KIND,I3) = R7;
    R7 = PASS R7;
    IF EQ JUMP _pj_new;
    R8 = DM(CH_A_DINT,I3);
    DM(CH_B_DINT,I3) = R8;
    R9 = DM(CH_A_FRAC,I3);
    DM(CH_B_FRAC,I3) = R9;
    R9 = DM(CH_A_DIR,I3);
    DM(CH_B_DIR,I3) = R9;
    // The old head's delay moves by the reference's advance minus its direction: not at all, toward the reference (it
    // may run to the loop's natural end), or away from it (to the deepest written frame).
    R10 = DM(PS_WADV,I5);
    R10 = R10 - R9;
    IF EQ JUMP _pj_new;
    IF LT JUMP _pj_toward;
    R11 = DM(PS_LIMIT,I5);
    R11 = R11 - R8;
    R12 = 2;
    R11 = R11 - R12;
    R13 = 0;
    COMP(R11,R13);
    IF LT R11 = PASS R13;
    COMP(R10,R12);
    IF LT JUMP _pj_room;
    R11 = LSHIFT R11 BY -1;
    JUMP _pj_room;
_pj_toward:
    R11 = PRISM_WFADE + 1;
    R11 = R8 - R11;
_pj_room:
    COMP(R3,R11);
    IF GT R3 = PASS R11;
    R3 = PASS R3;
    IF GT JUMP _pj_new;
    // No room at all: the old head ends here.
    R3 = 0;
    DM(CH_B_KIND,I3) = R3;
_pj_new:
    DM(CH_A_KIND,I3) = R0;
    DM(CH_A_DINT,I3) = R1;
    DM(CH_A_DIR,I3) = R2;
    R0 = 0;
    DM(CH_A_FRAC,I3) = R0;
    DM(CH_VEL,I3) = R0;
    DM(CH_FADE_LEFT,I3) = R3;
    DM(CH_FADE_LEN,I3) = R3;
    R3 = PASS R3;
    IF EQ RTS;
    F0 = FLOAT R3;
    CALL _pr_recip;
    R1 = 0x43000000;
    F0 = F0 * F1;
    DM(CH_FADE_INV,I3) = R0;
    RTS;

// Both sides' heads: 32 samples each to PS_Y, left then right.
_prism_heads:
    R13 = DM(PS_CAPACITY,I5);
    R12 = DM(PS_WADV,I5);
    R0 = I5;
    R1 = PS_CH_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    R0 = I5;
    R1 = PS_Y * 4;
    R0 = R0 + R1;
    I1 = R0;
    R11 = DM(PS_BANK,I5);
    CALL _ph_channel;
    R0 = I5;
    R1 = PS_CH_R * 4;
    R0 = R0 + R1;
    I3 = R0;
    R11 = DM(PS_BANK,I5);
    R0 = LSHIFT R13 BY 2;
    R11 = R11 + R0;
    CALL _ph_channel;
    RTS;

// One side. I3 its record, I1 the output (advanced by 32), R11 its plane, R12 the reference's advance, R13 the tape's
// frames. In the loop R8 and F9 are head A's delay, R10 the reference frame of the sample, R14 the samples left, R15
// the fade table, R6 the samples left of the crossfade. Uses R0 to R10, R14, R15, I2.
_ph_channel:
    R10 = DM(PS_REF,I5);
    R8 = DM(CH_A_DINT,I3);
    R9 = DM(CH_A_FRAC,I3);
    R14 = 32;
    R15 = _prism_fade;
_ph_sample:
    // ---- the glide of a forward head: the velocity follows (target - delay) / 1024, at most PRISM_GLIDE_MAX, over
    // 256 samples, so the pitch bends in and out instead of jumping.
    R0 = DM(CH_GLIDE,I3);
    R0 = PASS R0;
    IF EQ JUMP _ph_read;
    R0 = DM(CH_TARGET,I3);
    R0 = R0 - R8;
    F0 = FLOAT R0;
    F0 = F0 - F9;
    F1 = DM(CH_DRIFT,I3);
    F0 = F0 + F1;
    R1 = PRISM_GLIDE_RATE;
    F2 = F0 * F1;
    R1 = PRISM_GLIDE_MAX;
    F2 = MIN(F2,F1);
    F1 = -F1;
    F2 = MAX(F2,F1);
    F3 = DM(CH_VEL,I3);
    F2 = F2 - F3;
    R1 = PRISM_GLIDE_EASE;
    F2 = F2 * F1;
    F3 = F3 + F2;
    // At rest on a whole frame the head stays there: a read between frames would take highs off every repeat.
    F0 = ABS F0;
    R1 = 0x3a800000; // 1/1024 of a frame
    COMP(F0,F1);
    IF GE JUMP _ph_move;
    F0 = ABS F3;
    R1 = 0x3727c5ac; // 1e-5 frames a sample
    COMP(F0,F1);
    IF GE JUMP _ph_move;
    R0 = DM(CH_DRIFT,I3);
    R0 = PASS R0;
    IF NE JUMP _ph_move;
    R8 = DM(CH_TARGET,I3);
    R9 = 0;
    DM(CH_VEL,I3) = R9;
    JUMP _ph_read;
_ph_move:
    DM(CH_VEL,I3) = R3;
    F9 = F9 + F3;
    R1 = 0x3f800000;
    COMP(F9,F1);
    IF LT JUMP _ph_no_carry;
    F9 = F9 - F1;
    R8 = R8 + 1;
_ph_no_carry:
    R2 = 0;
    COMP(F9,F2);
    IF GE JUMP _ph_no_borrow;
    F9 = F9 + F1;
    R8 = R8 - 1;
_ph_no_borrow:
    R0 = PRISM_TIME_MIN;
    COMP(R8,R0);
    IF GE JUMP _ph_read;
    R8 = R0;
    R9 = 0;
    DM(CH_VEL,I3) = R9;
_ph_read:
    // ---- head A: the frame at its delay, and between frames the earlier one by the fraction
    R4 = 0;
    R0 = DM(CH_A_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _ph_a_step;
    R0 = R10 - R8;
    IF LT R0 = R0 + R13;
    COMP(R0,R13);
    IF GE R0 = R0 - R13;
    R1 = LSHIFT R0 BY 2;
    R1 = R1 + R11;
    I2 = R1;
    F4 = DM(0,I2);
    R9 = PASS R9;
    IF EQ JUMP _ph_a_step;
    R0 = R0 - 1;
    IF LT R0 = R0 + R13;
    R1 = LSHIFT R0 BY 2;
    R1 = R1 + R11;
    I2 = R1;
    F5 = DM(0,I2);
    F5 = F5 - F4;
    F5 = F5 * F9;
    F4 = F4 + F5;
_ph_a_step:
    R7 = DM(CH_A_DIR,I3);
    R0 = R12 - R7;
    R8 = R8 + R0;
    // ---- the crossfade with head B
    R6 = DM(CH_FADE_LEFT,I3);
    R6 = PASS R6;
    IF EQ JUMP _ph_out;
    R5 = 0;
    R0 = DM(CH_B_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _ph_gains;
    R2 = DM(CH_B_DINT,I3);
    R0 = R10 - R2;
    IF LT R0 = R0 + R13;
    COMP(R0,R13);
    IF GE R0 = R0 - R13;
    R1 = LSHIFT R0 BY 2;
    R1 = R1 + R11;
    I2 = R1;
    F5 = DM(0,I2);
    R3 = DM(CH_B_FRAC,I3);
    R3 = PASS R3;
    IF EQ JUMP _ph_b_step;
    R0 = R0 - 1;
    IF LT R0 = R0 + R13;
    R1 = LSHIFT R0 BY 2;
    R1 = R1 + R11;
    I2 = R1;
    F1 = DM(0,I2);
    F1 = F1 - F5;
    F1 = F1 * F3;
    F5 = F5 + F1;
_ph_b_step:
    R0 = DM(CH_B_DIR,I3);
    R0 = R12 - R0;
    R2 = R2 + R0;
    DM(CH_B_DINT,I3) = R2;
_ph_gains:
    // The fade's position 0 .. 128 in the table: head A takes the entry, head B the mirrored one.
    R0 = DM(CH_FADE_LEN,I3);
    R0 = R0 - R6;
    F0 = FLOAT R0;
    F1 = DM(CH_FADE_INV,I3);
    F0 = F0 * F1;
    R1 = TRUNC F0;
    F2 = FLOAT R1;
    F0 = F0 - F2;
    R2 = LSHIFT R1 BY 2;
    R2 = R2 + R15;
    I2 = R2;
    F2 = DM(0,I2);
    F3 = DM(1,I2);
    F3 = F3 - F2;
    F3 = F3 * F0;
    F2 = F2 + F3;
    F4 = F4 * F2;
    R2 = 127;
    R2 = R2 - R1;
    R2 = LSHIFT R2 BY 2;
    R2 = R2 + R15;
    I2 = R2;
    F2 = DM(1,I2);
    F3 = DM(0,I2);
    F3 = F3 - F2;
    F3 = F3 * F0;
    F2 = F2 + F3;
    F5 = F5 * F2;
    F4 = F4 + F5;
    R6 = R6 - 1;
    DM(CH_FADE_LEFT,I3) = R6;
_ph_out:
    DM(I1,M6) = R4;
    // ---- the seam: forward when the delay has come down to it, reverse when it has grown to it
    R1 = DM(CH_SEAM,I3);
    R7 = PASS R7;
    IF LT JUMP _ph_seam_reverse;
    COMP(R8,R1);
    IF GT JUMP _ph_next;
    R1 = DM(CH_HARD,I3);
    COMP(R8,R1);
    IF LE JUMP _ph_turn;
    JUMP _ph_seam;
_ph_seam_reverse:
    COMP(R8,R1);
    IF LT JUMP _ph_next;
    R1 = DM(CH_HARD,I3);
    COMP(R8,R1);
    IF GE JUMP _ph_turn;
_ph_seam:
    R0 = DM(CH_A_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _ph_next;
    R6 = PASS R6;
    IF NE JUMP _ph_next;
    // Head B goes on with this turn; head A starts the next one.
    DM(CH_B_KIND,I3) = R0;
    DM(CH_B_DINT,I3) = R8;
    DM(CH_B_FRAC,I3) = R9;
    DM(CH_B_DIR,I3) = R7;
    R0 = DM(CH_JUMP,I3);
    R8 = R8 + R0;
    R9 = 0;
    R0 = DM(CH_SEAM_FADE,I3);
    DM(CH_FADE_LEFT,I3) = R0;
    DM(CH_FADE_LEN,I3) = R0;
    R0 = DM(CH_SEAM_INV,I3);
    DM(CH_FADE_INV,I3) = R0;
    JUMP _ph_next;
_ph_turn:
    // The crossfade could not start in time (another one was running): turn without one rather than leave the loop.
    R0 = DM(CH_A_KIND,I3);
    R0 = PASS R0;
    IF EQ JUMP _ph_next;
    R0 = DM(CH_JUMP,I3);
    R8 = R8 + R0;
    R9 = 0;
_ph_next:
    R10 = R10 + R12;
    R14 = R14 - 1;
    IF GT JUMP _ph_sample;
    DM(CH_A_DINT,I3) = R8;
    DM(CH_A_FRAC,I3) = R9;
    RTS;

// The crossfade's gain: sin(pi / 2 * s(k / 128)) with s(u) = 3 u^2 - 2 u^3, k = 0 .. 128. The gain of the head that comes
// is entry k, of the head that goes entry 128 - k: their squares add up to 1, and both ends have no slope
// (tests/engine.rig.test.ts checks the table).
.PKHEX _prism_fade, 00000000d4029639c239953abffe263b84a7933b4d7be53b8157243c537b5e3c7a81903c00e4b53ce751df3cd75b063dd0001f3de68d393dfff8553ddd37743d0a208a3d86039b3dfec0ac3df352bf3dc8b3d23dc4dde63d0ecbfb3dd6ba083ec1eb133e2a751f3ede532b3e9c84373e0f04443ecfce503e64e15d3e42386b3ecccf783e2a52833e0e598a3eaa7a913e0eb5983e4606a03e526ca73e31e5ae3ed86eb63e3a07be3e45acc53ee25bcd3efa13d53e72d2dc3e2f95e43e145aec3e071ff43ef0e1fb3e5cd0013fa6ac053fd084093fd5570d3fb624113f76ea143f1da8183fb75c1c3f5807203f19a7233f183b273f7bc22a3f713c2e3f2fa8313ff304353f0452383fb28e3b3f57ba3e3f56d4413f1ddc443f24d1473ff0b24a3f0d814d3f163b503fafe0523f8971553f61ed573ffd535a3f33a55c3fe1e05e3ff206613f5e17633f2712653f5cf7663f14c7683f76816a3fb0266c3ffcb66d3f9e326f3fe599703f27ed713fc72c733f2f59743fcf72753f247a763faf6f773ff853783f8f27793f0aeb793f039f7a3f1a447b3ff2da7b3f34647c3f8ae07c3fa0507d3f27b57d3fce0e7e3f455e7e3f3da47e3f66e17e3f6e167f3f00447f3fc76a7f3f688b7f3f84a67f3fb8bc7f3f9ace7f3fbbdc7f3fa5e77f3fd8ef7f3fcef57f3ff5f97f3fb4fc7f3f65fe7f3f56ff7f3fcaff7f3ff5ff7f3fffff7f3f0000803f;
