// One command snapshot in the idle file-transfer packet, echoed until acknowledged.
#define RT_MAGIC 0x524c5431
#define RT_FOCUS 0
#define RT_RECORD 1
#define RT_FINISH 2
#define RT_PLAY 3
#define RT_UNDO 4
#define RT_ALT 5
#define RT_AUD 6
#define RT_KEEP 7
#define RT_EXIT 8
#define RT_CLEAR 9
#define RT_PREVIEW 10
// Shared between the native recorder and a stereo machine voice.
// Each track has 16 words. SLOT is the audible MAIN or alternate slot.
#define RT_LENGTH 0
#define RT_POS 1
#define RT_SLOT 2
#define RT_PLAYING 3
#define RT_DIRTY 4
#define RT_UNDONE 5
#define RT_AUDITION 6
#define RT_BEFORE_AUD 7
#define RT_REVISION 8
#define RT_PREVIEW_END 9
#define RT_TRIG 10
// Manual transport requests a stock track gate until note-on acknowledges it.
#define RT_LAUNCH 11
#define RT_UNDOABLE 12
#define RT_LANES 13
#define RT_SOURCE 14
#define RT_ARM_PLAY 15

// Tape ABI. Keep the original row layout for previously packed track voices.
#define RT_SETTINGS 11
#define RT_SHARE 12
#define RT_VIEW 13
#define RT_TRANSPORT_STOP 14
#define RT_WRITE 15
#define RT_SEEK_READ 16
#define RT_SEEK_WRITE 17
#define RT_HOME_READ 18
#define T_STRIDE_SHIFT 9
#define T_ACTIVE 0
#define T_RATE 1
#define T_FRAC 2
#define T_SPACING 3
#define T_GAIN 4
#define T_FEEDBACK 5
#define T_MIX 6
#define T_GRID 7
#define T_LAST_L 8
#define T_LAST_R 9
#define T_RESET 10
#define T_MONITOR 11
#define T_TAIL_BLOCK 14
#define T_STOPPED 15
// Extra reader offset, in frames modulo this tape length.
#define T_SLIP 16
#define T_MOD_TYPE 17
#define T_MOD_PRIMED 18
#define T_EFF_RATE 19
#define T_EFF_SPACING 20
#define T_EFF_FEEDBACK 21
#define T_EFF_MIX 22
#define T_EFF_GAIN 23
#define T_EFF_POSITION 24
#define T_READ_OFFSET 25
#define T_LEFT 32
#define T_RIGHT 64

// Recorder status error codes.
#define RL_RAM_FULL 1
#define RL_LANES_FULL 2
#define RL_EMPTY 3
#define RL_BUSY 4
#define RL_CHOOSE_LEN 6
#define RL_NOT_READY 7
