// The loop effect (Overloop): its controls, its status, its tap and the wet mailbox. Pure numeric definitions, shared by the
// control home (core/loop-control, through <dk/loop.h>), the pages (core/loop-display, core/phrase-ui), the loop effect's
// engine and the phrase effect's engine (PRISM). DTII OS 1.17 only.
//
// Controls. The home holds them outside the kit; the fx service hands them to the engine as the parameter words of the
// effect's row (it asks the home for the values instead of the kit). LP_* is the index of a raw value and of its parameter
// word; a raw value is 0 .. 127.
//   XFAD   the crossfader: 0 LIVE .. 127 LOOP (the word is a float 0 .. 1)
//   ARM    a counter: each change arms the engine, or disarms it when it is armed (the word is the count as a float)
//   DROP   a counter: each change drops the loop
//   SEEN   the low seven bits of the last loop count the home has read from the status. The engine holds the crossfader
//          at LOOP from an armed change until SEEN is its count: the home has then set XFAD to 127 itself.
#define LP_XFAD 0
#define LP_ARM 1
#define LP_DROP 2
#define LP_SEEN 3
#define LP_COUNT 4
#define LP_MAX 127
// Status: the engine publishes 16 words in _loop_ui_status after every block. core/dsp-link returns words 1 to 13 to the
// ColdFire (words 7, 14 and 15 travel as seven bits each and are 0 here).
#define LS_MAGIC_VALUE 0x4f564c31
#define LS_WORDS 16
#define LS_MAGIC 0
#define LS_HEARTBEAT 1
// LS_STATE: the bits below, and the count of loops taken from bit 8 up. An engine that starts (a load, a reset of its state, a
// lease that moved) goes on counting from the count the mailbox shows, so a loop taken after a restart has a count no reader
// has seen before.
#define LS_STATE 2
#define LS_LOOP 1
#define LS_ARMED 2
#define LS_CUT 4
#define LS_TRACK 8
#define LS_NEXT_CUT 16
#define LS_HELD 32
#define LS_COUNT_SHIFT 8
// The loop's frames (0: none), the frames an armed change would keep now (0: not known), the loop's phase in frames, the
// crossfader as applied (float).
#define LS_LOOP_FRAMES 3
#define LS_NEXT_FRAMES 4
#define LS_PHASE 5
#define LS_GAIN 6
// LS_STEPS: the steps of the pattern the loop came from, the steps of the pattern that plays (from bit 8) and its step
// (from bit 16); the last two are 0 while no pattern is followed.
#define LS_STEPS 9
#define LS_CAPACITY 10
// The tap: the loop times the crossfader as applied, one block, at the level of the mix before the master stage: divided by the
// master stage's gain after its sum at the end of the block (its +10 dB and the volume ramp; at most 1024 times). The engine
// writes it after every block; a reader in the next block uses it while the engine runs (LW_TAKEN moved since its last block)
// and LT_FLAGS has LT_AUDIBLE. 68 words, in core/loop-storage.
#define LT_MAGIC_VALUE 0x4c544150
#define LT_VERSION_VALUE 1
#define LT_WORDS 68
#define LT_MAGIC 0
#define LT_VERSION 1
#define LT_BLOCK 2
#define LT_FLAGS 3
#define LT_LEFT 4
#define LT_RIGHT 36
// LT_FLAGS: LT_AUDIBLE while a loop is mixed, and the count of loops taken from bit LT_COUNT_SHIFT up (a reader sees a new loop
// in the block after the one it was taken in).
#define LT_AUDIBLE 1
#define LT_COUNT_SHIFT 8
// The wet mailbox: a send effect's output that the loop effect adds behind its crossfader, at the master stage's gain after
// its sum, in the same block (the send effects run before the master stage, the loop effect after it). One writer a word:
// the send effect writes the head, LW_SEQ and the samples, the loop effect LW_TAKEN. LW_SEQ counts the blocks the send effect
// wrote (it moves after the samples are there); LW_TAKEN counts the blocks the loop effect finished. A reader takes a block
// when the other's count moved since its own last block. 68 words, in core/loop-storage.
#define LW_MAGIC_VALUE 0x4c574554
#define LW_VERSION_VALUE 1
#define LW_WORDS 68
#define LW_MAGIC 0
#define LW_VERSION 1
#define LW_SEQ 2
#define LW_TAKEN 3
#define LW_LEFT 4
#define LW_RIGHT 36
