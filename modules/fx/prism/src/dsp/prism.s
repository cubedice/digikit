// PRISM: a stereo repeater on one tape. The tape is the first leased bank (two planes of float frames); the block's input
// and the feedback are written to it, and two read heads a side play it back. Audio comes from the effect framework, the
// controls from the control mailbox of a versioned provider; no stock address is embedded here.
//
// One block:
//   plan (this file)    the controls become the two sides' times, the hold state and each side's head program
//   heads (heads.s)     the heads read the tape as the last block left it: PS_Y
//   colour (loop.s)     DC blocker and the colour filter, in place
//   halo (halo.s)       the diffusion network, in place: PS_Y is now the wet signal
//   loop (loop.s)       output = wet * MIX; feedback = limit(wet * gain); the tape takes input + feedback
//
// The colour filter and HALO sit between the heads and the tape, so each pass through the loop adds to them. While HOLD
// stands the tape is not written: what it holds repeats unchanged, and colour and HALO work on the output only.
#include "dk_fx.inc"
#include "dk_phrase_imports.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_process;
.GLOBAL _pr_recip;
.GLOBAL _pr_smooth;
.EXTERN _prism_program;
.EXTERN _prism_heads;
.EXTERN _prism_colour;
.EXTERN _prism_halo;
.EXTERN _prism_loop;

_prism_process:
    I5 = R4;
    I4 = R8;
    R0 = DM(DK_FX_IN_W_IN,I4);
    DM(PS_INPUT,I5) = R0;
    R0 = DM(DK_FX_IN_W_OUT,I4);
    DM(PS_OUTPUT,I5) = R0;
    R0 = DM(DK_FX_IN_W_BLOCK,I4);
    DM(PS_BLOCK,I5) = R0;
    I4 = _phrase_control_info;
    R0 = DM(PH_C_MAGIC / 4,I4);
    R1 = PH_CONTROL_MAGIC;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    R0 = DM(PH_C_VERSION / 4,I4);
    R1 = PRISM_CONTROL_VERSION;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    R0 = DM(PH_C_SIZE / 4,I4);
    R1 = PH_CONTROL_WORDS * 4;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    R0 = DM(PH_C_ENABLED / 4,I4);
    R0 = PASS R0;
    IF EQ JUMP _prism_bad_link;
    R6 = DM(PH_C_SEQ / 4,I4);
    R0 = DM(PH_C_SEQ_END / 4,I4);
    COMP(R6,R0);
    IF NE JUMP _prism_bad_link;
    // The snapshot is immutable for this call. The provider updates before the FX callback; the second sequence read
    // rejects a mixed publication.
    R0 = I5;
    R1 = PS_CONTROL * 4;
    R2 = R0 + R1;
    I3 = R2;
    LCNTR = PH_CONTROL_WORDS, DO _prism_control_copy UNTIL LCE;
        R0 = DM(I4,M6);
_prism_control_copy: DM(I3,M6) = R0;
    I4 = _phrase_control_info;
    R0 = DM(PH_C_SEQ / 4,I4);
    COMP(R0,R6);
    IF NE JUMP _prism_bad_link;
    DM(PS_CONTROL_SEQ,I5) = R6;
    // A float control that is not finite is refused before it reaches a filter or an index.
    R1 = 0x7f800000;
    R0 = DM(PC_RATE,I5);
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF EQ JUMP _prism_bad_link;
    R0 = I5;
    R2 = PC_RPTS * 4;
    R0 = R0 + R2;
    I3 = R0;
    R3 = 5;
