// This fixture accepts only CPS1 fixed-sine settings and owns 256-byte banks.
// It shares the production voice/event kernel, but has no V2 instance, CPS9
// controls, wavetable/FM state or factory assets. Keep the timbre extensions
// inactive: the wavetable sample callback preserves F0 and event state stays
// null; FM dispatch remains disabled. Production tests link actual renderers.
.SECTION/PM seg_pmco;
.GLOBAL _wt_begin_slot;
.GLOBAL _wt_tick;
.GLOBAL _fm_render;
_wt_begin_slot:
    RTS;
_wt_tick:
    RTS;
_fm_render:
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _wt_state;
.VAR _wt_state = 0;
.GLOBAL _fm_enabled;
.VAR _fm_enabled = 0;
