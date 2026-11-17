#include "dk_phrase.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_capture;

_prism_capture:
    R0 = 0;
    DM(PS_HISTORY_READY,I5) = R0;
    // Retire every old reader before its bank becomes the rolling bank. No
    // full-bank clear/copy ever occurs in the audio callback.
    R0 = DM(PS_HANDOFF,I5);
    R0 = PASS R0;
    IF EQ JUMP _pc_history;
    R0 = R0 - 1;
    DM(PS_HANDOFF,I5) = R0;
    IF NE JUMP _pc_history;
    R0 = DM(PS_OLD_BANK,I5);
    DM(PS_ROLL_BANK,I5) = R0;
    R0 = 0;
    DM(PS_OLD_BANK,I5) = R0;
    DM(PS_OLD_MASK,I5) = R0;
    DM(PS_WRITE_BLOCK,I5) = R0;
    DM(PS_VALID_BLOCKS,I5) = R0;
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R2 = R0 + R1;
    I3 = R2;
    R4 = PRISM_GRAINS;
_pc_retire:
    R0 = DM(PG_OLD,I3);
    R0 = PASS R0;
    IF EQ JUMP _pc_retire_next;
    R0 = 0;
    DM(PG_REMAIN,I3) = R0;
_pc_retire_next:
    MODIFY(I3,PRISM_GRAIN_WORDS) (NW);
    R4 = R4 - 1;
    IF GT JUMP _pc_retire;

_pc_history:
    R6 = 0;
    R0 = DM(PC_EPOCH,I5);
    R1 = DM(PS_EPOCH,I5);
    COMP(R0,R1);
    IF EQ JUMP _pc_selection_only;
    // The beat origin restarts with its timeline. A held phrase must not wait
    // for the new clock to catch up to a previous performance's launch time.
    R2 = 0;
    DM(PS_LAUNCH_VALID,I5) = R2;
    // Only an immediately observed, provider-qualified transport start can
    // preserve a numbered cue armed while stopped. Never bridge its history.
    R2 = R1 + 1;
    COMP(R0,R2);
    IF NE JUMP _pc_reset_history;
    R0 = DM(PC_FLAGS,I5);
    R1 = PH_TIMING_VALID + PH_TIMING_RUNNING + PH_TIMING_START;
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF NE JUMP _pc_reset_history;
    R0 = DM(PC_SELECTION,I5);
    R1 = DM(PS_SELECTION,I5);
    COMP(R0,R1);
    IF NE JUMP _pc_reset_history;
    R0 = DM(PS_MODE,I5);
    R1 = PH_ARMED;
    COMP(R0,R1);
    IF NE JUMP _pc_reset_history;
    R0 = DM(PS_ARM_CUE,I5);
    R0 = PASS R0;
    IF LE JUMP _pc_reset_history;
    R6 = PH_ARMED;
    JUMP _pc_reset_history;
_pc_selection_only:
    R0 = DM(PC_SELECTION,I5);
    R1 = DM(PS_SELECTION,I5);
    COMP(R0,R1);
    IF EQ JUMP _pc_clock;
_pc_reset_history:
    CALL _pc_retire_tape;
    R0 = DM(PC_SELECTION,I5);
    DM(PS_SELECTION,I5) = R0;
    R0 = DM(PC_EPOCH,I5);
    DM(PS_EPOCH,I5) = R0;
    R0 = 0;
    DM(PS_VALID_BLOCKS,I5) = R0;
    DM(PS_WRITE_BLOCK,I5) = R0;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    R1 = DM(PS_MODE,I5);
    DM(PS_MODE,I5) = R6;
    R1 = PASS R1;
    IF EQ JUMP _pc_clock;
    R6 = PASS R6;
    IF NE JUMP _pc_clock;
    R0 = PH_ERROR_TIMING;
    DM(PS_ERROR,I5) = R0;
