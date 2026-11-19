#ifndef PRISM_H
#define PRISM_H

/* PRISM, the repeater engine (src/dsp). This header holds the constants that can be tuned and the layout of the engine's
 * state. The control words the engine reads and the status words it writes are in sdk/dsp/contract/dk_phrase.inc
 * (version PH_VERSION), which a source includes before this header; the control provider and the page share it. No native
 * device address appears in this module. */

/* Constants that can be tuned. Floats are given as their bits.
 * PRISM_XFADE: samples of the equal-power crossfade between the two heads at a zone change, a SYNC step, FLIP and HOLD
 * (the seam of a reverse segment or a held loop uses at most half the segment). A multiple of 32 is not required.
 * PRISM_WFADE: frames of tape written after HOLD is taken, which are the crossfade into new tape at the release
 * (a multiple of 32). */
#define PRISM_XFADE 960
#define PRISM_WFADE 480
/* 1 / PRISM_WFADE. */
#define PRISM_WFADE_STEP 0x3b088889
/* The glide of a free RATE or SKEW move: the delay follows its target with the velocity (target - delay) / 1024 frames a
 * sample, at most PRISM_GLIDE_MAX (0.5: an octave down, a fifth up), and the velocity itself follows over 256 samples. */
#define PRISM_GLIDE_MAX 0x3f000000
#define PRISM_GLIDE_RATE 0x3a800000
#define PRISM_GLIDE_EASE 0x3b800000
/* COLR at 48 kHz. The dark side's low pass (800 Hz): lp += k (x - lp), k = 1 - exp(-2 pi 800 / 48000). The bright side's
 * high pass (1 kHz): hp = g (x - x1) + r hp1, r = exp(-2 pi 1000 / 48000), g = (1 + r) / 2. */
#define PRISM_DARK_COEFF 0x3dcb9e5b
#define PRISM_BRIGHT_COEFF 0x3f60971c
#define PRISM_BRIGHT_GAIN 0x3f704b8e
/* The slow drift of the dark side: at most 30 frames and at most 2 % of the time, at 0.6 Hz (a triangle). */
#define PRISM_DRIFT_FRAMES 0x41f00000
#define PRISM_DRIFT_SHARE 0x3ca3d70a
#define PRISM_DRIFT_STEP 0x39d1b717
/* The feedback limiter: exact below the knee (0.25), then a cubic curve up to the ceiling (0.5). */
#define PRISM_LIMIT_KNEE 0x3e800000
#define PRISM_LIMIT_SLOPE 0x402aaaab
#define PRISM_LIMIT_SPAN 0x3ec00000
/* HALO at 1: tan of half the all-pass angle (60 degrees, coefficient 0.5). */
#define PRISM_HALO_MAX 0x3f13cd3a
/* The DC blocker in front of the colour filter (2 Hz): y = g (x - x1) + r y1, r = 1 - 2 pi 2 / 48000, g = (1 + r) / 2. */
#define PRISM_DC_COEFF 0x3f7feed8
#define PRISM_DC_GAIN 0x3f7ff76c
/* RPTS, COLR, HALO and MIX follow their controls through two stages, each moving by this share of its distance a block
 * (1/16: a jump is half way after about 18 ms), and take the value asked for when both are within PRISM_SMOOTH_NEAR (1e-4)
 * of it. Two stages: neither the value nor its slope jumps, so a jump of a control is no kink in the signal. */
#define PRISM_SMOOTH 0x3d800000
#define PRISM_SMOOTH_NEAR 0x38d1b717
/* The shortest time: a head never reads the block being written. */
#define PRISM_TIME_MIN 32
/* The loop as PRISM's input (INPT at LOOP): the loop effect's tap, which is the loop at the level of the mix before the master
 * stage (the whole mix at full send), times this trim: -6 dB (10^(-6/20)). Set by ear. */
#define PRISM_LOOP_TRIM 0x3f004dce
/* The step a sample of INPT's crossfade and of the output's move between the FX return and the loop effect: 1/256, a move
 * over 256 frames (5.3 ms). */
