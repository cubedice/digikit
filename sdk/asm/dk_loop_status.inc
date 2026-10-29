// Experimental core/loop-display return mailbox, required by Overloop 0.4+.
// One writer, 16 u32 words (64 bytes), owned by the framework provider.
// Publish after processing a block; dsp-link reads it on the same DSP core.
// Fields: magic, heartbeat, mode, valid_frames, written_frames, phase,
// applied_gain_float32, armed_start, goal_steps, progress_steps, error,
// recorded_steps, window_offset_frames, window_length_frames, START, LENGTH.
// Mode: 0 idle, 1 recording, 2 frozen, 3 armed. Error: 0 none, 1 timing, 3 full.
// Missing import rejects loading. This interface is DTII OS 1.17 only.
#define DK_LOOP_STATUS_MAGIC 0x4f564c31
.EXTERN _loop_ui_status;