_pc_clock:
    R0 = DM(PC_FLAGS,I5);
    R1 = PH_TIMING_VALID;
    R2 = R0 AND R1;
    R2 = PASS R2;
    IF NE JUMP _pc_commands;
    // The provider validates tempo and master scale even for the stopped grid.

    R0 = 0;
    DM(PS_VALID_BLOCKS,I5) = R0;
    DM(PS_MODE,I5) = R0;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    R0 = PH_ERROR_TIMING;
    DM(PS_ERROR,I5) = R0;
    // CLEAR and CANCEL do not depend on a clock. Acknowledge a rejected ARM
    // too, so REC turns off and that command cannot replay after recovery.
    R0 = DM(PC_CMD_SEQ,I5);
    R1 = DM(PS_ACK,I5);
    COMP(R0,R1);
    IF EQ RTS;
    DM(PS_ACK,I5) = R0;
    R0 = DM(PC_ACTION,I5);
    R1 = PH_ACTION_CLEAR;
    COMP(R0,R1);
    IF EQ JUMP _pc_clear;
    R1 = PH_ACTION_CANCEL;
    COMP(R0,R1);
    IF EQ JUMP _pc_cancel;
    RTS;

_pc_commands:
    R0 = DM(PC_CMD_SEQ,I5);
    R1 = DM(PS_ACK,I5);
    COMP(R0,R1);
    IF EQ JUMP _pc_pending;
    DM(PS_ACK,I5) = R0;
    R0 = DM(PC_ACTION,I5);
    R1 = PH_ACTION_CLEAR;
    COMP(R0,R1);
    IF EQ JUMP _pc_clear;
    R1 = PH_ACTION_CANCEL;
    COMP(R0,R1);
    IF EQ JUMP _pc_cancel;
    R1 = PH_ACTION_ARM;
    COMP(R0,R1);
    IF NE JUMP _pc_pending;
    R0 = DM(PS_HANDOFF,I5);
    R1 = DM(PS_MODE,I5);
    R2 = R0 OR R1;
    R2 = PASS R2;
    IF NE JUMP _pc_pending;
    R0 = DM(PC_MASK,I5);
    R1 = PH_INPUT_MASK;
    R2 = R0 AND R1;
    R2 = PASS R2;
    IF EQ JUMP _pc_no_input;
    DM(PS_ARM_MASK,I5) = R2;
    R0 = DM(PC_TAKE,I5);
    R0 = PASS R0;
    IF NE JUMP _pc_take;
    R0 = DM(PC_LENGTH,I5);
_pc_take:
    R1 = 1;
    COMP(R0,R1);
    IF LT JUMP _pc_bad_take;
    R1 = 128;
    COMP(R0,R1);
    IF GT JUMP _pc_bad_take;
    R1 = LSHIFT R0 BY 16;
    DM(PS_ARM_GOAL,I5) = R1;
    R0 = DM(PC_CUE,I5);
    R1 = DM(PC_LENGTH,I5);
    COMP(R0,R1);
    IF GT JUMP _pc_bad_take;
    R0 = PASS R0;
    IF LT JUMP _pc_bad_take;
    DM(PS_ARM_CUE,I5) = R0;
    R0 = DM(PC_STEP_SEQ,I5);
    DM(PS_ARM_SEQUENCE,I5) = R0;
    R0 = DM(PC_FROM,I5);
    DM(PS_ARM_FROM,I5) = R0;
    R0 = PH_ARMED;
    DM(PS_MODE,I5) = R0;
    R0 = 0;
    DM(PS_ERROR,I5) = R0;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    JUMP _pc_pending;
_pc_cancel:
    R0 = 0;
    DM(PS_MODE,I5) = R0;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    DM(PS_ERROR,I5) = R0;
    JUMP _pc_command_done;
