// Experimental exclusive phrase-FX capability (DTII 1.17 first); the layout's version is PH_VERSION.
// Include this in the engine's SHARC entry source. C uses dk/phrase.h;
// dk_phrase.inc alone contains the shared numeric layout without imports.
#include "dk_phrase.inc"

// Read-only 64-word coherent control mailbox from core/phrase-control.
// Importing this makes one FX (no voices) the exclusive phrase effect.
// Loading fails if the control provider is absent or another owner
// is loaded. Consume only valid, enabled frames for this owner generation.
// The input is the effect framework's (the route the record declares); emit only the wet signal.
.EXTERN _phrase_control_info;

// 32-word engine status mailbox from core/phrase-control. Publish the ABI
// magic/version, owner generation and matching HEARTBEAT/SEQ_END. The engine
// owns every status word.
.EXTERN _phrase_status;

// Read-only two-bank lease descriptor from core/loop-storage, separate from
// the legacy loop lease. Validate magic/version, both bank extents and frame
// capacity before access; do not assume a fixed address or bank size. PCM is
// interleaved stereo float32 at 48 kHz. Initialize frames before reading them.
.EXTERN _phrase_storage_info;