#define PRISM_SWITCH_STEP 0x3b800000
/* At a new loop (the loop effect's count in its tap moved) the input is the loop at once, from the block after the change, and
 * stays there this many blocks (0.5 s) while the provider has not set INPT to LOOP: the page does it in its next pass. A
 * crossfade, or waiting for the page, would let the new pattern's first notes into the tape. */
#define PRISM_AUTO_BLOCKS 750

/* State offsets, in 32-bit words. */
#define PS_INIT 0
#define PS_INPUT 1
#define PS_OUTPUT 2
#define PS_BLOCK 3
#define PS_CONTROL_SEQ 4
#define PS_ERROR 5
#define PS_OWNER 6
#define PS_BANK 7
#define PS_CAPACITY 8
#define PS_BANK_BYTES 9
/* The tape: the write position in frames (a multiple of 32) and the frames of written tape behind it. */
#define PS_WPOS 10
#define PS_VALID 11
/* HOLD: 0 running; 1 held, and the tape still takes the PRISM_WFADE frames that follow the loop (PS_POST counts them
 * down); 2 held and standing. PS_HOLD_W is the write position where the tape stands; while held the heads' delays count
 * from it. PS_WFADE counts the frames of the crossfade the write makes after a release. */
#define PS_HOLD 12
#define PS_POST 13
#define PS_HOLD_W 14
#define PS_WFADE 15
#define PS_XFADE 16
/* The key of the heads' program (zone, SYNC, SYNC time): PS_KEYGEN counts its changes that also change the base time. */
#define PS_KEYGEN 17
#define PS_KEY_ZONE 18
#define PS_KEY_SYNC 19
#define PS_KEY_TIME 20
#define PS_BASE 21
/* This block: mode (bit 0 reverse, bit 1 held), the frame the heads' delays count from, its advance per sample (1 or 0)
 * and the largest delay that reads written tape. */
#define PS_MODE 22
#define PS_REF 23
#define PS_WADV 24
#define PS_LIMIT 25
#define PS_LFO 26
#define PS_GAIN 27
#define PS_MIX 28
#define PS_COLOUR 29
#define PS_HALO_C 30
#define PS_HALO_S 31
#define PS_HALO_ON 32
#define PS_HALO_INDEX 33
#define PS_TIME_L 34
#define PS_TIME_R 35
#define PS_ARG_T 36
#define PS_HALO_H 37
/* The first block after the start: the smoothed controls take their values at once. */
#define PS_FIRST 38
#define PS_TEMP 40
#define PS_TEMP1 41
#define PS_TEMP2 42
#define PS_TEMP3 43
#define PS_TEMP4 44
#define PS_TEMP5 45
#define PS_TEMP6 46
#define PS_TEMP7 47
/* The first stages of the smoothed controls (their values are PS_GAIN, PS_MIX, PS_COLOUR and PS_HALO_H). */
#define PS_GAIN_MID 48
#define PS_MIX_MID 49
#define PS_COLOUR_MID 50
#define PS_HALO_MID 51
/* The loop effect: its count LW_TAKEN at this engine's last block; it finished a block since then (this block: 0 or 1); the
 * loop feeds the input (its tap carries a loop); the share of the output the loop effect takes (float 0..1, PS_DIVERT) and
 * the share of the loop in the input (float 0..1, PS_INPT), both at the end of the last block. */
#define PS_OL_LAST 52
#define PS_ALIVE 53
#define PS_FEED 54
#define PS_DIVERT 55
#define PS_INPT 56
/* The loop count the tap showed last, plus 1 (0: none seen yet); the blocks the input stays at LOOP by itself after a new loop
 * while INPT is not LOOP yet; whether the input is LOOP this block (0 or 1). */
