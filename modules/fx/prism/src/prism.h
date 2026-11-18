#ifndef PRISM_H
#define PRISM_H

/* Module-private state offsets, in 32-bit words. Provider offsets are kept in
 * the versioned phrase ABI; no native device address appears in this module. */
#define PS_INIT 0
#define PS_INPUT 1
#define PS_OUTPUT 2
#define PS_BLOCK 3
#define PS_CONTROL_SEQ 4
#define PS_ACK 5
#define PS_SELECTION 6
#define PS_EPOCH 7
#define PS_ROLL_BANK 8
#define PS_WRITE_BLOCK 9
#define PS_VALID_BLOCKS 10
#define PS_HELD_BANK 11
#define PS_HELD_START 12
#define PS_HELD_FRAMES 13
#define PS_HELD_MASK 14
#define PS_OLD_BANK 15
#define PS_OLD_START 16
#define PS_OLD_FRAMES 17
#define PS_OLD_MASK 18
#define PS_HANDOFF 19
#define PS_MODE 20
#define PS_ERROR 21
#define PS_ARM_FROM 22
#define PS_ARM_GOAL 23
#define PS_ARM_CUE 24
#define PS_ARM_SEQUENCE 25
#define PS_START_PHASE 26
#define PS_START_FRAME 27
#define PS_CAPTURE_BLOCKS 28
#define PS_ARM_MASK 29
#define PS_OWNER 30
#define PS_PREV_PHASE 31
#define PS_CAPACITY 32
#define PS_BLOCKS 33
#define PS_STAMP_BYTES 34
#define PS_BANK_BYTES 35
#define PS_BANK0 36
#define PS_BANK1 37
#define PS_ACTIVE 38
#define PS_LIMIT 39
#define PS_SCAN 40
#define PS_CROP_START 41
#define PS_CROP_LENGTH 42
#define PS_LAST_POS 43
#define PS_RNG 44
#define PS_LAUNCH_PHASE 45
#define PS_LAUNCH_VALID 46
#define PS_NORMALIZE 47
#define PS_LAST_LEFT 48
#define PS_LAST_RIGHT 49
#define PS_SMOOTH 50
#define PS_OLD_CROP_START 51
#define PS_OLD_CROP_LENGTH 52
#define PS_TEMP 53
#define PS_TEMP1 54
#define PS_TEMP2 55
#define PS_TEMP3 56
#define PS_TEMP4 57
#define PS_TEMP5 58
#define PS_TEMP6 59
#define PS_TEMP7 60
#define PS_FILTER_LP_L 61
#define PS_FILTER_LP_R 62
#define PS_FILTER_HP_L 63
#define PS_CONTROL 64
#define PS_GRAINS 128
#define PRISM_GRAINS 8
#define PRISM_GRAIN_WORDS 16
#define PG_REMAIN 0
#define PG_AGE 1
#define PG_DURATION 2
#define PG_INV_DURATION 3
#define PG_PHASE 4
#define PG_RATE 5
#define PG_BANK 6
#define PG_START 7
#define PG_FRAMES 8
#define PG_SHAPE 9
#define PG_PAN_LEFT 10
#define PG_PAN_RIGHT 11
#define PG_OLD 12
#define PG_GAIN 13
#define PG_ORIGIN 14
#define PG_RETIRE 15
#define PS_SPACE 256
#define PS_SPACE_INDEX 256
#define PS_FILTER_HP_R 257
#define PS_BANK_GENERATION 258
#define PS_HISTORY_READY 259
#define PS_DIFF_CURRENT 260
#define PS_DECAY_CURRENT 261
#define PS_HPF_CURRENT 262
#define PS_LPF_CURRENT 263
#define PS_DIFF_STEP 264
#define PS_DECAY_STEP 265
#define PS_HPF_STEP 266
#define PS_LPF_STEP 267
#define PS_SPACE_LINE0 272
#define PS_SPACE_LINE1 528
#define PS_SPACE_LINE2 784
#define PS_SPACE_LINE3 1040
/* The tape delay (tape.s): one stereo head on the rolling bank. Per channel the delay in frames (integer and fraction, slewed
 * for the tape glide), the counter and length of a reverse segment or of a held loop; the feedback filters; then the head's
 * output of this block and the feedback the write adds to the tape (32 left, 32 right each). */