_pc_clear:
    R0 = 0;
    DM(PS_MODE,I5) = R0;
    DM(PS_HELD_BANK,I5) = R0;
    DM(PS_HELD_FRAMES,I5) = R0;
    DM(PS_HELD_MASK,I5) = R0;
    DM(PS_OLD_BANK,I5) = R0;
    DM(PS_OLD_MASK,I5) = R0;
    DM(PS_HANDOFF,I5) = R0;
    DM(PS_VALID_BLOCKS,I5) = R0;
    DM(PS_WRITE_BLOCK,I5) = R0;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    DM(PS_LAUNCH_VALID,I5) = R0;
    DM(PS_SCAN,I5) = R0;
    DM(PS_CROP_START,I5) = R0;
    DM(PS_CROP_LENGTH,I5) = R0;
    DM(PS_LAST_LEFT,I5) = R0;
    DM(PS_LAST_RIGHT,I5) = R0;
    DM(PS_SMOOTH,I5) = R0;
    DM(PS_FILTER_LP_L,I5) = R0;
    DM(PS_FILTER_LP_R,I5) = R0;
    DM(PS_FILTER_HP_L,I5) = R0;
    DM(PS_ERROR,I5) = R0;
    R1 = DM(PS_BANK0,I5);
    DM(PS_ROLL_BANK,I5) = R1;
    R1 = I5;
    R2 = PS_GRAINS * 4;
    R3 = R1 + R2;
    I3 = R3;
    LCNTR = (PRISM_STATE_BYTES / 4 - PS_GRAINS), DO _pc_clear_state UNTIL LCE;
_pc_clear_state: DM(I3,M6) = R0;
_pc_command_done:
    // An emergency command can arrive while timing is invalid. Its effects
    // are immediate, but it must not seed rolling history from that clock.
    R0 = DM(PC_FLAGS,I5);
    R1 = PH_TIMING_VALID;
    R0 = R0 AND R1;
    IF EQ RTS;
    JUMP _pc_write;
_pc_no_input:
    R0 = PH_ERROR_NO_INPUT;
    DM(PS_ERROR,I5) = R0;
    JUMP _pc_write;
_pc_bad_take:
    R0 = PH_ERROR_TIMING;
    DM(PS_ERROR,I5) = R0;
    JUMP _pc_write;

_pc_pending:
    R0 = DM(PS_MODE,I5);
    R1 = PH_RECORDING;
    COMP(R0,R1);
    IF EQ JUMP _pc_recording;
    R1 = PH_ARMED;
    COMP(R0,R1);
    IF NE JUMP _pc_write;
    R0 = DM(PS_ARM_CUE,I5);
    R0 = PASS R0;
    IF EQ JUMP _pc_cue;
    R1 = DM(PC_FLAGS,I5);
    R2 = PH_TIMING_RUNNING;
    R3 = R1 AND R2;
    R3 = PASS R3;
    IF EQ JUMP _pc_write;
    R1 = DM(PC_STEP_SEQ,I5);
    R2 = DM(PS_ARM_SEQUENCE,I5);
    COMP(R1,R2);
    IF EQ JUMP _pc_write;
    R1 = DM(PC_STEP,I5);
    R1 = R1 + 1;
    COMP(R0,R1);
    IF NE JUMP _pc_write;
_pc_cue:
    R0 = DM(PS_ARM_FROM,I5);
    R0 = PASS R0;
    IF NE JUMP _pc_past;
    R0 = DM(PC_PHASE,I5);
    DM(PS_START_PHASE,I5) = R0;
    R0 = DM(PS_WRITE_BLOCK,I5);
    R1 = LSHIFT R0 BY 5;
    DM(PS_START_FRAME,I5) = R1;
    R0 = PH_RECORDING;
    DM(PS_MODE,I5) = R0;
    JUMP _pc_write;
_pc_recording:
    R0 = DM(PC_PHASE,I5);
    R1 = DM(PS_START_PHASE,I5);
    R2 = R0 - R1;
    R1 = DM(PS_ARM_GOAL,I5);
    COMP(R2,R1);
    IF LT JUMP _pc_record_capacity;
    R0 = DM(PS_CAPTURE_BLOCKS,I5);
    R0 = PASS R0;
    IF EQ JUMP _pc_history_missing;
    R1 = LSHIFT R0 BY 5;
    DM(PS_TEMP,I5) = R1;
    R0 = DM(PS_START_FRAME,I5);
    DM(PS_TEMP1,I5) = R0;
    JUMP _pc_commit;
_pc_record_capacity:
    R0 = DM(PS_CAPTURE_BLOCKS,I5);
    R1 = DM(PS_BLOCKS,I5);
    COMP(R0,R1);
    IF LT JUMP _pc_write;
    R0 = PH_ERROR_CAPACITY;
    JUMP _pc_capture_error;

