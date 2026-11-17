# Phrase FX controls

DTII1.17 provider for one dynamically loaded phrase effect. The runtime recognizes
the `_phrase_control_info` import, validates one effect and no voices, requires the
route provider, and reserves the capability before it publishes any DSP row.
Ordinary effects retain the normal route and MIX path. An owner uses internal
route2 and fixed framework MIX1; FADE belongs to the phrase router.

The public DSP imports are declared in `sdk/dsp/contract/dk_phrase_imports.inc`. The
versioned layout is `sdk/dsp/contract/dk_phrase.inc`, shared by C through
`dk/phrase.h`. The control mailbox is64 words; status is32 words. All offsets in
that header are bytes. UI values and next input selection are kept in runtime
settings under `phrase/<module id>`. REC and FADE start at zero, and audio is
volatile. No new data is written into Elektron kits, sounds or projects.

CF writes the64-word control packet into DTII's frame suffix at0x8b4. The
position, instrument and note-lock suffixes retain their positions. A code
transfer has41 payload words when this provider is present. A per-frame
sequence, matching end sequence and checksum gate atomic DSP publication.
Invalid/repeated frames clear the control capability and let routing fail open.

The existing loop telemetry carrier (rows17–20 and29–31, words+0x44/+0x48)
is explicitly multiplexed: one of four frames keeps legacy loop status; three
carry a latched32-word phrase snapshot tagged with a page and sequence. CF
assembles it in the audio ISR, so UI-task scheduling cannot lose individual
pages. Diagnostic rows1–16, instrument rows21–28 and the ACK are unchanged.
The UI copies the completed snapshot with interrupts masked briefly. No audio
RAM scanning, allocation, drawing or settings writes occur in the ISR.

Beat phase follows the stock clock (21600000ticks/quarter); stopped NOW advances
from the selected stock tempo. The master-step phase resets its fractional
position on committed nominal boundaries using their exact master-scale period.
Timing is resolved at the32-frame audio-block quantum. The narrowly identified
START precommit interval retains the previous valid stopped grid for at most
64 blocks. Only the committed next generation with the same pattern and scale
receives the retained START reason. This preserves an armed numbered cue;
other transport/pattern changes invalidate pending history. The held phrase
is owned by the DSP engine and remains available.

Validation: the module's host harness exercises actual CF control implementation,
including persistence, zero-gain membership, armed selection locks, held-versus-
next isolation, status fragment assembly, stale fallback, stopped tempo and
scaled master boundaries. DSP compilation re-decodes the actual instructions.
