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
// The receiver keeps the last good mailbox while frames are refused (a repeated, late or torn frame on the device): after
// PH_RX_GRACE refused frames in a row (half a second) it clears the mailbox's magic and enabled word.
#define PH_RX_GRACE 750
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
//   SYNC_TIME int frames  with SYNC: the time, a ratio of the zone's 1/1 at the tempo (the provider's, below). A change
//                         crossfades.
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
//   INPUT     int 0, 1    INPT: 0 the tracks by their send amounts (LIVE), 1 the loop effect's loop (LOOP): its tap of the block
//                         before (dk_loop.inc) times the engine's PRISM_LOOP_TRIM; silence while the loop effect does not run or
//                         has no loop. A change crossfades the input. It may be 1 while there is no loop: the engine then
//                         hears silence.
// XFADE and ARM_SEQ are unused and 0: the crossfader and ARM are the loop effect's controls, held by core/loop-control.
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
// The input as the engine takes it: INPUT (0 LIVE, 1 LOOP); LOOP 1 while the loop feeds it (the loop effect runs and its tap
// carries a loop); ARMED is unused and 0. ROUTE: 0 the output goes to the FX return, 1 the loop effect adds it behind its
// crossfader (it runs; the engine moves the output there over PRISM_SWITCH_STEP a sample).
#define PH_S_INPUT 48
#define PH_S_LOOP 52
#define PH_S_ARMED 56
#define PH_S_ROUTE 60
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
//   RATIO 0..PH_SYNC_RATIOS - 1, the ratio of the zone's 1/1 used with SYNC (below); RPTS 0..127 with PH_RPTS_UNITY as
//   unity; COLR and SKEW 0..127 with 64 as the centre; HALO, MIX 0..127; INPT 0 or 1, not saved: the page's G turns it at
//   any time, and it goes to LOOP by itself at a new loop and to LIVE when the loop is dropped or the loop effect stops
//   reporting. XFAD is unused: the crossfader is the loop effect's (dkrt_loop_*).
//   TRKS 0..PH_TRACKS_ALL: the tracks PRISM hears at LIVE, bit t for track t + 1 (the TRIG keys while G is held on the
//   page); saved. It is no control word: the provider hands the fx service PH_SEND_FULL as the send amount of a heard track
//   and 0 for the others (phrase_sends), and the effect row's amount words carry them to the send route.
#define PH_P_ZONE 0
#define PH_P_RATE 1
#define PH_P_SYNC 2
#define PH_P_RATIO 3
#define PH_P_FLIP 4
#define PH_P_HOLD 5
#define PH_P_RPTS 6
#define PH_P_COLR 7
#define PH_P_HALO 8
#define PH_P_SKEW 9
#define PH_P_MIX 10
#define PH_P_INPT 11
#define PH_P_XFAD 12
#define PH_P_TRKS 13
#define PH_PARAM_COUNT 14
#define PH_TRACKS 16
#define PH_TRACKS_ALL 0xffff
#define PH_SEND_FULL 127
#define PH_RATE_MAX 1016
#define PH_RATE_COARSE 8
#define PH_RPTS_UNITY 96
// SYNC, after the Mimeophon: the beat (a quarter note at the pattern's tempo) is the 1/1 of the zone Z0 whose centre (the
// geometric mean of its free range, sqrt(low * high)) lies nearest it in log terms; each zone above doubles the 1/1, each
// below halves it. RATIO picks one of PH_SYNC_RATIOS ratios of it, shortest first: 1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 1/1 (index
// PH_SYNC_UNITY), 5/4, 4/3, 3/2, 5/3, 7/4, 2 (the provider's table, and the page's texts). The time is at most PH_SYNC_MAX
// frames, the high end of the top zone. The beat lies in zone z (0..7) when it is at least PH_SYNC_EDGE_z frames and less
// than PH_SYNC_EDGE_(z+1): the geometric means of two neighbouring centres, in frames to the nearest (from the engine's
// zone table of free ranges; the engine's rig test checks them against it).
#define PH_SYNC_RATIOS 13
#define PH_SYNC_UNITY 6
#define PH_SYNC_MAX 513600
#define PH_SYNC_EDGE_1 321
#define PH_SYNC_EDGE_2 1263
#define PH_SYNC_EDGE_3 4044
#define PH_SYNC_EDGE_4 11372
#define PH_SYNC_EDGE_5 32199
#define PH_SYNC_EDGE_6 84219
#define PH_SYNC_EDGE_7 181139