_pc_past:
    // Binary search monotonically increasing per-block phase stamps in the
    // logical ring. Signed differences stay valid for this <=128-step lease.
    R0 = DM(PC_PHASE,I5);
    R1 = DM(PS_ARM_GOAL,I5);
    R2 = R0 - R1;
    DM(PS_TEMP2,I5) = R2;
    R7 = DM(PS_VALID_BLOCKS,I5);
    R7 = PASS R7;
    IF EQ JUMP _pc_history_missing;
    R0 = DM(PS_WRITE_BLOCK,I5);
    R1 = R0 - R7;
    R2 = DM(PS_BLOCKS,I5);
    R1 = PASS R1;
    IF GE JUMP _pc_oldest;
    R3 = R1 + R2;
    R1 = R3;
_pc_oldest:
    DM(PS_TEMP3,I5) = R1;
    R0 = LSHIFT R1 BY 2;
    R1 = DM(PS_ROLL_BANK,I5);
    R2 = DM(PS_STAMP_BYTES,I5);
    R3 = R1 + R2;
    R1 = R3 + R0;
    I3 = R1;
    R0 = DM(0,I3);
    R1 = DM(PS_TEMP2,I5);
    R2 = R0 - R1;
    R2 = PASS R2;
    IF LE JUMP _pc_search_begin;
    // A full ring cannot accumulate an older boundary. Distinguish a take
    // that cannot fit from an ordinary refill that can eventually be ready.
    R0 = DM(PS_BLOCKS,I5);
    COMP(R7,R0);
    IF LT JUMP _pc_history_missing;
    R0 = PH_ERROR_CAPACITY;
    JUMP _pc_capture_error;
_pc_search_begin:
    R6 = 0;
_pc_search:
    COMP(R6,R7);
    IF GE JUMP _pc_found;
    R0 = R6 + R7;
    R8 = LSHIFT R0 BY -1;
    R0 = DM(PS_TEMP3,I5);
    R1 = R0 + R8;
    R2 = DM(PS_BLOCKS,I5);
    COMP(R1,R2);
    IF LT JUMP _pc_search_index;
    R3 = R1 - R2;
    R1 = R3;
_pc_search_index:
    R0 = LSHIFT R1 BY 2;
    R1 = DM(PS_ROLL_BANK,I5);
    R2 = DM(PS_STAMP_BYTES,I5);
    R3 = R1 + R2;
    R1 = R3 + R0;
    I3 = R1;
    R0 = DM(0,I3);
    R1 = DM(PS_TEMP2,I5);
    R2 = R0 - R1;
    R2 = PASS R2;
    IF LT JUMP _pc_search_right;
    R7 = R8;
    JUMP _pc_search;
_pc_search_right:
    R6 = R8 + 1;
    JUMP _pc_search;
_pc_found:
    R0 = DM(PS_VALID_BLOCKS,I5);
    R1 = R0 - R6;
    R1 = PASS R1;
    IF LE JUMP _pc_history_missing;
    DM(PS_CAPTURE_BLOCKS,I5) = R1;
    R0 = LSHIFT R1 BY 5;
    DM(PS_TEMP,I5) = R0;
    R0 = DM(PS_TEMP3,I5);
    R1 = R0 + R6;
    R2 = DM(PS_BLOCKS,I5);
    COMP(R1,R2);
    IF LT JUMP _pc_past_start;
    R3 = R1 - R2;
    R1 = R3;
_pc_past_start:
    R0 = LSHIFT R1 BY 5;
    DM(PS_TEMP1,I5) = R0;
    JUMP _pc_commit;
_pc_history_missing:
    R0 = PH_ERROR_HISTORY;
_pc_capture_error:
    DM(PS_ERROR,I5) = R0;
    R0 = 0;
    DM(PS_MODE,I5) = R0;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    JUMP _pc_write;