#define PS_LOOP_SEEN 57
#define PS_AUTO 58
#define PS_INPUT_ON 59
#define PS_CONTROL 64
/* The snapshot of the control mailbox (PH_CONTROL_WORDS words) and the words the engine reads in it. */
#define PC_ZONE (PS_CONTROL + PH_C_ZONE / 4)
#define PC_RATE (PS_CONTROL + PH_C_RATE / 4)
#define PC_FLAGS (PS_CONTROL + PH_C_FLAGS / 4)
#define PC_SYNC_TIME (PS_CONTROL + PH_C_SYNC_TIME / 4)
#define PC_RPTS (PS_CONTROL + PH_C_RPTS / 4)
#define PC_COLR (PS_CONTROL + PH_C_COLR / 4)
#define PC_HALO (PS_CONTROL + PH_C_HALO / 4)
#define PC_SKEW (PS_CONTROL + PH_C_SKEW / 4)
#define PC_MIX (PS_CONTROL + PH_C_MIX / 4)
#define PC_INPUT (PS_CONTROL + PH_C_INPUT / 4)
#define PC_OWNER (PS_CONTROL + PH_C_OWNER_GENERATION / 4)

/* One side's two heads (heads.s). A head: playing or silent, its delay in frames from the reference (integer and
 * fraction) and its direction on the tape (1 forward, -1 reverse). Head A is the current one; head B is the one a
 * crossfade leaves, silent when CH_FADE_LEFT is 0. */
#define PS_CH_L 128
#define PS_CH_R 160
#define PRISM_CH_WORDS 32
#define CH_A_KIND 0
#define CH_A_DINT 1
#define CH_A_FRAC 2
#define CH_A_DIR 3
#define CH_B_KIND 4
#define CH_B_DINT 5
#define CH_B_FRAC 6
#define CH_B_DIR 7
/* The glide of head A (forward, running tape): velocity, target and drift in frames, and whether it moves this block. */
#define CH_VEL 8
#define CH_TARGET 9
#define CH_DRIFT 10
#define CH_GLIDE 11
/* The crossfade: samples left, its length and 128 / length. */
#define CH_FADE_LEFT 12
#define CH_FADE_LEN 13
#define CH_FADE_INV 14
/* The seam of a reverse segment or a held loop: head A crosses CH_SEAM (forward: delay <= seam, reverse: delay >= seam),
 * head B takes it over for CH_SEAM_FADE samples and head A goes on CH_JUMP frames away. CH_HARD is where head A turns
 * without a crossfade if the seam could not start one. */
#define CH_SEAM 15
#define CH_JUMP 16
#define CH_SEAM_FADE 17
#define CH_SEAM_INV 18
#define CH_HARD 19
/* The program head A follows: its mode and key generation, and the head's delay when HOLD was taken (the held loop
 * starts there). */
#define CH_MODE 20
#define CH_KEYGEN 21
#define CH_HOLD_SPAN 22

/* The filters of one side (loop.s): the DC blocker's last input and output, the dark low pass, and the bright high pass's
 * last input and output. */
#define PS_FILT_L 192
#define PS_FILT_R 200
#define FL_DC_X 0
#define FL_DC_Y 1
#define FL_DARK 2
#define FL_BRIGHT_X 3
#define FL_BRIGHT_Y 4

/* The block's signals, 32 left then 32 right: the heads' output, which the colour filter and HALO change in place into
 * the wet signal, and the feedback the write adds to the tape. */
#define PS_Y 256
#define PS_FB 320
/* HALO: four lines of 256 samples. */
#define PS_LINE0 384
#define PS_LINE1 640
#define PS_LINE2 896
#define PS_LINE3 1152
/* The block's input when the loop is part of it (INPT): 32 left then 32 right. */
#define PS_IN 1408
#define PRISM_STATE_BYTES 5888
#define PRISM_CALL_DEPTH 4

/* Modes (PS_MODE, CH_MODE). */
#define PM_FORWARD 0
#define PM_REVERSE 1
#define PM_HOLD 2
#define PM_HOLD_REVERSE 3

#endif
