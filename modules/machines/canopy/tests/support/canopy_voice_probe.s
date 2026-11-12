// Test-only raw voice adapter. This exposes CANOPY's pre-AMP output through
// core/dsp-voice without pretending to arm the stock carrier or implement V2.
#include "../../src/dsp/voice.s"
#include "../../src/dsp/fm.s"
#include "../../src/dsp/fm-tables.s"
#include "../../src/dsp/wavetable.s"
#include "../../src/dsp/wavetables.s"
#include "../../src/dsp/tuning.s"
#include "../../src/dsp/events.s"
#include "../../src/dsp/instrument.s"
#include "../../../../core/adapters/midi-events/src/dsp/carrier.s"
#include "../../../../core/adapters/midi-events/src/dsp/envelope.s"
#include "../../../../core/adapters/midi-events/src/dsp/workspace.s"
#include "../../../../core/adapters/midi-events/src/dsp/carrier-frame.s"
#include "../../../../core/adapters/midi-events/src/dsp/velocity.s"
#include "../../../../core/adapters/midi-events/src/dsp/velocity-entry.s"
#include "../../src/dsp/carrier-prepare.s"
#include "../../../../core/adapters/midi-events/src/dsp/block.s"
#include "../../src/dsp/render.s"
.SECTION/PM seg_pmco;
.GLOBAL _mi_workspace_next;
.GLOBAL _mi_velocity_next;
_mi_velocity_next:
    RTS;
_mi_workspace_next:
    RTS;
.GLOBAL _mi_receive;
.GLOBAL _mi_reply_build;
.GLOBAL _instrument_registry_block;
_instrument_registry_block:
    RTS; // registry admission has a separate production-adapter fixture
_mi_receive:
    RTS; // packet behavior has separate receiver/consumer integration tests
_mi_reply_build:
    RTS;
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