_prism_finite_control:
    R0 = DM(0,I3);
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF EQ JUMP _prism_bad_link;
    MODIFY(I3,1) (NW);
    R3 = R3 - 1;
    IF GT JUMP _prism_finite_control;

    // The tape is the first bank of the lease: two planes of FRAMES floats.
    I4 = _phrase_storage_info;
    R0 = DM(PH_STORAGE_MAGIC_OFF / 4,I4);
    R1 = PH_STORAGE_MAGIC;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PH_STORAGE_VERSION / 4,I4);
    R1 = PH_STORAGE_FORMAT;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_storage;
    R6 = DM(PH_STORAGE_BANK0 / 4,I4);
    R8 = DM(PH_STORAGE_BANK_BYTES / 4,I4);
    R9 = DM(PH_STORAGE_FRAMES / 4,I4);
    R0 = 31;
    R1 = R9 AND R0;
    R1 = PASS R1;
    IF NE JUMP _prism_bad_storage;
    R0 = 4096;
    COMP(R9,R0);
    IF LT JUMP _prism_bad_storage;
    R0 = 1032192;
    COMP(R9,R0);
    IF GT JUMP _prism_bad_storage;
    // A lease cannot move under the heads. A reload supplies a fresh zeroed state; a descriptor that changes in place
    // fails closed.
    R0 = DM(PS_INIT,I5);
    R0 = PASS R0;
    IF EQ JUMP _prism_storage_extent;
    R0 = DM(PS_BANK,I5);
    COMP(R0,R6);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PS_CAPACITY,I5);
    COMP(R0,R9);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PS_BANK_BYTES,I5);
    COMP(R0,R8);
    IF NE JUMP _prism_bad_storage;
_prism_storage_extent:
    R0 = LSHIFT R9 BY 3;
    COMP(R8,R0);
    IF LT JUMP _prism_bad_storage;
    // The bank lies in external memory, and its end does not wrap.
    R0 = 3;
    R1 = R6 AND R0;
    R1 = PASS R1;
    IF NE JUMP _prism_bad_storage;
    R0 = 0x80000000;
    R1 = R6 XOR R0;
    R3 = 0x20000000;
    R1 = PASS R1;
    IF LT JUMP _prism_bad_storage;
    COMP(R1,R3);
    IF GE JUMP _prism_bad_storage;
    R4 = R3 - R1;
    COMP(R8,R4);
    IF GT JUMP _prism_bad_storage;
    R0 = DM(PS_INIT,I5);
    R0 = PASS R0;
    IF EQ JUMP _prism_initialize;
    R0 = DM(PS_OWNER,I5);
    R1 = DM(PC_OWNER,I5);
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    JUMP _prism_run;
_prism_initialize:
    // The state is zero: both heads silent, no tape yet.
    R0 = 1;
    DM(PS_INIT,I5) = R0;
    DM(PS_BANK,I5) = R6;
    DM(PS_CAPACITY,I5) = R9;
    DM(PS_BANK_BYTES,I5) = R8;
    R0 = DM(PC_OWNER,I5);
    DM(PS_OWNER,I5) = R0;
    R0 = PRISM_XFADE;
    DM(PS_XFADE,I5) = R0;
    R0 = 0x3f800000;
    DM(PS_HALO_C,I5) = R0;
    R0 = 1;
    DM(PS_FIRST,I5) = R0;
    DM(PS_CH_L + CH_A_DIR,I5) = R0;
    DM(PS_CH_L + CH_B_DIR,I5) = R0;
    DM(PS_CH_R + CH_A_DIR,I5) = R0;
    DM(PS_CH_R + CH_B_DIR,I5) = R0;
_prism_run:
    CALL _prism_plan;
    CALL _prism_heads;
    CALL _prism_colour;
    CALL _prism_halo;
    CALL _prism_loop;
    R0 = 0;
    DM(PS_FIRST,I5) = R0;
    CALL _prism_publish;
    R0 = DK_FX_OK;
    JUMP (M13,I13);

_prism_bad_storage:
    R0 = PH_ERROR_STORAGE;
    JUMP _prism_fault;
_prism_bad_link:
    R0 = PH_ERROR_LINK;
_prism_fault:
    DM(PS_ERROR,I5) = R0;
    CALL _prism_publish;
    R0 = -1;
    JUMP (M13,I13);

