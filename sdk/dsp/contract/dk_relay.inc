// _relay_pool: R4 command, R8 argument, R12 frame index. Single DSP owner.
// R0 = -1 on refusal. READ_SPAN returns a byte address (high bit set is valid)
// and R1 = consecutive frames. PCM16 stereo is one packed word per frame.
#define RLY_RESET 0
#define RLY_AVAILABLE 1
#define RLY_BEGIN 2
#define RLY_APPEND 3
#define RLY_READ_SPAN 4
#define RLY_COMMIT 5
#define RLY_ABORT 6
#define RLY_CLEAR 7
#define RLY_SWAP 8
#define RLY_PIN 9
#define RLY_UNPIN 10
#define RLY_LENGTH 11
#define RLY_REWRITE 12
#define RLY_RETARGET 13
// Acquire an uninitialized write span. Fill it, then publish 1..R1 frames.
#define RLY_ACQUIRE 14
#define RLY_PUBLISH 15
#define RLY_FINISH_DUB 16
#define RLY_PINNED 7
#define RLY_WRITER 8
// Four ranges of 16 track slots. IDs below PINNED are reserved.
#define RLY_TRACK_MAIN 9
#define RLY_TRACK_T1 25
#define RLY_TRACK_T2 41
#define RLY_TRACK_UNDO 57
#define RLY_SLOT_COUNT 73
#define RLY_PAGE_COUNT 511
#define RLY_FRAMES_PER_PAGE 16384
// Prepare the bounded writer for random access. R8 is a source slot or -1 for silence.
#define RLY_TAPE 19
// Materialize one untouched group of a punch to release its backing revision.
#define RLY_TIDY 20
// Share a completed MAIN between two track roots; the next punch copies on write.
#define RLY_SHARE 21
