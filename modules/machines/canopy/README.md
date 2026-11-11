# CANOPY implementation

This directory contains the four-note pad instrument with movement, presets and native SRC pages for the
[CANOPY design](../../../docs/design/canopy.md). Its dynamic package uses V2
reset/process callbacks and an assigned machine type. It requires the optional
DTII 1.17 `core/instrument-play` adapter; ordinary firmware cannot admit it.
The engine supports Velvet/Reed, factory wavetable and four-operator FM families,
drift, wavetable FM and independent note envelopes controlled by the stock AMP page. FM adds
a fourth, conditional FM ENV page; wavetable sounds instead expose WT FM.
The current source requires matching CPSB firmware and package builds. The
2026-11-10 compact controls and native MOD revision is installed on the physical
DTII (image ID `481b2b9a4fcddf5e`, finding 120 and its handover); the
installation and acceptance results below describe the preceding revisions.
Full Stage 5 and Stage 6 wavetable acceptance passed in the emulator with matching
CPS9 firmware and package builds, including Overloop recording, crossfades,
transfers and shared release tails. The matching wavetable firmware and module
were installed on the physical DTII on 2026-11-08; boot, package readback, preserved
values and the new SRC pages were checked. The direct CHRD/WAVE control revision
was installed on 2026-11-09 with slower-selector checks, saved-sound restoration
and USB/DIN routing verification. Physical timing/resource qualification remains open.
This is not a hardware-qualified release.

Build and pack with `patchkit build digikit/canopy` and
`patchkit pack digikit/canopy --device dt2 -o out/patch/canopy-dt2.dkm` through
the repository's Python wrapper. Select CANOPY on track 1. In firmware containing
the adapter, SETTINGS > DIGIKIT > CANOPY > INPUT opens USB/DIN,
channel and EXTERNAL MIDI settings. The built-in KEYBOARD/TRIG route is available on
the admitted track without enabling external MIDI.
Enable the corresponding stock MIDI receive ports as well. Development
installation uses the upload/selection protocol; the generic module-browser fit
calculator remains V1-only. External MIDI starts disabled unless you previously used SAVE ROUTING;
saved routing is restored after CANOPY is admitted and ready. Learned chord
templates and saved presets persist. Keyboard mode and sound controls start at
defaults until you load a preset.
Only track 1 is admitted. Other tracks and channels retain stock behavior.

`src/dsp/voice.s` implements four foreground notes, two short release tails and
four pending-replacement records in a caller-owned 256-byte bank.
Each note has its own token, gate, oscillator phase, velocity and amplitude envelope.
With matching V7 firmware and package builds, the stock AMP page controls each
note's AHD or ADSR shape. The earlier linear
attack/release fields remain available only to legacy fallback inputs. Output is
mono, in 32-sample blocks at 48 kHz. A normalized sine rotator supplies the fundamental
and finite second/third harmonic families; harmonics fade before Nyquist.
`src/dsp/tuning.s` supplies MIDI 0–127 coefficients at A4=440 Hz.

V7 keeps the audible AMP shape after the stock track effects. This prevents
bit reduction from holding a release at its smallest quantization level until
the note suddenly ends. Each note retains its relative envelope within the
polyphonic mix; pad/arp blending and bounded voice-stealing fades are included.
The core firmware and CANOPY package must both support V7. The older installed
V6 pair does not contain this correction. Evidence and qualification status:
[release ordering](../../../docs/findings/119-canopy-post-fx-amp.md).

Note-on rejects duplicate tokens and invalid notes/velocities without changing
existing notes. Note-off matches the exact token and releases only that
note. Zero-velocity Note On must already have been normalized by MIDI ingress.
QUIET is returned only after every local release finishes, and the entire output
block is always written. Independent banks allow separate track state.

The adapter supplies PLAY, ROOT + TYPE and learned chord modes. The callback layer supports CC1 mod wheel, CC64 sustain, CC121 controller reset, CC123 All Notes Off and CC120
All Sound Off, plus 14-bit pitch bend at a fixed +/-2 semitone range, scoped to
MIDI port/channel.

## Playing and selecting sounds

