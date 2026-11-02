# CANOPY implementation

This directory contains the four-note pad instrument with Stage 4 movement for the
[CANOPY design](../../../docs/design/canopy.md). Its dynamic package uses V2
reset/process callbacks and an assigned machine type. It requires the optional
DTII 1.17 `core/instrument-play` adapter; ordinary firmware cannot admit it.
The engine supports Velvet/Reed families, color, drift and local envelopes.
The final SRC interface, presets and hardware release remain later stages.
Synchronized-clock qualification is still in progress.

Build and pack with `patchkit build digikit/canopy` and
`patchkit pack digikit/canopy --device dt2 -o out/patch/canopy-dt2.dkm` through
the repository's Python wrapper. Select CANOPY on track 1. In firmware containing
the adapter, SETTINGS > DIGIKIT > KEYBOARD selects USB/DIN, channel and ENABLE.
Enable the corresponding stock MIDI receive ports as well. Development
installation uses the upload/selection protocol; the generic module-browser fit
calculator remains V1-only. Routing starts disabled on every boot. Learned chord templates persist;
keyboard and sound setup currently return to defaults.
Only track 1 is admitted. Other tracks and channels retain stock behavior.

`src/dsp/voice.s` implements four foreground notes, two short release tails and
four pending-replacement records in a caller-owned 256-byte bank.
Each note has its own token, gate, oscillator phase, velocity and linear envelope.
Attack defaults to 80 ms and release to one second; both are adjustable in
KEYBOARD setup (a note released during attack ends sooner). Output is mono, in
32-sample blocks at 48 kHz. A normalized sine rotator supplies the fundamental
and finite second/third harmonic families; harmonics fade before Nyquist.
`src/dsp/tuning.s` supplies MIDI 0â€“127 coefficients at A4=440 Hz.

Note-on rejects duplicate tokens and invalid notes/velocities without changing
existing notes. Note-off matches the exact token and releases only that
note. Zero-velocity Note On must already have been normalized by MIDI ingress.
QUIET is returned only after every local release finishes, and the entire output
block is always written. Independent banks allow separate track state.

The adapter supplies PLAY, ROOT + TYPE and learned chord modes. Presets remain a future stage. The callback layer supports CC64 sustain, CC121 pedal reset, CC123 All Notes Off and CC120
All Sound Off, plus 14-bit pitch bend at a fixed +/-2 semitone range, scoped to
MIDI port/channel.

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

The V2 callback in `src/dsp/v2.s` uses a 544-byte allocator-owned
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
Stage 3 now supplies the pad renderer; arp remains Stage 4. See the
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
producer and native SRC attachment remain unfinished. Build the updated CANOPY
package with the updated playback firmware: older packages cannot consume CPS6.

## Movement controls (CPS5 compatibility)

KEYBOARD setup exposes ARP MIX (0 pad, 127 arp), LOCAL BPM (30..300),
NOTES/Q (1..8), gate percent (1..100), UP/DOWN/UP-DOWN/AS-PLAYED/RANDOM,
one to three octaves, swing (0..75), LOCAL BPM, MASTER GRID or MASTER PHASE clock,
TRIG/PATTERN/CONTINUE restart and STRUM MS (0..500). In MASTER GRID, the
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
State is now 768 bytes: use the matching instrument-play firmware policy.
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
release and coexistence remain Stage 6; named SRC pages and presets are Stage 5.
