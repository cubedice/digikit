// Overloop's engine: the constants that can be tuned and the layout of its state. Shared by the SHARC sources and read
// by the rig tests. Decimal values only.

// Frames of the crossfade at the loop's seam (its end into the frames before its start): 10 ms. A loop shorter than
// four times this uses a quarter of its length.
#define OL_SEAM 480
// Frames of the declick when the loop starts again at a START (the engine's weight of it is 1/256 a frame).
#define OL_JUMP_FRAMES 256
// The smallest buffers the engine works with, in frames.
#define OL_MIN_FRAMES 4096
// The master stage of DT2 1.17 multiplies its sum by this (+10 dB, 3.1623: the literal 'R4 = 0x404a6320' of the master
// function, offset 0x1ad) before its clip and the volume ramp. A send effect's output that the engine adds (the wet mailbox)
// gets this and the ramp; the tap is divided by both.
#define OL_MASTER_BOOST 0x404a6320
// The tap is divided by at most 1024 (a master gain below 1/1024 counts as 1/1024).
#define OL_INV_FLOOR 0x3a800000
// A sample of the output with the send effect's output added stays within +-7.9 (the framework's bound is 8).
#define OL_WET_BOUND 0x40fccccd
#define OL_WET_FLOOR 0xc0fccccd

// The state, in words. It is zero when the adapter resets it.
#define OL_INIT 0
// frames processed so far (wraps)
#define OL_T 1
// frames of each buffer, and their byte addresses (the left plane; the right plane follows it)
#define OL_N 2
#define OL_BUF0 3
#define OL_BUF1 4
// the rolling record: which buffer (0 or 1), its next frame, the frames it holds before that one (at most N)
#define OL_ROLL 5
#define OL_W 6
#define OL_VALID 7
// the loop: held or not, its buffer, the frame of its first sample, its frames, the phase the next sample reads
#define OL_LOOP 8
#define OL_LBUF 9
#define OL_BASE 10
#define OL_L 11
#define OL_PHASE 12
// phases below OL_SILENT were never recorded and are silence; OL_PRE: the frames before the loop's start were recorded
#define OL_SILENT 13
#define OL_PRE 14
// the seam's frames and their reciprocal (float)
#define OL_X 15
#define OL_XINV 16
// steps of the pattern the loop came from, cut or not, loops taken
#define OL_STEPS 17
#define OL_CUT 18
#define OL_COUNT 19
// the crossfader as applied (float); held at LOOP until the home has seen the loop; the loop is being dropped
#define OL_GAIN 20
#define OL_HELD 21
#define OL_DROP 22
// the declick after a restart: frames left, and the phase the old head reads
#define OL_JUMP 23
#define OL_OLD 24
// the controls: armed, and the counters last read (OL_CTL: 1 once they were read)
#define OL_ARMED 25
#define OL_CTL 26
#define OL_ARM_SEQ 27
#define OL_DROP_SEQ 28
// the position record of the last block was valid: its send count and its boundary count
#define OL_HAVE 29
#define OL_TX 30
#define OL_BSEQ 31
// a run of boundaries is followed: its generation (OL_GEN_OK: one was seen), pattern, length in steps, last step
#define OL_RUN 32
#define OL_GEN_OK 33
#define OL_GEN 34
#define OL_PAT 35
#define OL_LEN 36
#define OL_STEP 37
// OL_T at the run's first boundary, and the boundaries of the run
#define OL_RUN_T 38
#define OL_RUN_N 39
// OL_CYC: OL_CYC_T is a boundary of step 0 and every step since came in order. OL_CYC_LEN: frames of the last whole
// cycle of this pattern (0: none yet). OL_NEXT: the frames an armed change would keep now (0: not known).
#define OL_CYC 40
#define OL_CYC_T 41
#define OL_CYC_LEN 42
#define OL_NEXT 43
// The frames heard before the rolling record began lie in the other buffer (the loop's), up to its frame OL_FILL_END:
// OL_FILL_MAX of them. OL_FILL of them have been copied to the frames before the record's first, the newest first.
#define OL_FILL 44
#define OL_FILL_MAX 45
#define OL_FILL_END 46
// this block: the input and output blocks, the block count, the parameter words, the crossfader's target (float)
#define OL_IN 47
#define OL_OUT 48
#define OL_BLOCK 49
#define OL_PARAMS 50
#define OL_TARGET 51
#define OL_TAP_SET 52
// The wet mailbox: LW_SEQ at the last block, 1 once it was read, 1 when this block adds it; the tap's factor (float).
#define OL_WET_SEEN 53
#define OL_WET_INIT 54
#define OL_WET_OK 55
#define OL_INV 56
#define OL_STATE_WORDS 60
#define OL_STATE_BYTES 240
