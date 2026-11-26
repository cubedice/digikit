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
can exceed a DSP block (finding 345).

Instance i is DSP slot i and registry instance i. A reset with word 10 = i+1
binds the slot to the reset's track; the CF flags the selected track's instance
with bit 15 of control word 0 so its status reaches STATUS. The UI edits the
selected track's instance; each instance keeps its own sound settings, SRC
state and status.

The SRC pages are the track keyboard's three: VOICE, FM ENV and DETAIL. VOICE
holds WAVE, the family's timbre controls and DRFT on H; FM ENV holds the
wavetable or FM envelope and its modulation amounts (Velvet and Reed have
none); DETAIL holds the stock TUNE (A) and source LEV (H) cells and MIX on G.
WAVE selects Velvet, Reed, one of the five wavetables or FM, and has its own
view-only identity so it cannot alias the FM algorithm field. Discrete lists
accumulate four raw ticks per item without acceleration. Family transitions
retain the held DSP voices and invalidate queued SRC edits. Hold SRC uses the
firmware's momentary values state. Detailed notes, pedal and clock state are
under `DIGIKIT > MODULES > CANOPY > SETTINGS > STATUS`.
See the [accepted UI contract](../../../docs/design/canopy-ui-review.md).
The global setup entry is CANOPY, with MODULATION, PRESETS, PANIC, RECOVER
KEYBOARD, the status line and `MIDI: TRACK CHANNEL`. MODULATION holds scan
clock/restart and the three wheel amounts. TUNE/LEV are exclusive to DETAIL
A/H; unused cells cannot edit stock source parameters.

The DTII 1.17 KeyboardView bridge (`core/keyboard-panel`) gives the panel keys
of a CANOPY track to the track keyboard, as for any track it plays; CANOPY has
no panel path of its own, and tracks the track keyboard does not play keep the
stock handlers. Leaving keyboard mode or detaching the view cancels the track
keyboard's panel notes. While the sequencer live records, a press on a CANOPY
track goes to the track keyboard's native record path
(`tk_panel_native_record_on`), with the stock audition guarded for that track
and pitch. During step entry, or with a trig held, the stock note action
records the note and the track keyboard auditions it; both releases pair. A
note the stock key handler writes with KEYBOARD held (without the note action)
is captured by the track keyboard (`tk_capture_panel_press`, then
`tk_capture_note_list_commit` after the stock note-list writer) and is not
auditioned. Steps recorded earlier keep their chord-note locks (IDs 53..55),
which the lock decode still reads; recording no longer writes them
(findings 347 and 125).

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

Instruction-level checks are in `modules/machine/digikit/canopy/tests/test_instrument_play.py`, with
stock dispatch coverage in `tests/patchkit/test_dsp_voice.py`. The separate integrated
acceptance for earlier CANOPY builds passed uploaded V2 admission, stock selection,
explicit routing, final stereo continuity, pedal, velocity and fault retirement.
FM compiled acceptance is tracked separately in the
[current FM handover](../../../docs/handovers/HANDOVER-2026-11-09-canopy-fm.md).
Neither kind of test establishes a hardware CPU budget.

The normal-file loader publishes the assigned type only after CREATE. Because
that happens after the stock machine chooser is constructed, a narrowly scoped
hook remembers the persistent view and refreshes its items once on the UI task
when the admitted type appears. The firmware module set remains boot-only.
Sound changes revoke ownership before in-place writes; committed pattern changes
may refresh the same admitted carrier.
Version 3 replies carry row-zero phase and allocation generation. A process fault
closes admission and requests acknowledged DETACH before releasing DSP storage;
published CF code/metadata remain retained until reboot for existing readers.
The adapter has its own UI tick and does not take Overloop's control callback.

The integrated device checks are the `canopy-arp-tests` project
(`modules/machine/digikit/canopy/tests/*.device.test.ts`). No firmware build in
this document authorizes a device flash.


## Keyboard modes and the track keyboard

The track keyboard (`core/track-keyboard`) is CANOPY's only note engine.
Without it in the image, CANOPY tracks are admitted and keep their Sounds but
nothing plays. A Sound still records its performance settings: the keyboard
mode (PLAY, or ROOT with a chord TYPE and VOICING) and a user chord shape by
value. The track keyboard reads them through `instrument_track_keyboard_legacy`
and plays them; a preset or Sound load sets them, and CANOPY's own pages no
longer edit them. The shape library (`canopy/shapes/v1`), route
(`canopy/routes/v1`) and channel (`canopy/channels/v1`) records stay on the
card unread. CANOPY has no MIDI route of its own: the track keyboard plays it
from the Digitakt's MIDI config, and hands the track's pitch bend, CC1 and
CC121 to CANOPY with its notes.

The shared adapter calls `instrument_track_keyboard_lanes(track, lanes)`
with bit 0 for the four foreground pad voices and bit 1 for the separate
timed voice. A nonzero mask claims the track. The first call requests
the normal generation reset and bank configuration. Once ready, the owner
sends resolved onsets and releases with
`instrument_track_keyboard_note(track, token, pitch, velocity, on, lane)`.
The bounded queue pairs event tokens, reserves release capacity and
drops stale generations after a handoff. A zero mask releases the track;
nothing plays on it until the track keyboard claims it again.

The carrier suppresses its own arp and strum for a claimed track. With
both lanes enabled it renders the Sound's MIX after eligible lock and
modulation overlays. A single lane plays at its full level and MIX shows
that lane. The decoded Sound bank retains its saved controls. Use a
matched CANOPY module and a firmware build with this adapter.

Latch ownership is removed. Control slot 12 is reserved zero. Old CPST v1-v5
records may contain latch 0/1; decode discards that setting and subsequent
saves write zero without changing other sound controls. Malformed former latch
values are still rejected.

PANIC releases sound. RECOVER KEYBOARD asks for all keys and pedals to be
released before clearing capacity quarantine and advancing the MIDI epoch. It
cannot restore a failed boot/session guard or a retired DSP callback; those
require a real restart.

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
LEVEL, held keys, pedal membership, tokens, phase and pointers.

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


## Stock MOD integration

CANOPY's six native custom SRC slots are fixed per source family. Mirrors
26/27/28/31/32/33 map to COLOR/DRIFT/ARP MIX/STRUM/GATE/SWING for Velvet/Reed;
POSITION/SCAN DEPTH/SCAN RATE/TIMBRE ENV/WT FM DEPTH/WT FM ENVELOPE for
wavetables; and FM DEPTH/FM BALANCE/FM FEEDBACK/FM ENVELOPE/FM ATTACK/FM RELEASE
for FM. The optional `instrument_mod_param` hook supplies native destination
descriptors only for the admitted instrument type and a live generation. Other
machines retain their normal lookup. The hook that calls it (`mf_typearg`) is in the machines service file
(`machines.dks`), which reaches the function through the info table `machines_ext_info` of core/machines (the word
is 0 without this adapter). Family labels publish with the control
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
rules are in the [FM contract](../../../docs/design/canopy.md#four-operator-fm-extension-cpsa).

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

Stage 5 acceptance was a manual device test, removed with CANOPY's own note engine. The earlier
native-UI checkpoint used `out/patch/canopy-stage5-native.syx` and the matching
`out/patch/canopy-stage5-native.dkm`; these are not the FM artifacts. The
[FM handover](../../../docs/handovers/HANDOVER-2026-11-09-canopy-fm.md) records
the matching CPSA firmware/package hashes and current acceptance status. Hardware
keyboard latency, worst-case CPU and Overloop coexistence require their own
Stage 6 results.