_pc_commit:
    R0 = DM(PS_BANK_GENERATION,I5);
    R0 = R0 + 1;
    DM(PS_BANK_GENERATION,I5) = R0;
    R0 = DM(PS_HELD_BANK,I5);
    DM(PS_OLD_BANK,I5) = R0;
    R0 = DM(PS_HELD_START,I5);
    DM(PS_OLD_START,I5) = R0;
    R0 = DM(PS_HELD_FRAMES,I5);
    DM(PS_OLD_FRAMES,I5) = R0;
    R0 = DM(PS_HELD_MASK,I5);
    DM(PS_OLD_MASK,I5) = R0;
    R0 = DM(PS_CROP_START,I5);
    DM(PS_OLD_CROP_START,I5) = R0;
    R0 = DM(PS_CROP_LENGTH,I5);
    DM(PS_OLD_CROP_LENGTH,I5) = R0;
    R0 = DM(PS_ROLL_BANK,I5);
    DM(PS_HELD_BANK,I5) = R0;
    R0 = DM(PS_TEMP,I5);
    DM(PS_HELD_FRAMES,I5) = R0;
    R0 = DM(PS_TEMP1,I5);
    DM(PS_HELD_START,I5) = R0;
    R0 = DM(PS_ARM_MASK,I5);
    DM(PS_HELD_MASK,I5) = R0;
    R0 = 0;
    DM(PS_MODE,I5) = R0;
    DM(PS_ERROR,I5) = R0;
    DM(PS_ROLL_BANK,I5) = R0;
    DM(PS_VALID_BLOCKS,I5) = R0;
    DM(PS_WRITE_BLOCK,I5) = R0;
    DM(PS_LAUNCH_VALID,I5) = R0;
    DM(PS_SCAN,I5) = R0;
    R0 = PRISM_HANDOFF_BLOCKS;
    DM(PS_HANDOFF,I5) = R0;
    // Existing grains become old readers. They are faded and retired before
    // the old bank is reused even if SIZE is longer than the handoff.
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R2 = R0 + R1;
    I3 = R2;
    R4 = PRISM_GRAINS;
    R1 = 1;
    R5 = 0;
_pc_mark_old:
    DM(PG_OLD,I3) = R1;
    R0 = DM(PG_REMAIN,I3);
    R0 = PASS R0;
    IF LE JUMP _pc_mark_next;
    R5 = R5 + 1;
_pc_mark_next:
    MODIFY(I3,PRISM_GRAIN_WORDS) (NW);
    R4 = R4 - 1;
    IF GT JUMP _pc_mark_old;
    R0 = PRISM_GRAINS;
    COMP(R5,R0);
    IF NE JUMP _pc_reader_ready;
    // A saturated old pool must leave room for the new phrase promptly.
    // Fade one old reader to zero over its next block; never steal it with
    // a discontinuous reassignment or exceed the eight-reader ceiling.
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R2 = R0 + R1;
    I3 = R2;
    R0 = DM(PG_REMAIN,I3);
    R1 = 32;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    DM(PG_REMAIN,I3) = R0;
    DM(PG_RETIRE,I3) = R0;
_pc_reader_ready:
    R0 = DM(PS_OLD_BANK,I5);
    R0 = PASS R0;
    IF NE RTS;
    // First take has no old readers: the unused bank is immediately writable.
    R0 = DM(PS_HELD_BANK,I5);
    R1 = DM(PS_BANK0,I5);
    COMP(R0,R1);
    IF NE JUMP _pc_first_other;
    R1 = DM(PS_BANK1,I5);
_pc_first_other:
    DM(PS_ROLL_BANK,I5) = R1;
    R0 = 0;
    DM(PS_HANDOFF,I5) = R0;
    RTS;

// The tape starts again from its first block: the grains that read it fade out over their next 32 samples instead of
// reading a window the new tape is being written into. Uses R0, R1, R4 and I3.
_pc_retire_tape:
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R0 = R0 + R1;
    I3 = R0;
    R4 = PRISM_GRAINS;
_pc_retire_tape_next:
    R0 = DM(PG_FRAMES,I3);
    R0 = PASS R0;
    IF EQ JUMP _pc_retire_tape_skip;
    R0 = DM(PG_REMAIN,I3);
    R1 = 32;
    COMP(R0,R1);
    IF LE JUMP _pc_retire_tape_skip;
    DM(PG_REMAIN,I3) = R1;
    DM(PG_RETIRE,I3) = R1;
_pc_retire_tape_skip:
    MODIFY(I3,PRISM_GRAIN_WORDS) (NW);
    R4 = R4 - 1;
    IF GT JUMP _pc_retire_tape_next;
    RTS;

