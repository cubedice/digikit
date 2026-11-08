// This fixture accepts only CPS1 fixed-sine settings and owns 256-byte banks.
// It shares the production voice/event kernel, but has no V2 instance, CPS9
// controls, wavetable state or factory assets. Keep the new timbre extension
// inactive: its sample callback preserves F0 and its event state stays null.
// Production CPS9 tests link the actual wavetable renderer instead.
.SECTION/PM seg_pmco;
.GLOBAL _wt_begin_slot;
.GLOBAL _wt_tick;
_wt_begin_slot:
    RTS;
_wt_tick:
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _wt_state;
.VAR _wt_state = 0;