// The controls become this block's plan: the two sides' times in frames (PS_TIME_L, PS_TIME_R), the hold state, the frame
// the heads' delays count from, and each side's head program (heads.s).
_prism_plan:
    R15 = DM(PC_FLAGS,I5);
    // ---- the base time. SYNC: the frames the provider gives. Free: low * exp(RATE * ln(high / low)) of the zone.
    R0 = PRISM_F_SYNC;
    R0 = R15 AND R0;
    R0 = PASS R0;
    IF EQ JUMP _pl_free;
    R4 = DM(PC_SYNC_TIME,I5);
    JUMP _pl_base;
_pl_free:
    R0 = DM(PC_ZONE,I5);
    R1 = 1;
    COMP(R0,R1);
    IF LT R0 = PASS R1;
    R1 = PRISM_ZONES;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    R0 = R0 - 1;
    R0 = LSHIFT R0 BY 3;
    R1 = _prism_zones;
    R0 = R0 + R1;
    I4 = R0;
    F6 = DM(0,I4);
    F7 = DM(1,I4);
    F0 = DM(PC_RATE,I5);
    R1 = 0;
    F0 = MAX(F0,F1);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    F0 = F0 * F7;
    CALL _pr_exp;
    F0 = F0 * F6;
    R1 = 0x3f000000;
    F0 = F0 + F1;
    R4 = TRUNC F0;
_pl_base:
    R0 = PRISM_TIME_MIN;
    COMP(R4,R0);
    IF LT R4 = PASS R0;
    R0 = DM(PS_CAPACITY,I5);
    R0 = LSHIFT R0 BY -1;
    COMP(R4,R0);
    IF GT R4 = PASS R0;
    // ---- the key: a change of zone, of SYNC or of the SYNC time that changes the base time is a jump (the heads
    // crossfade); any other change of the time is a glide.
    R5 = DM(PC_ZONE,I5);
    R6 = 0;
    R7 = 0;
    R0 = PRISM_F_SYNC;
    R0 = R15 AND R0;
    R0 = PASS R0;
    IF EQ JUMP _pl_key;
    R5 = 0;
    R6 = 1;
    R7 = DM(PC_SYNC_TIME,I5);
_pl_key:
    R0 = DM(PS_KEY_ZONE,I5);
    COMP(R0,R5);
    IF NE JUMP _pl_key_new;
    R0 = DM(PS_KEY_SYNC,I5);
    COMP(R0,R6);
    IF NE JUMP _pl_key_new;
    R0 = DM(PS_KEY_TIME,I5);
    COMP(R0,R7);
    IF EQ JUMP _pl_key_done;
_pl_key_new:
    DM(PS_KEY_ZONE,I5) = R5;
    DM(PS_KEY_SYNC,I5) = R6;
    DM(PS_KEY_TIME,I5) = R7;
    R0 = DM(PS_BASE,I5);
    COMP(R0,R4);
    IF EQ JUMP _pl_key_done;
    R0 = DM(PS_KEYGEN,I5);
    R0 = R0 + 1;
    DM(PS_KEYGEN,I5) = R0;
_pl_key_done:
    DM(PS_BASE,I5) = R4;
    // ---- SKEW: one side time / ratio, the other time * ratio, ratio 1 .. 1.5.
    F8 = FLOAT R4;
    F0 = DM(PC_SKEW,I5);
    R1 = 0x3f800000;
    F0 = MIN(F0,F1);
    R1 = 0xbf800000;
    F0 = MAX(F0,F1);
    DM(PS_TEMP,I5) = R0;
    F0 = ABS F0;
    R1 = 0x3f000000;
    F0 = F0 * F1;
    R1 = 0x3f800000;
    F0 = F0 + F1;
    F9 = F8 * F0;
    CALL _pr_recip;
    F8 = F8 * F0;
    R1 = 0x3f000000;
    F8 = F8 + F1;
    R2 = TRUNC F8;
    F9 = F9 + F1;
    R3 = TRUNC F9;
    // Both sides stay on the tape: a forward head reads its time back, a reverse head twice its time and the crossfade.
    R0 = DM(PS_CAPACITY,I5);
    R1 = 256;
    R0 = R0 - R1;
    R1 = PRISM_F_FLIP;
    R1 = R15 AND R1;
    R1 = PASS R1;
    IF EQ JUMP _pl_longest;
    R0 = LSHIFT R0 BY -1;
