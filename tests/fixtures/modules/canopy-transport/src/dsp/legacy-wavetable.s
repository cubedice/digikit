// This fixture accepts only CPS1 fixed-sine settings and owns 256-byte banks.
// It shares the production voice/event kernel, but has no V2 instance, CPS9
// controls, wavetable/FM state or factory assets. Keep the timbre extensions
// inactive: wavetable rendering stays disabled (_wt_enabled = 0, so the block
// renderer is never called) and event state stays null; FM dispatch remains
// disabled. Production tests link actual renderers.
.SECTION/PM seg_pmco;
.GLOBAL _wt_begin_slot;
.GLOBAL _wt_render_block;
.GLOBAL _fm_render;
_wt_begin_slot:
    RTS;
_wt_render_block:
    RTS;
_fm_render:
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _wt_state;
.VAR _wt_state = 0;
.GLOBAL _wt_enabled;
.VAR _wt_enabled = 0;
.GLOBAL _fm_enabled;
.VAR _fm_enabled = 0;
.PKBSS _wt_blk, 4;
.PKBSS _wt_r6, 4;
.PKBSS _wt_r7, 4;
