// The loop effect's DSP mailboxes (dk_loop.inc), one writer each: the engine. _loop_ui_status (16 words) is returned
// to the ColdFire by core/dsp-link on DTII; _loop_tap (68 words) is the loop times the crossfader, one block, for an
// effect that takes the loop as its input.
.SECTION/PM seg_pmco;
    NOP;
.PKDATA _loop_ui_status, 64;
.PKDATA _loop_tap, 272;
