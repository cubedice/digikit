# Persistent instrument playback adapter

Development-only DTII OS 1.17 adapter for one admitted V2 instance on track 1
(the zero-based DSP track 0). It is not in a default patch set. There is no
hardware release approval. The development policy admits one instance with a
544-byte state ceiling and declared 100000/1000 process/reset ceilings.
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
Prepare `out/patch/canopy-stage3.syx` from `core/instrument-play,core/digikit-menu`
and `out/patch/canopy-dt2.dkm` from `digikit/canopy` before running the dedicated
`canopy-stage1` Vitest project. It requires the preflighted coupled DT2 daemon
and sample card. No firmware build in this document authorizes a device flash.


## Keyboard modes and shape memory

Stage 2 keeps the sine renderer and adds a bounded ColdFire musical mapper to
`core/instrument-play`. Open DIGIKIT > KEYBOARD and explicitly enable routing.
PLAY admits four foreground notes, preserving pitch, octave, velocity and token
ownership; the existing DSP supplies the two bounded release tails. Stolen held
keys stay in the physical ledger until their own releases and never reappear.

ROOT + TYPE uses last-pressed physical-root priority, falling back to the most
recent still-held root. The selector octave defaults to C1 (MIDI 24-35).
C/D/E/F/G/A/B choose major/sus2/minor/sus4/dom7/min7/maj7; the black keys choose
major6/minor6/add9/diminished/custom. Selectors are silent. SELECT CH = 0 uses the
selector octave on the root channel; 1-16 reserves that separate channel for
selectors. Choosing the root channel as selector channel falls back to SAME so
roles never overlap. USB/DIN and the root channel remain explicit setup choices.
CLOSE, OPEN, INVERSION 1 and INVERSION 2 are available. A chord outside MIDI
0-127 is rejected as a whole. A missing custom shape produces silence and an
EMPTY status. Live chord/voicing edits retain the tokens of unchanged pitches.

LATCH retains a released phrase. After all physical keys are up, a new press
starts its replacement and subsequent rolled notes join it. Replacement clears
old pedal membership. Turning latch off retains only physical/pedal-owned notes.
CC64, CC120, CC121, CC123 and per-route pitch bend retain their defined roles.
The mapper handles pedal ownership; bend is forwarded exactly once to the DSP.
Unsupported controllers still follow the existing stock fallthrough policy.

Choose one of eight SHAPE SLOT values, then LEARN TO ROOT. Capture auditions in
isolated PLAY with latch disabled, accumulating up to four unique pitches even
after release. The fifth distinct pitch reports FULL. SAVE checks the displayed
session/revision, rejects an empty or changed capture, and stores ascending
intervals relative to its lowest note. CANCEL or NO preserves the prior shape.
Both exits restore the previous mode/latch and invalidate held-key ownership.
For example C-G-B-E above C saves 0,7,11,16; root D plays D-A-C#-F#.

All eight templates occupy one versioned 48-byte `canopy/shapes/v1` record in the
shared settings store. Validation covers length, version, checksum, reserved
bytes, counts, range and ordering before applying any template. RAM save is
immediate; a UI-task tick performs the durable write without holding the MIDI
critical section. Failure retains RAM and dirty state; RETRY SHAPE SAVE retries.
Keyboard enable, route, latch, mode and voicing start at defaults after reboot;
only templates persist in this stage. Presets and the named SRC pages are Stage 5.

### Stage 5 preset storage checkpoint

KEYBOARD > PRESETS now provides four explicit sound slots. SAVE CURRENT SOUND
queues a write on the UI task; LOAD SAVED SOUND validates the complete record
and resets the instrument before applying it. Fresh keyboard presses are required
after loading. Failed saves leave the working sound untouched and report NOT SAVED;
repeat SAVE to retry. Loading uses the last confirmed saved copy for this boot.
The shared store may independently retry a failed write, but the preset UI does
not claim confirmation until its own successful save.

Each version-1 `canopy/preset/1` through `canopy/preset/4` value is 64 bytes.
It includes stable logical control IDs 0-15, family, keyboard mode, local BPM,
clock source, and the custom shape by value. It excludes routing, native TUNE/
LEVEL, held keys, pedal membership, tokens, phase and pointers. Loading does not
overwrite any of the eight learned library slots. Selecting a different shape
slot or completing LEARN releases the preset shape override.

The format uses big-endian 16-bit controls and BPM, canonical zero padding and
a 32-bit FNV-1a checksum. Length, schema, checksum, every range, shape ordering
and unused bytes are checked before applying anything. SOURCE currently accepts
keyboard only; no sequencer owner is implied. Swing retains the existing CPS5
extra-delay percentage. Four presets plus the shape library occupy 390 bytes
including settings-entry overhead, within the requested 1536-byte quota.
The shared settings store checks its remaining capacity before inserting a value.

This checkpoint does not implement the named SRC pages, display telemetry or
native Sound/pattern persistence. Those Stage 5 acceptance tasks remain open.

