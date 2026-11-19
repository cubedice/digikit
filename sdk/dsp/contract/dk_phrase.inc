// Control ABI of the exclusive phrase effect (a repeater), version 3. Pure numeric definitions: this file is the one home
// of the layout, shared by the control provider (core/phrase-control, through <dk/phrase.h>), the page (core/phrase-ui) and the
// effect's engine. PH_C_*, PH_S_* and PH_STORAGE_* are BYTE offsets (DM(n,Ix) uses words: divide by 4).
// Version 2 was the tape delay with grains and phrase capture; its words are gone and an engine refuses a mailbox of another version.
#define PH_VERSION 3
#define PH_CONTROL_MAGIC 0x50484331
#define PH_STATUS_MAGIC 0x50485331
#define PH_STORAGE_MAGIC 0x50484231
#define PH_REPLY_MAGIC 0x50485231
#define PH_CHECK_SALT 0x73509c21
#define PH_CONTROL_WORDS 64
#define PH_STATUS_WORDS 32
// The storage descriptor of core/loop-storage has its own format number. The engine uses the first bank.
#define PH_STORAGE_FORMAT 1
#define PH_STORAGE_MAGIC_OFF 0
#define PH_STORAGE_VERSION 4
#define PH_STORAGE_BANK0 8
#define PH_STORAGE_BANK1 12
#define PH_STORAGE_BANK_BYTES 16
#define PH_STORAGE_FRAMES 20
#define PH_STORAGE_BYTES 24

// ---- the control mailbox, 64 words, published once a block by the provider and read once a block by the engine.
// The envelope: magic, version, size in bytes, the block sequence at both ends, enabled, the owner, a checksum of the transport.
#define PH_C_MAGIC 0
#define PH_C_VERSION 4
#define PH_C_SIZE 8
#define PH_C_SEQ 12
#define PH_C_ENABLED 16
// The engine's controls. A value out of range is clamped; a float that is not finite is a link fault.
//   ZONE      int 1..8    the time range. A change crossfades.
//   RATE      float 0..1  the position inside the zone: time = low * (high / low) ^ RATE. A change glides.
//   FLAGS     bits        PH_F_SYNC, PH_F_FLIP, PH_F_HOLD.
//   SYNC_TIME int frames  with SYNC: the time (a note length at the tempo; the provider's table). A change crossfades.
//   RPTS      float 0..1  repeats: 0 one echo, 0.75 unity, 1 a gain of 1.1.
//   COLR      float -1..1 colour of the repeats: below 0 dark, 0 flat, above 0 bright; its square is the amount of the filter.
//   HALO      float 0..1  diffusion inside the repeat path; 0 is an exact bypass.
//   SKEW      float -1..1 above 0 the left side is shorter and the right longer by up to 1.5; below 0 the reverse. A change glides.
//   MIX       float 0..1  level of the wet output (the engine writes the wet signal only; the effect row's own mix stays 1).
#define PH_C_ZONE 32
#define PH_C_RATE 36
#define PH_C_FLAGS 40
#define PH_C_SYNC_TIME 44
#define PH_C_RPTS 48
#define PH_C_COLR 52
#define PH_C_HALO 56
#define PH_C_SKEW 60
#define PH_C_MIX 64
// Kept for the loop as PRISM's input (INPT: 0 the tracks' sends, 1 the loop), the crossfader (float 0 live .. 1 loop) and
// the count of ARM presses: the provider publishes 0 and nothing reads them yet.
#define PH_C_INPUT 68
#define PH_C_XFADE 72
#define PH_C_ARM_SEQ 76
#define PH_C_OWNER_ROW 220
#define PH_C_OWNER_GENERATION 224
// The tempo the provider read, BPM * 120 (0: none). The engine does not use it.
#define PH_C_TEMPO_120 228
#define PH_C_SEQ_END 248
#define PH_C_CHECK 252
#define PH_F_SYNC 1
#define PH_F_FLIP 2
#define PH_F_HOLD 4
#define PH_ZONES 8

// ---- the status mailbox, 32 words, written by the engine once a block and carried back to the provider.
#define PH_S_MAGIC 0
#define PH_S_VERSION 4
#define PH_S_HEARTBEAT 8
#define PH_S_STATE 12
#define PH_S_ERROR 16
// The two sides' times in frames, the hold state (0 running, 1 taken and still writing, 2 standing) and the frames of written tape.
#define PH_S_TIME_L 32
#define PH_S_TIME_R 36
#define PH_S_HOLD 40
#define PH_S_TAPE 44
// Kept for the loop: what the input is, the loop's frames, whether ARM is pending. The engine writes 0.
#define PH_S_INPUT 48
#define PH_S_LOOP 52
#define PH_S_ARMED 56
#define PH_S_OWNER_GENERATION 96
#define PH_S_SEQ_END 124
// States: the tape is still too short to read (FILLING), the repeats run (READY), HOLD stands (HELD).
#define PH_FILLING 1
#define PH_READY 2
#define PH_HELD 5
#define PH_ERROR_NONE 0
#define PH_ERROR_STORAGE 5
#define PH_ERROR_LINK 6

// ---- the raw values the page turns and the provider keeps (dkrt_phrase_get / dkrt_phrase_set).
//   ZONE 0..7; RATE 0..PH_RATE_MAX in fine steps (an encoder detent is PH_RATE_COARSE of them); SYNC, FLIP, HOLD 0 or 1;
//   NOTE 0..PH_TIME_CHOICES - 1, the note length used with SYNC; RPTS 0..127 with PH_RPTS_UNITY as unity;
//   COLR and SKEW 0..127 with 64 as the centre; HALO, MIX 0..127. INPT and XFAD are kept for the loop and are not turned yet.
#define PH_P_ZONE 0
#define PH_P_RATE 1
#define PH_P_SYNC 2
#define PH_P_NOTE 3
#define PH_P_FLIP 4
#define PH_P_HOLD 5
#define PH_P_RPTS 6
#define PH_P_COLR 7
#define PH_P_HALO 8
#define PH_P_SKEW 9
#define PH_P_MIX 10
#define PH_P_INPT 11
#define PH_P_XFAD 12
#define PH_PARAM_COUNT 13
#define PH_RATE_MAX 1016
#define PH_RATE_COARSE 8
#define PH_RPTS_UNITY 96
// NOTE is a list of note lengths, in 1/96 of a quarter note (the ticks table of the control provider).
#define PH_TIME_CHOICES 16