#define PS_TAPE 1296
#define PT_DINT_L (PS_TAPE + 0)
#define PT_DFRAC_L (PS_TAPE + 1)
#define PT_COUNT_L (PS_TAPE + 2)
#define PT_LENGTH_L (PS_TAPE + 3)
#define PT_DINT_R (PS_TAPE + 4)
#define PT_DFRAC_R (PS_TAPE + 5)
#define PT_COUNT_R (PS_TAPE + 6)
#define PT_LENGTH_R (PS_TAPE + 7)
#define PT_LAST_L (PS_TAPE + 8)
#define PT_LAST_R (PS_TAPE + 9)
#define PT_MODE (PS_TAPE + 10)
#define PT_READABLE (PS_TAPE + 11)
#define PT_LFO (PS_TAPE + 12)
#define PT_LP_L (PS_TAPE + 13)
#define PT_LP_R (PS_TAPE + 14)
#define PT_HP_X_L (PS_TAPE + 15)
#define PT_HP_Y_L (PS_TAPE + 16)
#define PT_HP_X_R (PS_TAPE + 17)
#define PT_HP_Y_R (PS_TAPE + 18)
#define PT_FDBK (PS_TAPE + 19)
#define PT_FDBK_NOW (PS_TAPE + 20)
#define PT_GAIN_GRAIN (PS_TAPE + 21)
#define PT_DRIVE (PS_TAPE + 22)
#define PT_OUT (PS_TAPE + 32)
#define PT_FEEDBACK (PS_TAPE + 96)
#define PRISM_STATE_BYTES 5824
/* Head modes (PT_MODE). */
#define PT_FORWARD 0
#define PT_REVERSE 1
#define PT_HOLD 2
/* The shortest delay: a head never reads the block being written (32 frames) or its interpolation neighbour. */
#define PRISM_TAPE_MIN 48
#define PRISM_HANDOFF_BLOCKS 30
#define PRISM_MIN_CROP 32
#define PC_ENABLED (PS_CONTROL + PH_C_ENABLED / 4)
#define PC_CMD_SEQ (PS_CONTROL + PH_C_CMD_SEQ / 4)
#define PC_ACTION (PS_CONTROL + PH_C_ACTION / 4)
#define PC_FROM (PS_CONTROL + PH_C_FROM / 4)
#define PC_TAKE (PS_CONTROL + PH_C_TAKE_STEPS / 4)
#define PC_CUE (PS_CONTROL + PH_C_CUE / 4)
#define PC_SELECTION (PS_CONTROL + PH_C_SELECTION_EPOCH / 4)
#define PC_MASK (PS_CONTROL + PH_C_NEXT_MASK / 4)
#define PC_EPOCH (PS_CONTROL + PH_C_TIMELINE_EPOCH / 4)
#define PC_FLAGS (PS_CONTROL + PH_C_TIMING_FLAGS / 4)
#define PC_PHASE (PS_CONTROL + PH_C_MASTER_PHASE / 4)
#define PC_BEAT (PS_CONTROL + PH_C_BEAT_PHASE / 4)
#define PC_STEP (PS_CONTROL + PH_C_STEP / 4)
#define PC_LENGTH (PS_CONTROL + PH_C_LENGTH / 4)
#define PC_STEP_SEQ (PS_CONTROL + PH_C_STEP_SEQUENCE / 4)
#define PC_CROP_START (PS_CONTROL + PH_C_CROP_START / 4)
#define PC_CROP_LEN (PS_CONTROL + PH_C_CROP_LEN / 4)
#define PC_SIZE (PS_CONTROL + PH_C_SIZE_FRAMES / 4)
#define PC_RATE (PS_CONTROL + PH_C_RATE_BEATS / 4)
#define PC_PITCH (PS_CONTROL + PH_C_PITCH_RATIO / 4)
#define PC_SHAPE (PS_CONTROL + PH_C_SHAPE / 4)
#define PC_SPRAY (PS_CONTROL + PH_C_SPRAY / 4)
#define PC_JITTER (PS_CONTROL + PH_C_JITTER / 4)
#define PC_POS (PS_CONTROL + PH_C_POS / 4)
#define PC_SCAN (PS_CONTROL + PH_C_SCAN_RATIO / 4)
#define PC_REVERSE (PS_CONTROL + PH_C_REVERSE / 4)
#define PC_WIDTH (PS_CONTROL + PH_C_WIDTH / 4)
#define PC_DIFF (PS_CONTROL + PH_C_DIFF / 4)
#define PC_DECAY (PS_CONTROL + PH_C_DECAY_FEEDBACK / 4)
#define PC_HPF (PS_CONTROL + PH_C_HPF_COEFF / 4)
#define PC_LPF (PS_CONTROL + PH_C_LPF_COEFF / 4)
#define PC_OWNER (PS_CONTROL + PH_C_OWNER_GENERATION / 4)
#define PC_TIME (PS_CONTROL + PH_C_TAPE_TIME / 4)
#define PC_FDBK (PS_CONTROL + PH_C_TAPE_FEEDBACK / 4)
#define PC_TAPE (PS_CONTROL + PH_C_TAPE_PACK / 4)
#define PC_DRIVE (PS_CONTROL + PH_C_DRIVE / 4)

#endif