SAVE ROUTING stores enable, USB/DIN choices, root channel, selector channel and
selector octave separately in a 16-byte `canopy/routes/v1` record. It snapshots
the requested setup when pressed and writes on the UI task. Boot validates the
entire record before restoring anything, then waits for a registered instrument
generation. The existing healthy/prepared/configured handshake still gates
actual MIDI route activation. An explicit route edit cancels deferred boot
restoration. Invalid records leave routing disabled at its boot defaults.
Failed saves keep working settings and offer retry through SAVE ROUTING.
Including this entry, all CANOPY settings occupy 424 bytes with entry overhead.

PANIC releases sound and retains held-key tombstones. RECOVER KEYBOARD separately
asks for all keys and pedals to be released before clearing capacity quarantine
and advancing the MIDI epoch. It cannot restore a failed boot/session guard or
a retired DSP callback; those require a real restart. Route/mode changes also
retain tombstones, so an old release cannot kill a new same-pitch press.

The optional mapper consumes at most eight physical note events or one controller
at a time. Selectors within a batch precede roots. Audible diffs contain at most
eight commands; original FIFO retirement counts are separate from output counts.
Retries retain the exact mapped packet, never re-run the musical transformation,
and only matching acknowledgements retire physical events. Silent selector-only
batches retire locally. Pending UI diffs are sent before new controller batches.
Idle calls do no pool scan. No allocation, card access or UI calls run in this path.

Host scenarios execute the production C engine, transport and UI callbacks in
`tests/regression/host/canopy-keyboard.test.ts`; the combined Stage 1/2 acceptance
remains in `tests/regression/runtime/canopy-stage1.test.ts` and uses the
artifact names above. Instruction tests measure bounded CF work separately from
SHARC rendering. These checks do not establish hardware timing or release approval.


## Pad sound setup (Stage 3)

The final five KEYBOARD rows control the pad while notes remain held:

| Row | Range and meaning | Default |
| --- | --- | --- |
| REED (OFF=VELVET) | Velvet has a rounded odd-harmonic spectrum; Reed adds even and odd harmonics | Velvet |
| COLOR | 0 is sine; 127 is the full family spectrum | 64 |
| DRIFT | 0 is exact tuning; 127 adds deterministic slow motion up to five cents, less at high pitches | 0 |
| ATTACK | 0..127 maps to `10 + 14*value` milliseconds | 5 = 80 ms |
| RELEASE | 0..127 maps to `40 + 64*value` milliseconds from full level | 15 = 1000 ms |

These are setup controls until the Stage 5 SRC pages/presets. They remain at the
current setting across compatible pattern switches and return to defaults on
reboot. They are not Sound/project parameters. Stock filter, level, pan and FX
continue downstream of the instrument. Use a sustained stock AMP shape for
independent keyboard notes; stock AMP remains a shared master shape.

CPS3 uses six integer words (version 3, family, color, drift, attack, release).
The existing checked/acknowledged configuration transport commits all six at
once. UI changes coalesce while a prior packet is pending; updates retain tokens,
phase and envelopes. Invalid configurations cannot partially update sound.
Family and color are smoothed over 20 ms with sample interpolation. Drift updates
at block rate, with independent deterministic instance phase and opposite motion
for alternating tokens. Normal release never extends the 64-sample panic ramp.

CANOPY uses one rotating oscillator per tone group and derives only the second
and third harmonics. Frequency-dependent weights fade harmonics before Nyquist,
including bent/drifted pitches. Four foreground groups and two shared tails have
fixed headroom below 0.75 at full velocity; note count never normalizes gain.
The 544-byte instance and 2560-byte shared pitch/envelope tables fit the design's
4 KiB/64 KiB targets. Single-instance admission remains in force. Instruction
bounds are not a silicon CPU or Overloop coexistence qualification.

The packaged Stage 3 acceptance uses `out/patch/canopy-stage3.syx` and
`out/patch/canopy-dt2.dkm`, in the `canopy-stage3` Vitest project. Earlier Stage 1/2
acceptance can run against the Stage 3 image as well. No physical device is
flashed by these tests.

Use the matching Stage 3 firmware and CANOPY DKM together. The older sine-only
DKM cannot consume the new six-word sound configuration.
## Optional process status output

Descriptor flag bit 0 opts an instrument into the V5/96-byte borrowed input.
The first 88 bytes retain the V4 layout. Words 22/23 provide the adapter-owned
status buffer and its exact capacity of 13 words. The buffer contains ready,
then twelve normalized UI payload words; publish ready=1 only after filling the
payload. It is disjoint from input, events, controls, audio output and state.
Retain no pointer beyond the call. This capability does not authorize a module
to inspect other modules or firmware storage.

Admission preserves the flag through descriptor decoding, row mapping and
process installation. Unknown bits are rejected. A matching READY row and
owner/generation are still required; unflagged modules retain V3/V4 inputs.
The dispatcher bounds the V5 input and status storage in L2, requires alignment
and exact capacity, clears readiness before invocation and clears it again on
negative or invalid callback results. The playback owner also clears readiness
each block before checking admission/session, preventing reuse after revocation.

The mailbox connects to the authenticated alternating status-frame transport.
CANOPY's descriptor remains unflagged until its actual-state producer and
applied-control identity are implemented. This interface alone does not supply
screen telemetry or complete SRC acceptance.
