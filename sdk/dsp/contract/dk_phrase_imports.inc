// Experimental exclusive phrase-FX capability, ABI v1 (DTII 1.17 first).
// Include this in the engine's SHARC entry source. C uses dk/phrase.h;
// dk_phrase.inc alone contains the shared numeric layout without imports.
#include "dk_phrase.inc"

// Read-only 64-word coherent control mailbox from core/phrase-control.
// Importing this opts one FX (no voices) into the exclusive phrase route.
// Loading fails if the control/router providers are absent or another owner
// is loaded. Consume only valid, enabled frames for this owner generation.
// The normal FX input is selected, pre-fade stereo; emit only the wet signal.
.EXTERN _phrase_control_info;

// 32-word engine status mailbox from core/phrase-control. Publish the ABI
// magic/version, owner generation and matching HEARTBEAT/SEQ_END. The engine
// owns the other status fields except FADE and ROUTE_FAULT: the router owns
// those two words and the engine must never overwrite them.
.EXTERN _phrase_status;

// Read-only two-bank lease descriptor from core/loop-storage, separate from
// the legacy loop lease. Validate magic/version, both bank extents and frame
// capacity before access; do not assume a fixed address or bank size. PCM is
// interleaved stereo float32 at 48 kHz. Initialize frames before publishing
// them, and retain each held bank until every grain reading it has retired.
.EXTERN _phrase_storage_info;
