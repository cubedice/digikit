// The engine without HALO, for the test that HALO at 0 changes nothing: this stands in for src/dsp/halo.s.
.SECTION/PM seg_pmco;
.GLOBAL _prism_halo;
_prism_halo:
    RTS;
