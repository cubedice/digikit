// Internal raw dsp-voice entry, not a public V2 descriptor or release machine.
// The adapter calls once on the even voice and copies its audio to the odd voice.
// Publish after rendering, before either stock envelope reads the shared pair.
// Readiness admits ownership; previous ACTIVE retains it through reset tails
// and the final release edge. Stock ACTIVE/sample records are not touched here.
.SECTION/PM seg_pmco;
.GLOBAL _cp_voice_render;
.EXTERN _cp_render_track;
.EXTERN _cp_ready;
.EXTERN _mi_carrier_rendered;
_cp_voice_render:
    DM(_cp_render_index) = R2;
    R4 = R2;
    CALL _cp_render_track;
    R8 = R0;
    R4 = DM(_cp_render_index);
    R0 = LSHIFT R4 BY 2;
    R1 = _cp_ready;
    R1 = R1 + R0;
    I0 = R1;
    R10 = DM(0, I0);
    CALL _mi_carrier_rendered;
    R0 = 0;
    JUMP (M13, I13);
.PKDATA _cp_render_index, 4;
