# Persistent instrument playback adapter

Development-only DTII OS 1.17 adapter for up to four admitted V2 instances,
one per kit track whose Sound carries the instrument type (any of the sixteen
tracks). The four lowest such tracks are admitted; an instance keeps its track
while that track stays wanted, and a freed instance takes the next wanted track
with its sound settings retained. `ip_tracks[4]` publishes the bound tracks and
`ip_track` the lowest (0xffffffff when none). It is not in a default patch set.
There is no hardware release approval. The development policy admits four
instances, each with a 2976-byte state ceiling and declared 100000/1600
process/reset ceilings (400000 process in total). Instruction-test limits are
not a measured hardware cycle budget; four simultaneous worst-case instances
can exceed a DSP block (finding 121).

Instance i is DSP slot i and registry instance i. A reset with word 10 = i+1
binds the slot to the reset's track; the CF flags the selected track's instance
with bit 15 of control word 0 so its status reaches INPUT/STATUS. The UI edits
the selected track's instance; each instance keeps its own sound settings, SRC
state, status and MIDI CHANNEL (saved under `canopy/channels/v1`). A channel
already played by a lower instance is skipped and INPUT shows CHANNEL IN USE.
Learned shapes are shared.

The native UI revision uses the stock TUNE, source LEV and track LEV paths on
SOUND, with native fonts and bounded custom cells. ARP follows the pattern tempo
and remains playable from the keyboard while stopped; G/H are blank.
Hold SRC uses the firmware's momentary values state. Detailed notes,
pedal and clock state are under `DIGIKIT > MODULES > CANOPY > SETTINGS > STATUS`.
See the [accepted UI contract](../../../../docs/design/canopy-ui-review.md).

SOUND B / CHRD now selects PLAY or any generated chord directly; SOUND D / WAVE
selects Velvet, Reed, one of the five wavetables or FM. CHRD is a presentation of
the mode/chord fields; WAVE has its own view-only identity so it cannot alias
the FM algorithm field. Discrete lists accumulate four raw ticks per item without acceleration.
Mode transitions retire old note ownership; root chord changes update held notes.
Family transitions retain the held DSP voices and invalidate queued SRC edits.
The global setup entry is CANOPY, with INPUT, CHORD LEARN, MODULATION and PRESETS.
MODULATION holds scan clock/restart and the three wheel amounts. The complete
position envelope is on wavetable TONE. TUNE/LEV are exclusive to SOUND A/H;
unused cells on the other pages cannot edit stock source parameters.

The DTII 1.17 KeyboardView bridge intercepts resolved performance notes before
stock mono audition. It retains native key-up closures and uses a separate PANEL
owner through PLAY/ROOT/LEARN. Local keys do not require enabling external MIDI.
Keys play the admitted CANOPY on the selected track; stock machines and
tracks without an instance delegate to the original handlers. Leaving keyboard mode or selecting
another track cancels the
panel phrase without canceling external notes. While the sequencer live
records or a trig is held, presses on a CANOPY track go to the stock note
action and still play on CANOPY's panel path. Step recording enters notes via
the stock note-list writer instead; the adapter queues its completed entries
so quickly played chords and delayed releases retain their order. The stock
audition of a held recorded key is muted on the frame path. The other
keys held with a recorded press are stored on its step as chord-note locks
(IDs 53..55), which the mapper plays with the trig's note; live recording
commits after the key event, so the UI tick watches the track's note list for
the committed steps (findings 123 and 125).

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
acceptance for earlier CANOPY builds passed uploaded V2 admission, stock selection,
explicit routing, final stereo continuity, pedal, velocity and fault retirement.
FM compiled acceptance is tracked separately in the
[current FM handover](../../../../docs/handovers/HANDOVER-2026-11-09-canopy-fm.md).
Neither kind of test establishes a hardware CPU budget.

The normal-file loader publishes the assigned type only after CREATE. Because
that happens after the stock machine chooser is constructed, a narrowly scoped
hook remembers the persistent view and refreshes its items once on the UI task
when the admitted type appears. The firmware module set remains boot-only. The
CANOPY > INPUT setup starts disabled and exposes USB, DIN and channel 1-16.
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
`core/instrument-play`. Open DIGIKIT > CANOPY > INPUT and explicitly enable routing.
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

Latch ownership is removed. A released physical key starts its note's release
unless held by the sustain pedal; arp candidates follow the same ownership.
Control slot 12 is reserved zero. Old CPST v1-v5 records may contain latch 0/1;
decode discards that setting and subsequent saves write zero without changing
other sound controls. Malformed former latch values are still rejected.
CC1, CC64, CC120, CC121, CC123 and per-route pitch bend retain their defined roles.
The mapper handles pedal ownership; bend and wheel are forwarded once to the DSP.
CC121 also emits a zero wheel value for its USB/DIN route; CC120 preserves it.
Unsupported controllers still follow the existing stock fallthrough policy.