**SOUND B / CHRD** selects PLAY or a generated chord, including USER. PLAY sounds
exactly the notes you press; choosing a chord switches to root-note playing.
Turn CHRD fully left to return to PLAY.
Changing between PLAY and root mode releases the previous phrase; press new
keys to start. Changing chord type within root mode updates the held root.
USER plays the learned or preset-embedded shape. VOIC and STRM are unavailable
in PLAY, and USER keeps its learned voicing. The old read-only AUTO label is gone.

CHRD, VOIC, ARP lists and TONE lists use four raw knob ticks per choice, with no
speed-based acceleration. Small turns accumulate, reversing clears partial
movement, and lists stop at their ends. Continuous controls and native tuning,
source LEV and track LEVEL retain their existing scales.

**WAVE chooses the sound; CHRD chooses how your keys play it.** Both are edited
on SRC. Presets still store the original mode, chord, family and table fields;
existing sounds need no conversion beyond the existing preset migration.

SETTINGS > DIGIKIT > CANOPY contains four groups: INPUT for connections and
selector keys; CHORD LEARN for custom shapes; MODULATION for scan clock/restart
and wheel amounts; and PRESETS for save/load. ARP G/H select clock and local BPM.
Latch is removed: releasing a key starts its release unless the sustain pedal
holds it. Older presets load with their former latch setting discarded.

## Stock MOD destinations

The stock MOD page offers six CANOPY SRC targets. Their names follow the current
WAVE sound type; no extra assignment menu is needed.

| Sound type | Six SRC targets |
| --- | --- |
| Velvet / Reed | COLOR, DRIFT, ARP MIX, STRUM, GATE, SWING |
| Wavetable | POSITION, SCAN DEPTH, SCAN RATE, TIMBRE ENV, WT FM DEPTH, WT FM ENVELOPE |
| FM | FM DEPTH, FM BALANCE, FM FEEDBACK, FM ENVELOPE, FM ATTACK, FM RELEASE |

Choose a target with MOD's DEST control, then set its LFO depth and timing as
usual. Modulation is added to the knob value within that parameter's range;
turning it off restores the unchanged base value. It does not write movement
into a preset. Scan rate follows the selected free/synced range; GRID clock
keeps arp swing disabled. Native TUNE and LEVEL remain native destinations.
Changing WAVE also changes the meaning of any of these six active SRC routes.
Native Sound/project persistence of CANOPY's custom base controls and parameter
locks are separate work; exposing MOD targets does not add them.

## Wavetable controls

Press SRC to cycle SOUND, ARP and TONE. **SOUND D / WAVE** selects VELVT,
REED, ANLG, GLASS, VOCAL, ORGAN, METAL or FM directly. The first two are the original
synth sounds; the next five are wavetables. FM has its own TONE layout and a
conditional fourth FM ENV page; wavetables add the fourth WT FM page described below. There is no separate engine menu.
Velvet/Reed have COLR on TONE A; their other TONE cells are unused. Wavetable
position and its modulation share the complete TONE page.

| Encoder | TONE control | Meaning |
| --- | --- | --- |
| A | POS | Continuous position 1.00-8.00 through eight related waves |
| B | DEP | Scan depth 0-100%; full depth reaches 3.5 frames either side |
| C | HZ / RATE | 0.01-10.00 under HZ, or the available tempo divisions |
| D | SHAP | Sine, triangle or smooth random scan |
| E | ENV | Signed envelope amount to position |
| F | ATK | Position-envelope attack |
| G | REL | Position-envelope release |
| H | VEL | Signed velocity amount to position |

DEP starts at zero; raise it to hear automatic scanning. To keep a sound, use
CANOPY SETUP > PRESETS > SAVE CURRENT SOUND.

TUNE and LEV appear only on SOUND A/H. Dedicated LEVEL/DATA remains the stock track-level control;
holding SRC uses the native show-values presentation. No second level scale is added.

The MODULATION submenu selects scan clock/restart and signed wheel amounts for
position, standalone FM and wavetable FM. These amounts default to zero. Position modulation
clamps at the table ends. Mod wheel follows its USB/DIN channel; CC121 clears
that route's wheel, while CC120 stops sound without changing wheel position.