_pl_longest:
    R1 = DM(PS_XFADE,I5);
    R0 = R0 - R1;
    COMP(R2,R0);
    IF GT R2 = PASS R0;
    COMP(R3,R0);
    IF GT R3 = PASS R0;
    R1 = PRISM_TIME_MIN;
    COMP(R2,R1);
    IF LT R2 = PASS R1;
    COMP(R3,R1);
    IF LT R3 = PASS R1;
    // SKEW above 0: the left side is the shorter one.
    R0 = DM(PS_TEMP,I5);
    R0 = PASS R0;
    IF GE JUMP _pl_sides;
    R0 = R2;
    R2 = R3;
    R3 = R0;
_pl_sides:
    DM(PS_TIME_L,I5) = R2;
    DM(PS_TIME_R,I5) = R3;

    // ---- HOLD. Taken: the heads' delays count from the frame where the tape will stand, PRISM_WFADE frames on; the
    // tape takes those frames (what follows the loop) and stops. Released: the write goes back to the loop's end and
    // crossfades from those frames into the new signal. While a forward head crossfades, taking waits: that head would
    // run on toward the standing tape's end.
    R14 = DM(PS_HOLD,I5);
    R0 = PRISM_F_HOLD;
    R0 = R15 AND R0;
    R0 = PASS R0;
    IF EQ JUMP _pl_no_hold;
    R14 = PASS R14;
    IF NE JUMP _pl_hold_done;
    R0 = I5;
    R1 = PS_CH_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    CALL _pl_forward_busy;
    R0 = PASS R0;
    IF NE JUMP _pl_hold_done;
    R0 = I5;
    R1 = PS_CH_R * 4;
    R0 = R0 + R1;
    I3 = R0;
    CALL _pl_forward_busy;
    R0 = PASS R0;
    IF NE JUMP _pl_hold_done;
    R14 = 1;
    R0 = PRISM_WFADE;
    DM(PS_POST,I5) = R0;
    R1 = DM(PS_WPOS,I5);
    R1 = R1 + R0;
    R2 = DM(PS_CAPACITY,I5);
    COMP(R1,R2);
    IF GE R1 = R1 - R2;
    DM(PS_HOLD_W,I5) = R1;
    R4 = DM(PS_TIME_R,I5);
    CALL _pl_take;
    R0 = I5;
    R1 = PS_CH_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    R4 = DM(PS_TIME_L,I5);
    CALL _pl_take;
    JUMP _pl_hold_done;
_pl_no_hold:
    R14 = PASS R14;
    IF EQ JUMP _pl_hold_done;
    // While the tape still runs there is nothing to splice: the heads' delays count from the write position again.
    R4 = DM(PS_POST,I5);
    R4 = PASS R4;
    IF NE JUMP _pl_release_heads;
    R4 = PRISM_WFADE;
    R1 = DM(PS_CAPACITY,I5);
    R0 = DM(PS_HOLD_W,I5);
    R0 = R0 - R4;
    IF LT R0 = R0 + R1;
    DM(PS_WPOS,I5) = R0;
    R0 = DM(PS_VALID,I5);
    R0 = R0 - R4;
    R1 = 0;
    COMP(R0,R1);
    IF LT R0 = PASS R1;
    DM(PS_VALID,I5) = R0;
    DM(PS_WFADE,I5) = R4;
_pl_release_heads:
    R0 = I5;
    R1 = PS_CH_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    R0 = -R4;
    CALL _pl_shift;
    R0 = I5;
    R1 = PS_CH_R * 4;
    R0 = R0 + R1;
    I3 = R0;
    R0 = -R4;
    CALL _pl_shift;
    R14 = 0;
    DM(PS_POST,I5) = R14;
_pl_hold_done:
    DM(PS_HOLD,I5) = R14;

    // ---- this block's mode, reference frame and the deepest delay that reads written tape
    R0 = PM_FORWARD;
    R1 = PRISM_F_FLIP;
    R1 = R15 AND R1;
    R1 = PASS R1;
    IF EQ JUMP _pl_mode;
    R0 = PM_REVERSE;
