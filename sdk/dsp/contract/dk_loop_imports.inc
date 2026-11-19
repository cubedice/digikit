// The DSP imports of the loop effect from core/loop-control (DTII OS 1.17). Include this in the engine's entry source;
// dk_loop.inc alone holds the numeric layout, which C shares through <dk/loop.h>.
#include "dk_loop.inc"

// 16-word status mailbox (LS_*): the engine writes it after every block, core/dsp-link returns it to the ColdFire.
.EXTERN _loop_ui_status;

// 68-word tap (LT_*): the loop times the crossfader as applied, one block, for an effect that takes the loop as its
// input. The engine is its only writer. An image without core/loop-control has neither symbol: a module that imports
// them is refused there.
.EXTERN _loop_tap;