RST=TRIG restarts scanning at the beginning of a phrase (no held notes to at least
one), rather than on every chord note. Tempo-synchronized scanning holds its position
while transport or clock is unavailable. Selecting the scan clock resets RATE to
1 BAR for tempo sync or 0.10 Hz for free running. The scan clock is independent of
the arp clock. Timbre ATTACK/RELEASE use 0-127 control values, not milliseconds;
the position envelope rises, holds while the note is held, then releases.

The four custom preset slots save custom sound controls, mode and chord shape.
Native TUNE, source LEV, track LEVEL and routing are outside these presets.

The factory waves are original, reproducibly generated data, compiled into the
module. No sample-bank upload is needed. User-table import is a later extension.
Presets use CPST v5 (144 bytes), including independent wavetable, wavetable-FM and FM settings.
Existing 64-byte v1/v2, 96-byte v3 and 128-byte v4 presets remain readable and receive neutral
defaults for their missing source settings. Loading an older preset does not
rewrite it, but SAVE CURRENT SOUND then writes v5; older firmware cannot read
that updated record. Native Sound/project persistence and parameter locks remain
outside this custom preset format.

## Playing FM

On SOUND, turn D / WAVE past METAL to FM. CHRD still selects PLAY or a generated
chord on SOUND. TONE A becomes DEP: low values are soft and simple, higher
values add harmonic complexity. Turning WAVE away from FM remembers its settings
and restores the other source's own COLOR/POS and modulation parameters.

| Knob | FM TONE | FM ENV |
| --- | --- | --- |
| A | DEP | Timbre attack |
| B | ALG | Timbre decay |
| C | BAL between tones X/Y | Timbre sustain |
| D | FDBK on modulator B | Timbre release |
| E | RTA, modulator A ratio | Signed ENV amount |
| F | RTB, modulator B ratio | Signed VEL-to-depth amount |
| G | — | — |
| H | — | — |

Press SRC to cycle SOUND, ARP, TONE and FM ENV. The fourth page is omitted when
another source is selected. TUNE/LEV on SOUND, dedicated LEVEL/DATA and held-SRC values use
the native drawing and scales. WAVE, ALG and ratio choices use four raw encoder
ticks per item. MIX continues to mean pad versus arp; BAL means FM tone X versus Y.

TWIN uses two parallel FM pairs. STACK puts B through A into X and blends X with
a clean Y. SUM combines both modulators into X alongside clean Y. CHAIN routes
B through A, X and Y, with X/Y available for the final blend. DEP scales the
modulation; LEV changes output volume without changing those connections.

Ratios include musical integer/fraction choices and selected inharmonic values.
They are frequency multipliers, not cents or octave spread. A 1x/2x TWIN with
moderate DEP is a useful glass-pad starting point; an inharmonic ratio and falling
FM envelope make bell-like tones. CHAIN becomes complex sooner than TWIN.

High notes and high ratios progressively reduce the available modulation depth
to control aliasing. This changes brightness, not the LEV setting. The depth
limit moves smoothly when you change ratios; feedback keeps its separate
high-frequency attenuation. Two-times internal sampling and filtering reduce
aliasing but do not make extreme FM or feedback alias-free.

Switching between FM and another source uses a short fade out and in, about
5.33 ms altogether. Held keys remain owned throughout. Same-pitch arp notes with
matching drift inherit the parent's FM history; intentionally detuned or
transposed arp notes keep their own signal history.

