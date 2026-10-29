// Experimental core/loop-controls import: one immutable DSP word,
// _loop_controls_info = 0x4c435431. Its presence guarantees the image contains
// the Overloop destination-kit handoff adapter. Required by Overloop 0.3+.
// A missing import rejects module loading instead of silently losing handoff.
#define DK_LOOP_CONTROLS_MAGIC 0x4c435431
.EXTERN _loop_controls_info;
