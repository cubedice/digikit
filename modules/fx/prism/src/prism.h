#ifndef PRISM_H
#define PRISM_H

/* PRISM, the repeater engine (src/dsp). This header holds the control words the engine reads, the constants that can be
 * tuned, and the layout of its state. No native device address appears in this module.
 *
 * Control words. The engine reads the 64-word mailbox _phrase_control_info once a block. The envelope is that of
 * dk_phrase.inc (word 0 magic, 1 version, 2 size in bytes, 3 sequence, 4 enabled, 56 owner generation, 62 the sequence
 * again); the version is PRISM_CONTROL_VERSION, so the engine refuses the mailbox of an earlier control provider. The
 * engine's own words:
 *
 * | Word | Name      | Type  | Range     | Meaning                                                                        |
 * |------|-----------|-------|-----------|--------------------------------------------------------------------------------|
 * | 8    | ZONE      | int   | 1..8      | Time range (the table _prism_zones in prism.s). A change crossfades.           |
 * | 9    | RATE      | float | 0..1      | Position inside the zone: time = low * (high / low) ^ RATE. A change glides.   |
 * | 10   | FLAGS     | bits  |           | Bit 0 SYNC, bit 1 FLIP, bit 2 HOLD.                                            |
 * | 11   | SYNC_TIME | int   | frames    | With SYNC: the time in frames (a note length at the tempo). A change crossfades.|
 * | 12   | RPTS      | float | 0..1      | Repeats: 0 one echo, 0.75 unity, 1 a gain of 1.1.                              |
 * | 13   | COLR      | float | -1..1     | Colour of the repeats: below 0 dark, 0 flat, above 0 bright; its square is the |
 * |      |           |       |           | amount of the filter.                                                          |
 * | 14   | HALO      | float | 0..1      | Diffusion inside the repeat path; 0 is an exact bypass.                        |
 * | 15   | SKEW      | float | -1..1     | Above 0 the left side is shorter and the right longer by up to 1.5; below 0    |
 * |      |           |       |           | the reverse. A change glides.                                                  |
 * | 16   | MIX       | float | 0..1      | Level of the wet output (the engine writes the wet signal only).               |
 *
 * A value out of range is clamped; a float that is not finite is a link fault. With SYNC the zone is not used.
 */
#define PRISM_CONTROL_VERSION 3
#define PW_ZONE 8
#define PW_RATE 9
#define PW_FLAGS 10
#define PW_SYNC_TIME 11
#define PW_RPTS 12
#define PW_COLR 13
#define PW_HALO 14
#define PW_SKEW 15
#define PW_MIX 16
#define PRISM_F_SYNC 1
#define PRISM_F_FLIP 2
#define PRISM_F_HOLD 4
#define PRISM_ZONES 8

/* Status words the engine publishes in _phrase_status besides the envelope (magic, version, heartbeat, state, error,
 * owner generation, sequence): the two sides' times in frames, the hold state (PS_HOLD) and the readable tape in frames. */
#define PRISM_S_TIME_L 8
#define PRISM_S_TIME_R 9
#define PRISM_S_HOLD 10
#define PRISM_S_TAPE 11

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
#define PS_CONTROL 64
#define PC_ZONE (PS_CONTROL + PW_ZONE)
#define PC_RATE (PS_CONTROL + PW_RATE)
#define PC_FLAGS (PS_CONTROL + PW_FLAGS)
#define PC_SYNC_TIME (PS_CONTROL + PW_SYNC_TIME)
#define PC_RPTS (PS_CONTROL + PW_RPTS)
#define PC_COLR (PS_CONTROL + PW_COLR)
#define PC_HALO (PS_CONTROL + PW_HALO)
#define PC_SKEW (PS_CONTROL + PW_SKEW)
#define PC_MIX (PS_CONTROL + PW_MIX)
#define PC_OWNER (PS_CONTROL + 56)

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
#define PRISM_STATE_BYTES 5632
#define PRISM_CALL_DEPTH 4

/* Modes (PS_MODE, CH_MODE). */
#define PM_FORWARD 0
#define PM_REVERSE 1
#define PM_HOLD 2
#define PM_HOLD_REVERSE 3

#endif