In CANOPY > CHORD LEARN, choose one of eight SHAPE SLOT values, then LEARN TO ROOT.
Capture auditions in isolated PLAY, accumulating up to four unique pitches even
after release. The fifth distinct pitch reports FULL. SAVE checks the displayed
session/revision, rejects an empty or changed capture, and stores ascending
intervals relative to its lowest note. CANCEL or NO preserves the prior shape.
Both exits restore the previous mode and invalidate held-key ownership.
After saving, select SOUND B / CHRD = USER to play the learned shape.
For example C-G-B-E above C saves 0,7,11,16; root D plays D-A-C#-F#.

All eight templates occupy one versioned 48-byte `canopy/shapes/v1` record in the
shared settings store. Validation covers length, version, checksum, reserved
bytes, counts, range and ordering before applying any template. RAM save is
immediate; a UI-task tick performs the durable write without holding the MIDI
critical section. Failure retains RAM and dirty state; RETRY SHAPE SAVE retries.
Learned templates and saved presets persist across reboot. Keyboard mode,
voicing and sound controls start at defaults until you load a preset. External
MIDI starts disabled unless SAVE ROUTING was used; saved routing is restored
after instrument admission, with activation still gated by the DSP handshake.

### Stage 5 preset storage checkpoint

This historical checkpoint describes the 64-byte v2 records. The current
128-byte v4 format is documented below under CPSA; the preceding CPS9 checkpoint
describes the 96-byte v3 format and wavetable controls.
The storage totals in this checkpoint apply to the earlier format.

KEYBOARD > PRESETS now provides four explicit sound slots. SAVE CURRENT SOUND
queues a write on the UI task; LOAD SAVED SOUND validates the complete record
and resets the instrument before applying it. Fresh keyboard presses are required
after loading. Failed saves leave the working sound untouched and report NOT SAVED;
repeat SAVE to retry. Loading uses the last confirmed saved copy for this boot.
The shared store may independently retry a failed write, but the preset UI does
not claim confirmation until its own successful save.

Each version-2 `canopy/preset/1` through `canopy/preset/4` value is 64 bytes.
It includes stable logical control IDs 0-15, family, keyboard mode, the legacy
BPM/clock storage positions, and the custom shape by value. Current loads and
saves normalize those retired fields to the pattern clock and a reserved BPM
placeholder; they cannot select a separate tempo. Migration alone does not mark
the sound as edited. It excludes routing, native TUNE/
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

Octave spread uses control ID 8 with a +3 bias: 0..6 represents -3..+3
additional octaves in one direction. Version-1 presets remain readable: their
1..3 total upward octaves migrate to 0..+2 spread without changing the sound.
Migration alone does not mark a loaded preset unsaved.

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


## Stock MOD integration

CANOPY's six native custom SRC slots are fixed per source family. Mirrors
26/27/28/31/32/33 map to COLOR/DRIFT/ARP MIX/STRUM/GATE/SWING for Velvet/Reed;
POSITION/SCAN DEPTH/SCAN RATE/TIMBRE ENV/WT FM DEPTH/WT FM ENVELOPE for
wavetables; and FM DEPTH/FM BALANCE/FM FEEDBACK/FM ENVELOPE/FM ATTACK/FM RELEASE
for FM. The optional `instrument_mod_param` hook supplies native destination
descriptors only for the admitted instrument type and a live generation. Other
machines retain their normal lookup. Family labels publish with the control
snapshot; the SRC knob pages remain independent of these six carrier slots.

Admission initializes the six unused Sound mirrors at their neutral `0x4000`.
The stock MOD engine produces the movement. Before each callback, the adapter
reads the completed stock frame, verifies the carrier type, and applies bounded
offsets to a scratch copy of a CPSB bank. Its revision and schema stay unchanged;
the acknowledged base bank and preset values are never edited. A neutral frame
uses the original bank directly. Scalar duplicates, the sync scan-rate range
and GRID's no-swing constraint remain valid. No new content-module ABI or
persistent configuration fields are required.

Native TUNE/LEVEL modulation remains stock. Custom Sound/project persistence,
parameter locks and arbitrary target reassignment are outside this change.

## Pad sound setup (Stage 3, historical)