_pl_mode:
    R14 = PASS R14;
    IF EQ JUMP _pl_running;
    R1 = PM_HOLD;
    R0 = R0 + R1;
    DM(PS_MODE,I5) = R0;
    R0 = DM(PS_HOLD_W,I5);
    DM(PS_REF,I5) = R0;
    R0 = 0;
    DM(PS_WADV,I5) = R0;
    R0 = DM(PS_VALID,I5);
    R1 = DM(PS_POST,I5);
    R0 = R0 + R1;
    JUMP _pl_reach;
_pl_running:
    DM(PS_MODE,I5) = R0;
    R0 = DM(PS_WPOS,I5);
    DM(PS_REF,I5) = R0;
    R0 = 1;
    DM(PS_WADV,I5) = R0;
    R0 = DM(PS_VALID,I5);
_pl_reach:
    R1 = DM(PS_CAPACITY,I5);
    R2 = 64;
    R1 = R1 - R2;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    DM(PS_LIMIT,I5) = R0;

    // ---- the slow drift of the dark side, by the dark amount (the square of COLR, as last block left it): a triangle,
    // the left side against the right. None while the tape stands or plays backwards, and none without the dark side.
    F0 = DM(PS_LFO,I5);
    R1 = PRISM_DRIFT_STEP;
    F0 = F0 + F1;
    R1 = 0x3f800000;
    COMP(F0,F1);
    IF LT JUMP _pl_lfo;
    F0 = F0 - F1;
_pl_lfo:
    DM(PS_LFO,I5) = R0;
    R1 = 0x3f000000;
    F0 = F0 - F1;
    F0 = ABS F0;
    R1 = 0x40800000;
    F0 = F0 * F1;
    R1 = 0x3f800000;
    F4 = F0 - F1;
    F5 = DM(PS_COLOUR,I5);
    F5 = -F5;
    R1 = 0x3f800000;
    F5 = MIN(F5,F1);
    R1 = 0;
    COMP(F5,F1);
    IF LE JUMP _pl_no_drift;
    F5 = F5 * F5;
    R0 = DM(PS_MODE,I5);
    R0 = PASS R0;
    IF NE JUMP _pl_no_drift;
    F4 = F4 * F5;
    R0 = DM(PS_TIME_L,I5);
    F0 = FLOAT R0;
    R1 = PRISM_DRIFT_SHARE;
    F0 = F0 * F1;
    R1 = PRISM_DRIFT_FRAMES;
    F0 = MIN(F0,F1);
    F0 = F0 * F4;
    DM(PS_CH_L + CH_DRIFT,I5) = R0;
    R0 = DM(PS_TIME_R,I5);
    F0 = FLOAT R0;
    R1 = PRISM_DRIFT_SHARE;
    F0 = F0 * F1;
    R1 = PRISM_DRIFT_FRAMES;
    F0 = MIN(F0,F1);
    F0 = F0 * F4;
    F0 = -F0;
    DM(PS_CH_R + CH_DRIFT,I5) = R0;
    JUMP _pl_programs;
_pl_no_drift:
    R0 = 0;
    DM(PS_CH_L + CH_DRIFT,I5) = R0;
    DM(PS_CH_R + CH_DRIFT,I5) = R0;
_pl_programs:
    R0 = I5;
    R1 = PS_CH_L * 4;
    R0 = R0 + R1;
    I3 = R0;
    R0 = DM(PS_TIME_L,I5);
    DM(PS_ARG_T,I5) = R0;
    CALL _prism_program;
    R0 = I5;
    R1 = PS_CH_R * 4;
    R0 = R0 + R1;
    I3 = R0;
    R0 = DM(PS_TIME_R,I5);
    DM(PS_ARG_T,I5) = R0;
    CALL _prism_program;
    RTS;

// I3 one side: R0 is not 0 while one of its forward heads crossfades. Uses R1.
_pl_forward_busy:
    R0 = DM(CH_FADE_LEFT,I3);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = 1;
    R1 = DM(CH_A_DIR,I3);
    R1 = PASS R1;
    IF GT RTS;
    R1 = DM(CH_B_DIR,I3);
    R1 = PASS R1;
    IF GT RTS;
    R0 = 0;
    RTS;

