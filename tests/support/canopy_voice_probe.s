// Test-only raw voice adapter. This exposes CANOPY's pre-AMP output through
// core/dsp-voice without pretending to arm the stock carrier or implement V2.
#include "../../modules/machines/canopy/src/dsp/voice.s"
#include "../../modules/machines/canopy/src/dsp/tuning.s"
#include "../../modules/machines/canopy/src/dsp/instrument.s"
#include "../../modules/core/adapters/midi-events/src/dsp/carrier.s"
#include "../../modules/core/adapters/midi-events/src/dsp/envelope.s"
.SECTION/PM seg_pmco;
.GLOBAL _mi_envelope_next;
_mi_envelope_next:
    RTS; // boundary test stops before the rest of the real stock envelope
.GLOBAL _cp_voice_probe;
.EXTERN _cp_render_track;
_cp_voice_probe:
    R4 = R2; // raw wrapper supplies the track separately from the voice record
    CALL _cp_render_track;
    DM(_cp_probe_active) = R0;
    R0 = 0;
    JUMP (M13, I13);
.SECTION/DM seg_dmda;
.GLOBAL _cp_probe_active;
.GLOBAL _mi_epoch;
.VAR _cp_probe_active = 0;
.VAR _mi_epoch = 0; // lifecycle callbacks are linked but not invoked by this probe