The table below documents the earlier local envelope. Current CANOPY uses the
track AMP page per note through the opt-in V6 input. The earlier SOUND SETUP contained an AMP
page reminder; arp clock/BPM settings are removed, and its duplicate amplitude attack and
release controls have been removed. Legacy preset values remain stored for
compatibility with the earlier input layouts. Native AHD and ADSR snapshots use
the stock coefficient tables, with separate per-note phase and hold counters.
Only a current successfully rendered V6 block bypasses shared carrier AMP;
other instruments retain their existing envelope path. Stock filter, levels,
pan and FX remain downstream. See the current design for verification status.

The final five KEYBOARD rows control the pad while notes remain held:

| Row | Range and meaning | Default |
| --- | --- | --- |
| REED (OFF=VELVET) | Velvet has a rounded odd-harmonic spectrum; Reed adds even and odd harmonics | Velvet |
| COLOR | 0 is sine; 127 is the full family spectrum | 64 |
| DRIFT | 0 is exact tuning; 127 adds deterministic slow motion up to five cents, less at high pitches | 0 |
| ATTACK | 0..127 maps to `10 + 14*value` milliseconds | 5 = 80 ms |
| RELEASE | 0..127 maps to `40 + 64*value` milliseconds from full level | 15 = 1000 ms |

The current SOUND/ARP/TONE pages supersede this early setup layout. Native AMP
owns amplitude attack/release; the former SOUND SETUP group is removed. Four
explicit presets save the complete custom bank and an embedded learned shape.
They are not native Sound/project parameters.
Stock filter, level, pan and FX
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
The current 800-byte instance and 2560-byte shared pitch/envelope tables fit the design's
4 KiB/64 KiB targets. Single-instance admission remains in force. Instruction
bounds are not a silicon CPU or Overloop coexistence qualification.

The packaged Stage 3 acceptance uses `out/patch/canopy-stage3.syx` and
`out/patch/canopy-dt2.dkm`, in the `canopy-stage3` Vitest project. Earlier Stage 1/2
acceptance can run against the Stage 3 image as well. No physical device is
flashed by these tests.

Use matching firmware and CANOPY DKM builds together. The wavetable extension
uses CPS9 in the same 32-word bank. Header word 0 retains native TUNE and mode;
family word 1 adds value 2 for WAVETABLE. Words 16-31 retain the original sixteen
presentation controls in their low halves. The high halves of words 16-27 add
table, position, depth, rate, shape, restart, clock, envelope amount, timbre
attack/release, velocity amount and wheel amount; remaining high halves are zero.
The adapter checks every range, duplicate and clock constraint before copying
the entire bank and revision. Detail schema 3 publishes the unpacked 28 controls
and native tune at index 28 without widening the existing fragmented transport.
CPS8 retains signed spread; CPS6/CPS7 retain their original upward octave counts.
CPST v3 presets are 96 bytes and read legacy v1/v2 records with neutral wavetable
defaults. Routing and native Sound values remain outside the custom preset.
The Stage 3 package above is a historical checkpoint.

The FM revision uses CPSA (header version 10), adding family 3. For this family
the twelve upper halves hold ALG, BAL, RTA, RTB, FDBK, ATK, DEC, SUS, REL, ENV,
VEL and WHEL; common COLOR carries DEP. CPS9 retains its old family 0..2
interpretation. Detail schema 4 publishes the active bank; older telemetry
cannot acknowledge pending FM edits. CPST v4 records are 128 bytes and preserve
independent legacy COLOR, wavetable and FM settings, while reading versions
1/2/3 without changing their previous sound. Full ranges, layout and migration
rules are in the [FM contract](../../../../docs/design/canopy.md#four-operator-fm-extension-cpsa).

FM uses TONE A for DEP and a family-specific TONE page. A conditional fourth
FM ENV page exposes the timbre ADSR and signed modulation amounts. The native
page vector owns four entries; leaving FM returns navigation to three pages.
Native TUNE, source LEV, track LEVEL and held-SRC values retain stock semantics.
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
CANOPY now opts in: actual notes/arp state and revision-bound detail fragments
drive the SRC pages. The visible UI task drains telemetry independently of
painting and invalidates at most every 40 ms. Native TUNE/LEVEL use the stock
parameter editor and bounded custom value cells; the remaining controls use
stable preset IDs. The adapter captures queued encoder ownership at dispatch
and excludes stock tracks, alternate context and opaque modals.

Stage 5 acceptance is `tests/regression/runtime/canopy-stage5.test.ts`. The earlier
native-UI checkpoint used `out/patch/canopy-stage5-native.syx` and the matching
`out/patch/canopy-stage5-native.dkm`; these are not the FM artifacts. The
[FM handover](../../../../docs/handovers/HANDOVER-2026-11-09-canopy-fm.md) records
the matching CPSA firmware/package hashes and current acceptance status. Hardware
keyboard latency, worst-case CPU and Overloop coexistence require their own
Stage 6 results.
