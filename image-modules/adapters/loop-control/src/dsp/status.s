// The loop effect's status mailbox (dk_loop.inc), one writer: the engine. _loop_ui_status (16 words) is returned to the
// ColdFire by core/dsp-link on DTII. The tap is core/loop-storage's (both effects of the loop require that provider).
.SECTION/PM seg_pmco;
    NOP;
.PKDATA _loop_ui_status, 64;