// I3 one side, R4 its time: HOLD is taken. The held loop starts where a forward head A reads now, else the side's time
// back; the delays then count from the frame where the tape will stand.
_pl_take:
    R0 = R4;
    R1 = DM(CH_A_KIND,I3);
    R1 = PASS R1;
    IF EQ JUMP _pl_take_span;
    R1 = DM(CH_A_DIR,I3);
    R1 = PASS R1;
    IF LE JUMP _pl_take_span;
    R0 = DM(CH_A_DINT,I3);
_pl_take_span:
    DM(CH_HOLD_SPAN,I3) = R0;
    R0 = 0;
    DM(CH_VEL,I3) = R0;
    R0 = PRISM_WFADE;
    CALL _pl_shift;
    RTS;

// I3 one side, R0 frames: the reference moved, so both heads' delays, the seam and the turning point move with it. Uses R1.
_pl_shift:
    R1 = DM(CH_A_DINT,I3);
    R1 = R1 + R0;
    DM(CH_A_DINT,I3) = R1;
    R1 = DM(CH_B_DINT,I3);
    R1 = R1 + R0;
    DM(CH_B_DINT,I3) = R1;
    R1 = DM(CH_SEAM,I3);
    R1 = R1 + R0;
    DM(CH_SEAM,I3) = R1;
    R1 = DM(CH_HARD,I3);
    R1 = R1 + R0;
    DM(CH_HARD,I3) = R1;
    RTS;

// F0 = exp(F0) for 0 <= F0 <= 2.4: the series to the fifth power of F0 / 8, then squared three times. Uses F1, F2.
_pr_exp:
    R1 = 0x3e000000;
    F0 = F0 * F1;
    R1 = 0x3c088889; // 1/120
    F2 = F0 * F1;
    R1 = 0x3d2aaaab; // 1/24
    F2 = F2 + F1;
    F2 = F2 * F0;
    R1 = 0x3e2aaaab; // 1/6
    F2 = F2 + F1;
    F2 = F2 * F0;
    R1 = 0x3f000000;
    F2 = F2 + F1;
    F2 = F2 * F0;
    R1 = 0x3f800000;
    F2 = F2 + F1;
    F2 = F2 * F0;
    F2 = F2 + F1;
    F2 = F2 * F2;
    F2 = F2 * F2;
    F0 = F2 * F2;
    RTS;

// A smoothed control. F0 the value asked for, F1 the first stage, F2 the value now: F1 the first stage and F0 the value at
// the end of this block. Each stage moves by PRISM_SMOOTH of its distance; both take the value asked for when they are near
// it, and in the first block after the start. Uses F3, F8.
_pr_smooth:
    R8 = DM(PS_FIRST,I5);
    R8 = PASS R8;
    IF NE JUMP _pr_smooth_there;
    R8 = PRISM_SMOOTH_NEAR;
    F3 = F0 - F2;
    F3 = ABS F3;
    COMP(F3,F8);
    IF GE JUMP _pr_smooth_move;
    F3 = F0 - F1;
    F3 = ABS F3;
    COMP(F3,F8);
    IF GE JUMP _pr_smooth_move;
_pr_smooth_there:
    R1 = R0;
    RTS;
_pr_smooth_move:
    R8 = PRISM_SMOOTH;
    F3 = F0 - F1;
    F3 = F3 * F8;
    F1 = F1 + F3;
    F3 = F1 - F2;
    F3 = F3 * F8;
    F0 = F2 + F3;
    RTS;

// F0 = 1 / F0 (two refinements of the seed). Uses F1, F12, F13.
_pr_recip:
    F1 = RECIPS F0;
    R13 = 0x40000000;
    F12 = F0 * F1;
    F12 = F13 - F12;
    F1 = F1 * F12;
    F12 = F0 * F1;
    F12 = F13 - F12;
    F0 = F1 * F12;
    RTS;

