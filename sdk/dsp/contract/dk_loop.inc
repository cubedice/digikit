// The loop effect (Overloop): its controls, its status and its tap. Pure numeric definitions, shared by the control home
// (core/loop-control, through <dk/loop.h>), the page (core/loop-display) and the engine. DTII OS 1.17 only.
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
// LS_STATE: the bits below, and the count of loops taken from bit 8 up.
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
// The tap: what the loop adds to the output, the loop times the crossfader as applied, one block. The engine writes it
// after every block; a reader in the next block finds LT_BLOCK one behind its own block count. 68 words.
#define LT_MAGIC_VALUE 0x4c544150
#define LT_VERSION_VALUE 1
#define LT_WORDS 68
#define LT_MAGIC 0
#define LT_VERSION 1
#define LT_BLOCK 2
#define LT_FLAGS 3
#define LT_LEFT 4
#define LT_RIGHT 36
#define LT_AUDIBLE 1