FM ENV shapes brightness independently of loudness. ENV, VEL and WHEL default
to zero. Positive ENV adds complexity during its envelope; negative ENV reduces
it. The native track AMP page determines each note's audible amplitude envelope,
independently of FM ENV. Wheel modulation follows the incoming USB/DIN channel, like the
wavetable source. Exact technical contracts and acceptance requirements are in
the [FM design](../../../docs/design/canopy.md#four-operator-fm-extension-cpsa).

## Earlier carrier and transport evidence

The earlier legacy fixture owns sixteen separate voice banks and a 256-byte staging bank.
Each track additionally has a 256-byte controller record with 32 route pedal bits and
four deferred-token/route pairs; a separate 256-byte staging record makes pedal and
voice changes commit together. CC64 values 64–127 hold released keys; 0–63 release
only deferred tokens from that route. CC121 centers that route's bend and clears its pedal ownership
without releasing physically held keys. Reset clears pedal state for the affected
track (all tracks on epoch reset). Deferred tokens match current foreground or
pending-replacement ownership; stale stolen tokens cannot release a replacement.
Ten additional token/owner pairs store each base note and route, covering six
sounding slots and four pending onsets. Thirty-two signed bend values preserve
independent channel state. Bend changes only rotation coefficients, preserving
phase, envelope and velocity. Queued replacements carry the current bend and
apply it before their first sample; later bend messages update these pending
values too. Reset clears bend state. CC120 leaves bend unchanged.
Successful note-on prunes expired owners and records the new owner within the
same transaction. CC123 releases that route's keys using normal sustain rules.
CC120 clears its pedal/deferred ownership, cancels pending onsets and applies the
at-most-64-sample emergency ramp to its sounding voices, including release tails.
Other ports/channels keep their notes. No new voices or unbounded storage are added.
It applies each batch to staging and copies back only if every event succeeds.
Unknown releases are harmless; controllers other than CC64/CC120/CC121/CC123 or pitch bend and failed note allocation
reject the entire batch. The internal `CPS1` configuration schema accepts exactly
one word, value 1, selecting the fixed sine proof. It is not the release preset ABI.
Reset invalidates configuration and ramps affected voices down within 64 samples;
tails still render after reset ACK. A newer stream epoch resets all tracks.

When all four foreground slots are occupied, reclaim the quietest releasing note
first (oldest token breaks ties), otherwise the oldest held note. Preserve its
phase and gain in a free tail slot and fade it within 64 samples. If both tails
are occupied, fade the displaced note in its foreground slot and queue the new
onset there; it starts after at most two render blocks. Pending replacements can
themselves be replaced or released without sounding. Never-rendered intermediate
onsets need no audio tail. This allows an entire eight-event batch to commit
without rejecting it merely because four notes are held.

Token matching includes pending onsets. Releasing a stolen token cannot release
its replacement or lengthen its forced ramp. At most six sine oscillators sound
at once; the future arp group is not implemented. This fits the design's four
foreground/two shared-tail limit without adding hidden voices. Live routing still
requires the public V2 contract, carrier/AMP proof and supported controllers.

Run its concrete SHARC instruction-core checks with:

```sh
uv run python -m pytest tests/patchkit/test_canopy_voice.py -q
uv run python -m pytest tests/patchkit/test_canopy_instrument.py -q
uv run python -m pytest tests/patchkit/test_canopy_carrier.py -q
```

The tests check independent phase/velocity/release, exact token handling, capacity,
memory guards, silent idle output and pitch/envelopes across sustained renders.
Sustained checks cover one
second at MIDI notes 0, 69 and 127, then release to silence; waveform error against
the ideal sine/envelope stayed below 0.0002 in sample amplitude. This is a numerical
check, not a measured device CPU or latency claim.
Packet-to-kernel tests additionally cover schema rejection, atomic batch rollback,
independent track resets, note release, reset tails and stale generation rejection.
Allocator tests cover same-block bursts, delayed onsets, cancellation before onset,
reclaiming releases and stale note-offs after stealing.
The allocator checkpoint passed all 23 selected kernel, packet-integration and
frame-context tests, plus a focused quietest-release check. Lint, layering and
edited-doc path checks passed. The broader changed-file preflight still reports
256 existing stale references in 78 repository files.
The test-only raw adapter probe additionally checks pre-AMP output on inactive
tracks 0 and 15, preservation of note state across stock retriggers and velocity
changes, stereo copying without a second oscillator advance, and restarting a
quiet bank without a stock trigger. It checks register preservation and foreign
stores through the production voice adapter. It deliberately leaves stock ACTIVE
unchanged, including when the kernel becomes QUIET; this exposes the missing
carrier handoff rather than implementing one.

The internal `src/dsp/render.s` entry now publishes carrier controls through the
core adapter after each even-voice render. Connected component tests exercise
frame expiry, stereo sharing, envelope argument substitution and retirement
after configuration loss or reset. The final tail block remains ACTIVE; QUIET
and shared release occur on the following empty block. These sources are still
uninstalled. Connected tests now execute the workspace capture shim with a
synthetic workspace pointer instead of writing the envelope-record base directly.

The firmware carrier probe also connects the kernel and render publication to
the actual stock envelope in one execution core. Its bounded C/E/G scenario
checks independent release, low-then-high velocity, unchanged held audio through
AMP, and reset retirement. Stock configuration is loaded by the original DSP
caller from synthetic sustained settings. It still bypasses live MIDI ingress,
instance admission and the final filter/mixer path.

The probe additionally checks the stock gain builder and 32-sample application,
plus frame-velocity lookup across five curves. The lookup now continues through
the stock velocity latch into the gain builder in the same memory; separate
control attenuation and silence still work. Changing only the upstream cache
leaves the latched gain unchanged. Curve 0 is constant across keys, a candidate
for neutral shared velocity. An in-memory proof hook now substitutes that factor
for owned voices while retaining the stock cache and separate gain controls.
Pre-render ownership includes actual kernel tails and pending notes, even after
configuration is reset. The musical scenario now traverses admission, patched
gain construction, kernel, AMP and scalar audio gain in one memory image. Shared
velocity changes do not rescale the owned chord. The hook has resolved firmware
declarations but is not installed; final mixer output remains unproved.

The next conditional post-gain ramp is also included. The proof configuration
passes audio unchanged; enabling its envelope flag and transition word together
fades a block to zero. Both odd and even sample counts retain buffer guards.
Live control meanings and arbitrary Sound compatibility remain open; CANOPY does
not bypass this stock stage.

These checks do not establish stock AMP lifetime, final track output, MIDI latency
or device performance. The current V1 contract path skips inactive stock voices
and clears configured state at stock note-on. The raw path avoids that contract
but does not initialize or sustain the downstream stock AMP. CANOPY still needs
the separate V2 lifecycle and carrier path described in the design before this
kernel can become an installable instrument.

An emulator-only `test/canopy-transport` fixture now cold-boots the production MIDI
bridge and these lifecycle callbacks together. USB/DIN notes and individual
releases reach the sine banks after real reset/configuration ACKs; each event
batch drains through a real DSP ACK. Persistent bank/readiness storage is loaded
as zero-initialized data, fixing unknown boot state exposed by this test. The
fixture installs no renderer and proves no audio. Its preflight currently flags
tuning-table data as instructions; it must not be flashed. See the
[carrier findings](../../../docs/findings/78-canopy-carrier.md) and
[regression commands](../../../tests/regression/README.md).

The separate `test/canopy-carrier` fixture now exercises the raw renderer and
carrier hooks in booted DT2 firmware. Its USB/DIN chord produces finite, nonzero
final PCM; individual releases retire their own voices and the final release
returns the carrier to idle with final output below -120 dBFS. It shadows stock
WERP; selection now resolves the live Sound and refreshes its stock configuration
before CANOPY configuration acknowledgement and MIDI admission, without a stock
audition. Machine and project changes reset the instance, preserving old-key tombstones.
Compatible pattern changes within a project retain the instance and refresh the
destination stock carrier.
This remains a track-0 emulator proof, not a DKM or a supported keyboard workflow.
A fixture-only deserializer hook now brackets same-pointer, same-type Sound loads;
ColdFire tests cover the real stock routine with synthetic records. A booted
panel-driven temporary pattern reload also proves same-pointer invalidation,
re-admission and stale-release protection. A running switch between two prepared patterns also preserves source ownership
during observed scheduling lookahead and retains it after compatible commitment. The running-switch
regression now checks retained tokens, original key release, source/destination audio and settled release quietness,
and checks transition PCM against a source-only sine prediction on both channels.
The fixed-Sound pattern-continuity bound passes. Browser,
kit and paste coverage, broader transport cases and arbitrary-control audio continuity remain unproven. Public V2 initialization, broader Sound/control behavior
and final-output continuity remain open. See the
[carrier findings](../../../docs/findings/78-canopy-carrier.md) for measured results.

The extended booted carrier proof also compares velocity-127 G after velocity-24
C with G started from silence. Their final-output RMS ratio is 0.9999787403;
the C/G mixture is 1.0185269755 times the reference. Independent release and
quiet re-wake pass. This is final-output evidence against shared first-note
velocity suppression for the tested Sound, not phase-continuity or all-curve
acceptance; public V2 registration remains open.

The booted reload scenario also verifies pedal-owned USB C remains audible after
1.25 seconds beyond physical release, while DIN G releases independently. Pedal-up
retires C without cutting off a fresh physically held USB G; G's release returns
the final output to zero in the measured window. This is emulator evidence only.

## Current V2 callback

The V2 callback in `src/dsp/v2.s` uses a 2,976-byte allocator-owned
instance and normalized input events. It shares the atomic voice/pedal engine
with the transport proof. `test_canopy_v2.py` checks it both directly and through
the protected registry dispatcher, including paired held-voice phase/gain state,
independent velocity and bend, sustain, malformed batches and reset isolation.
CPS3 accepts family, color, drift, attack and release. CPS1 retains the historical
sine diagnostic. Descriptor budgets remain provisional; destructive reset
requires carrier quiescence. The optional adapter packages and admits the V2
instrument through the normal module protocol.


Stage 2 keyboard modes and learned shape memory are provided by the optional
`core/instrument-play` adapter. See
[the keyboard setup guide](../../core/adapters/instrument-play/README.md#keyboard-modes-and-shape-memory).
The Stage 3 pad renderer and Stage 4 arp are implemented. See the
[pad sound setup](../../core/adapters/instrument-play/README.md#pad-sound-setup-stage-3).


## Applied controls and SRC values (CPS6)

CPS6 is a 32-word configuration shared by playback and SRC presentation. Word
0 contains version 6 in its low byte and mapper mode (0..2) above it. Words
1..15 retain CPS5 sound and movement settings; words 16..31 contain the sixteen
stable SRC control values in ID order. Both owners validate ranges and reject
any duplicated SRC value that disagrees with its audio setting. STRUM must
match the truncated milliseconds-to-DSP-block conversion; SOURCE remains KEYS.

Both banks come from one locked snapshot with the SRC requested revision.
Mode, clock, family and BPM also advance that revision. Transport ACKs still
cannot clear pending markers without a matching DSP-applied bank. The status
producer and native SRC attachment are implemented; full Stage 5 wavetable
acceptance has passed in the emulator. Current wavetable builds require the
matching CPS9 CANOPY package and playback firmware.

## Signed octave spread (CPS8)

ARP encoder C (`OCTS`) expands in one direction at a time: 0 uses the played
octave, +1..+3 adds that many octaves above, and -1..-3 adds that many below.
The pad stays at its played pitch. Out-of-range extensions are omitted; ORDER
controls the sequence through the expanded notes. Edits affect the next arp
onset without resetting held pad notes. Four admitted notes expand to at most
sixteen candidates; there is still one arp voice. Values display without the
ambiguous OCT suffix. Current wavetable builds require matching CPS9 firmware
and DKM. V1 presets migrate
without changing their original sound; old CPS5/CPS6/CPS7 banks remain accepted.

## Movement controls (CPS5 compatibility)

The ARP page exposes RATE (1..8 notes/quarter), gate percent
(1..100), UP/DOWN/UP-DOWN/AS-PLAYED/RANDOM, signed octave spread (-3..+3),
swing (0..75), TRIG/PATTERN/CONTINUE restart, clock and local tempo (30..300).
SOUND G controls MIX (0 pad, 127 arp). ARP G selects LOCAL BPM, MASTER GRID or
MASTER PHASE clock; ARP H edits local BPM while LOCAL is selected.
SOUND F controls STRUM MS (0..500). In MASTER GRID, the
rate becomes a whole-step multiple and swing is disabled. Clock changes
restart timing observation. Setup values currently return to defaults at boot.

Generated chords can spread their onsets over the strum interval; PLAY keys
remain immediate. A new committed chord cancels obsolete delayed onsets while
preserving sounding common notes. TRIG means a new phrase, not each added key.
CONTINUE keeps timing across chord changes, including a gap with no held keys.
Arp gates never release pad owners. The lanes share two bounded release tails.

The sixteen-word CPS5 layout is version, family, color, drift, attack, release,
mix, local BPM, rate, gate, order, octaves, swing, clock, restart, strum blocks.
Strum uses 1500 blocks/second internally. CPS1/CPS3/CPS4 remain accepted.
CPS5 used 768-byte instances and CPS9 used 1,184 bytes; the current CPSB
instance is 2,976 bytes for all source families.
Use the matching instrument-play firmware policy.
Both MASTER modes wait for a complete observed boundary interval before playing.
MASTER PHASE uses the firmware's musical clock for 1..8 notes/quarter, including
triplets, and pair-preserving swing; MASTER GRID counts whole native master
steps. Neither mode falls back to local BPM. Missing frames, skipped boundaries,
large phase corrections and overdue boundaries silence the arp while preserving
held pad ownership. Recovery requires another complete boundary interval.
Minor backward clock corrections wait until phase catches up, avoiding duplicate
notes. STOP silences master arp; LOCAL remains independently playable.
The matched adapter forwards optional input V4 (88 bytes); old V2/V3 input is
accepted, but cannot drive MASTER PHASE. Descriptor and reset ABI remain V2.

Stage 4 is complete at the functional emulator boundary: menu/audio, both master
modes, local arp, strum, USB/DIN loss and recovery, and changed tempo/scale pass.
A lost external clock stays latched across timestamp wrap until a new pulse.
See the design and Stage 4 handover for artifact-specific evidence. Hardware
release remains Stage 6 qualification work; full wavetable coexistence passed in the emulator.
Named SRC pages and presets are implemented and included in the passing full
Stage 5 wavetable acceptance. The November 8 wavetable build is installed with
boot/readback/UI checks complete; physical timing/resource qualification remains open.

### Pad/arp note levels

At intermediate MIX, a same-pitch arp onset starts in phase with its held pad
owner. This avoids pitch-dependent cancellation from resetting the arp oscillator
independently. Its gate, envelope and drift remain independent; octave extensions
keep their own pitch and phase. Native filtering, AMP and send effects can still
change level across notes. Set MIX to PAD when comparing the pad alone.

## FM on wavetable sounds

Choose ANLG, GLASS, VOCAL, ORGAN or METAL with WAVE on SOUND, then cycle SRC
to WT FM. Position, scanning and the shared timbre envelope stay on TONE.

| Knob | Label | Function |
| --- | --- | --- |
| A | DEP | FM depth, 0-100% |
| B | RAT | Modulator-to-note frequency ratio, 0.25-8.00 |
| C | ENV | Timbre envelope to FM depth, -100% to +100% |
| D–H | — | Unused; TUNE/LEV stay on SOUND, ATK/REL on TONE, WHEL in MODULATION |

Start with DEP around 30 and RAT 1.00 or 2.00. Increase DEP for a stronger
change; noninteger ratios give more metallic timbres. RAT uses the same slower
selection as WAVE/SHAP. With DEP, ENV and WHEL at zero the original wavetable
sound is preserved. Positive ENV or WHEL can add FM even with DEP at zero.
High notes and ratios use less modulation to control excess high-frequency energy.

ATK/REL share the existing wavetable timbre envelope, including its use for
position when enabled in WAVETABLE TIMBRE. Use the stock AMP page for loudness
attack/release. The standalone four-operator FM source keeps its own settings.

Explicit saves now write 144-byte CPST v5 presets. Older presets load with
wavetable FM off; loading alone does not rewrite them. This extension needs
matching CPSB firmware and CANOPY module builds. Implementation and verification
details are in the [design](../../../docs/design/canopy.md#wavetable-fm-integration-cpsb).