// The engine writes every status word except PH_S_FADE and PH_S_ROUTE_FAULT, which the router owns. The sequence is
// published last; a block that was not processed leaves no fresh heartbeat.
_prism_publish:
    I4 = _phrase_status;
    R0 = 0;
    DM(PH_S_SEQ_END / 4,I4) = R0;
    R0 = PH_STATUS_MAGIC;
    DM(PH_S_MAGIC / 4,I4) = R0;
    R0 = PRISM_CONTROL_VERSION;
    DM(PH_S_VERSION / 4,I4) = R0;
    R0 = DM(PS_BLOCK,I5);
    R0 = R0 + 1;
    DM(PH_S_HEARTBEAT / 4,I4) = R0;
    // FILLING while a side has no tape to read yet, HELD while HOLD stands, else READY.
    R1 = PH_READY;
    R2 = PH_FILLING;
    R0 = DM(PS_CH_L + CH_A_KIND,I5);
    R0 = PASS R0;
    IF EQ R1 = PASS R2;
    R0 = DM(PS_CH_R + CH_A_KIND,I5);
    R0 = PASS R0;
    IF EQ R1 = PASS R2;
    R2 = PH_HELD;
    R0 = DM(PS_HOLD,I5);
    R0 = PASS R0;
    IF NE R1 = PASS R2;
    DM(PH_S_STATE / 4,I4) = R1;
    R1 = DM(PS_ERROR,I5);
    DM(PH_S_ERROR / 4,I4) = R1;
    // The words of the capture and of the grains that this engine does not have read as zero.
    R0 = 0;
    DM(PH_S_HELD_MASK / 4,I4) = R0;
    DM(PH_S_OLD_MASK / 4,I4) = R0;
    DM(PH_S_HANDOFF / 4,I4) = R0;
    R1 = I4;
    R2 = PH_S_GOAL_STEPS + 4;
    R1 = R1 + R2;
    I3 = R1;
    LCNTR = 11, DO _prism_status_clear UNTIL LCE;
_prism_status_clear: DM(I3,M6) = R0;
    R1 = I4;
    R2 = PH_S_GRAIN_POS0;
    R1 = R1 + R2;
    I3 = R1;
    LCNTR = 5, DO _prism_status_grains UNTIL LCE;
_prism_status_grains: DM(I3,M6) = R0;
    R1 = DM(PS_TIME_L,I5);
    DM(PRISM_S_TIME_L,I4) = R1;
    R1 = DM(PS_TIME_R,I5);
    DM(PRISM_S_TIME_R,I4) = R1;
    R1 = DM(PS_HOLD,I5);
    DM(PRISM_S_HOLD,I4) = R1;
    R1 = DM(PS_VALID,I5);
    DM(PRISM_S_TAPE,I4) = R1;
    R1 = DM(PS_OWNER,I5);
    DM(PH_S_OWNER_GENERATION / 4,I4) = R1;
    R0 = DM(PS_BLOCK,I5);
    R0 = R0 + 1;
    DM(PH_S_SEQ_END / 4,I4) = R0;
    RTS;

.SECTION/DM seg_dmda;
.GLOBAL _prism_desc;
.VAR _prism_desc = DK_FX_MAGIC;
.VAR _prism_desc1 = DK_FX_V1;
.VAR _prism_desc2 = 0;
.VAR _prism_desc3 = PRISM_STATE_BYTES;
.VAR _prism_desc4 = PRISM_CALL_DEPTH;
.VAR _prism_desc5 = 0;
// The zones: the low end in frames and ln(high / low), two floats each. 1 to 10 ms, 5 to 40 ms, 20 to 120 ms, 60 to
// 350 ms, 150 ms to 1 s, 0.5 to 2.7 s, 1.3 to 5.4 s, 2.7 to 10.7 s (tests/engine.rig.test.ts checks the table).
.PKHEX _prism_zones, 000040428e5d13400000704392150540000070446058e53f0000344545bde13f0000e145d4d4f23f0080bb46ecdbd73f00c07347c546b63f0020fd474641b03f;
