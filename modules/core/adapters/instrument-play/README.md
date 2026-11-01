# Persistent instrument playback adapter

Development-only DTII OS 1.17 adapter for one admitted V2 instance on track 1
(the zero-based DSP track 0). It is not in a default patch set. There is no
hardware release approval. The development policy admits one instance with a
528-byte state ceiling and declared 100000/1000 process/reset ceilings.
Instruction-test limits are not a measured hardware cycle budget.

The early-frame transport callbacks copy normalized events into an owned input
buffer. The admitted process callback runs once per frame; the raw stock-carrier
entry copies its output without advancing state. A separate optional voice table
keeps legacy registrations first and never shadows a stock type. The adapter
retains a silent dispatch row after a fault rather than falling into a stock
sample renderer for that type.

Replacement reset calls the admitted reset entry through a protected wrapper.
CANOPY retains oscillator phase while releasing its voices over at most 64 samples;
configuration waits for that tail before accepting new ownership. CREATE, RECOVER
and UNLOAD still clear CANOPY state. Inactive tracks acknowledge transport resets.

Instruction-level checks are in `tests/patchkit/test_instrument_play.py`, with
stock dispatch coverage in `tests/patchkit/test_dsp_voice.py`. The separate integrated
acceptance now passes uploaded V2 admission, stock selection, explicit routing,
final stereo continuity, pedal, velocity and fault retirement. Neither kind of
test establishes a hardware CPU budget.

The normal-file loader publishes the assigned type only after CREATE. Because
that happens after the stock machine chooser is constructed, a narrowly scoped
hook remembers the persistent view and refreshes its items once on the UI task
when the admitted type appears. The firmware module set remains boot-only. The
KEYBOARD setup menu starts disabled and exposes USB, DIN and channel 1-16.
The corresponding stock MIDI receive ports must also be enabled.
Sound changes revoke ownership before in-place writes; committed pattern changes
may refresh the same admitted carrier. Route edits preserve key tombstones.
Version 3 replies carry row-zero phase and allocation generation. A process fault
closes admission and requests acknowledged DETACH before releasing DSP storage;
published CF code/metadata remain retained until reboot for existing readers.
The adapter has its own UI tick and does not take Overloop's control callback.

`tests/regression/runtime/canopy-stage1.test.ts` is the integrated acceptance.
Prepare `out/patch/canopy-stage1.syx` from `core/instrument-play,core/digikit-menu`
and `out/patch/canopy-dt2.dkm` from `digikit/canopy` before running the dedicated
`canopy-stage1` Vitest project. It requires the preflighted coupled DT2 daemon
and sample card. No firmware build in this document authorizes a device flash.
