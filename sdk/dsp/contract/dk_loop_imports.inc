// The DSP imports of the loop effect (DTII OS 1.17). Include this in the engine's entry source; dk_loop.inc alone holds the
// numeric layout, which C shares through <dk/loop.h>.
#include "dk_loop.inc"

// 16-word status mailbox (LS_*) of core/loop-control: the engine writes it after every block, core/dsp-link returns it to
// the ColdFire. An image without core/loop-control does not have it: a module that imports it is refused there.
.EXTERN _loop_ui_status;

// 68-word tap (LT_*) and 68-word wet mailbox (LW_*) of core/loop-storage, the provider both effects of the loop already
// require. The engine is the tap's only writer, and the writer of LW_TAKEN.
.EXTERN _loop_tap;
.EXTERN _loop_wet;

// The master stage's volume ramp (DTII 1.17, anchors of devices/fw/dsp/master.toml through core/dyndsp's import table): the
// gain the master function gives the first sample of this block, and its step a sample (the gain before sample k is the
// start plus k steps). Read-only; the master function writes them once a block, before the effects of the master chain run.
.EXTERN _fw_master_gain;
.EXTERN _fw_master_step;