_pc_write:
    R0 = DM(PS_ROLL_BANK,I5);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = DM(PC_MASK,I5);
    R0 = PASS R0;
    IF EQ RTS;
    // HOLD stops the tape: the head loops what is on it (tape.s). A take in progress keeps writing.
    R0 = DM(PT_MODE,I5);
    R1 = PT_HOLD;
    COMP(R0,R1);
    IF NE JUMP _pc_write_run;
    R0 = DM(PS_MODE,I5);
    R0 = PASS R0;
    IF EQ RTS;
_pc_write_run:
    // Record phase before writing this block. Stereo planes and the metadata
    // tail are disjoint; every sample we later expose was written here.
    R0 = DM(PS_WRITE_BLOCK,I5);
    R1 = LSHIFT R0 BY 2;
    R2 = DM(PS_ROLL_BANK,I5);
    R3 = DM(PS_STAMP_BYTES,I5);
    R4 = R2 + R3;
    R3 = R4 + R1;
    I3 = R3;
    R1 = DM(PC_PHASE,I5);
    DM(0,I3) = R1;
    R1 = LSHIFT R0 BY 7;
    R3 = R2 + R1;
    I1 = R3;
    R0 = DM(PS_CAPACITY,I5);
    R1 = LSHIFT R0 BY 2;
    R2 = R3 + R1;
    I2 = R2;
    I0 = DM(PS_INPUT,I5);
    // The tape takes the input and the head's feedback of this block (read from older tape, so never from these frames).
    R0 = I5;
    R1 = PT_FEEDBACK * 4;
    R0 = R0 + R1;
    I3 = R0;
    LCNTR = 32, DO _pc_write_left UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
_pc_write_left: DM(I1,M6) = R0;
    LCNTR = 32, DO _pc_write_right UNTIL LCE;
        F0 = DM(I0,M6);
        F1 = DM(I3,M6);
        F0 = F0 + F1;
_pc_write_right: DM(I2,M6) = R0;
    R0 = DM(PS_WRITE_BLOCK,I5);
    R1 = R0 + 1;
    R2 = DM(PS_BLOCKS,I5);
    COMP(R1,R2);
    IF LT JUMP _pc_write_index;
    R1 = 0;
_pc_write_index:
    DM(PS_WRITE_BLOCK,I5) = R1;
    R0 = DM(PS_VALID_BLOCKS,I5);
    R1 = R0 + 1;
    COMP(R1,R2);
    IF GT R1 = PASS R2;
    DM(PS_VALID_BLOCKS,I5) = R1;
    // READY means a PAST request for the current TAKE can really be served.
    // The oldest stamp is safe because VALID_BLOCKS became positive above.
    R0 = DM(PS_WRITE_BLOCK,I5);
    R3 = R0 - R1;
    R3 = PASS R3;
    IF GE JUMP _pc_ready_index;
    R0 = R3 + R2;
    R3 = R0;
_pc_ready_index:
    R0 = LSHIFT R3 BY 2;
    R1 = DM(PS_ROLL_BANK,I5);
    R2 = DM(PS_STAMP_BYTES,I5);
    R3 = R1 + R2;
    R1 = R3 + R0;
    I3 = R1;
    R0 = DM(0,I3);
    R1 = DM(PC_PHASE,I5);
    R2 = R1 - R0;
    R0 = DM(PC_TAKE,I5);
    R0 = PASS R0;
    IF NE JUMP _pc_ready_goal;
    R0 = DM(PC_LENGTH,I5);
_pc_ready_goal:
    R1 = LSHIFT R0 BY 16;
    R0 = 1;
    COMP(R2,R1);
    IF LT JUMP _pc_ready_done;
    DM(PS_HISTORY_READY,I5) = R0;
_pc_ready_done:
    R0 = DM(PS_MODE,I5);
    R1 = PH_RECORDING;
    COMP(R0,R1);
    IF NE RTS;
    R0 = DM(PS_CAPTURE_BLOCKS,I5);
    R0 = R0 + 1;
    DM(PS_CAPTURE_BLOCKS,I5) = R0;
    RTS;
