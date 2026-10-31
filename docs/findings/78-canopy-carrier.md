# CANOPY carrier and stock envelope, DT2 1.17

2026-10-29. **[D]** Single-agent static inspection and bounded concrete-core
execution. No second-agent byte verification or running-device acceptance yet.

## Separate sample activation from AMP lifetime

The supplied DT2 OS 1.17 SYX has SHA-256
`26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5`.
Its extracted section 7 has SHA-256
`0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2`.
The analysis catalogue labels this identical DSP blob as 1.16; these observations
were made on extraction from the supplied **1.17** image.

The sample-record arm routine at short-word PC `0x1c4eaf` sets ACTIVE and seed
pending, resulting in word `0x00010001` at record byte offset `0x1b8`. Its caller
in the voice dispatcher is at `0x1c6582`. It does not receive the independent
envelope record. In the component probe its execution leaves that record
unchanged. Calling this routine alone therefore does not establish AMP wakeup.

**[C] [D]** The older voice-hook section in
[finding 06](06-sharc-engine-and-startup.md) identifies `0x1cdd8b` as level/envelope
gain. Inspection of its complete body, including the branch to `0x1cddff`, instead
shows quantization: table-selected bit counts, power-of-two shifts, rounding and
integer-to-float reconstruction. Treat it as a bit-depth-reduction candidate,
not the carrier envelope hook. The older measured LEVEL/filter/reverb results
remain separate evidence; this correction concerns the function label.

The envelope candidate at `0x1cbb57`, called at `0x1c6f29`, operates on an
independent 11-word record. The dispatcher derives its record base from workspace
`+0xd604` and advances by `0x2c` per voice. It passes audio input/output buffers and
two stack flags followed by the sample count. The routine includes attack, hold
and release branches; entry flags can restart its envelope. Mapping those flags
back to live MIDI and CF frame writers remains open.

## Reproducible component experiment

[canopy_carrier_probe.py](../../tools/sharc_dev/canopy_carrier_probe.py) refuses
unrecognized firmware hashes and executes the original instructions through the
existing SHARC runner. Each call is limited to 10,000 instructions; the CLI permits
at most 1,500 blocks. Firmware bytes and generated dossiers stay under ignored
output directories.

```sh
uv run python -m tools.sharc_dev.canopy_carrier_probe --blocks 1500 --out out/canopy-carrier-analysis/probe.json
```

The probe seeds an explicit synthetic envelope record: HOLD index 127, mode word
1, state 3, internal amplitude -1.0, and constant input 0.25. This is **not** a
claim that normal carrier setup has been implemented. With both entry flags zero:

- 1,500 blocks / 48,000 samples pass unchanged.
- State remains 3 and the hold counter advances to 48,000, rather than resetting
  each block. Thus unchanged output is not the result of continual retriggering.
- Setting state 4 explicitly enters release. The maximum absolute output after
  the first sample is about 0.09319, below the 0.25 input.
- Raising the first entry flag for one block restarts the envelope. Its last
  sample returns to 0.25; the following block passes unchanged again.

All seven report checks passed. This establishes a sustained path in the actual
envelope instructions, not the full filter/mixer output, correct live flag
ownership, keyboard behavior or a device CPU measurement.

## Remaining carrier integration

### Configured envelope and transition policy follow-up

**[D]** The probe now calls the stock configuration routine at `0x1cba9e` before
rendering. It no longer seeds hold/release state or amplitude by hand. The
investigation uses mode 1, normalized attack 0 and HOLD 1, with the remaining
normalized arguments 0/1/0 and final flag 0. The routine produces HOLD index 127,
mode 1 and zero runtime state. A single first-flag pulse starts its envelope;
the second flag releases it. These are component call arguments, not resolved
CF MIDI fields.

The probe also executes the assembled adapter
[carrier policy](../../modules/core/adapters/midi-events/src/dsp/carrier.s),
feeding its emitted flags into the stock envelope. It supplies conflicting stock
trigger and release flags on every held block. All **nine** checks pass over
1,500 held blocks: the counter advances from 31 after onset to 48,031, with no
retrigger; QUIET emits release and a new ACTIVE edge wakes again. The report has
exactly two wake edges and one release edge. Input is still constant 0.25 and
the full mixer is not executed.

Eight [CANOPY adapter checks](../../tests/patchkit/test_canopy_carrier.py) also
pass. They exercise the policy with the real kernel's ACTIVE/QUIET result,
including held notes, independent releases, local tails, per-track isolation,
unowned stock pass-through and invalid indexes. The kernel and firmware-envelope
checks are separate component experiments, not a continuous live device chain.

### Still open

The stereo handoff now publishes a block-stamped pair once per track and reads
it without mutation for both voices. Ten adapter tests cover both endpoint
tracks, odd-first and repeated reads, stale blocks, an initial zero stamp,
ownership loss, per-voice stock flags and invalid indexes. The firmware probe
checks both reads on every block before feeding the pair into its envelope call.
This handoff is now used by a component-tested envelope entry hook.

**[D] Entry-hook follow-up:** `src/dsp/envelope.s` in the MIDI adapter replaces
the first instruction at `0x1cbb57`, executes its displaced 16-normal-word stack
reservation, and resumes at `0x1cbb5a`. It derives the voice by bounded lookup of
the 32 aligned 44-byte records relative to a configured base; unknown records
fall through. It substitutes only stack arguments +1/+2 when a current owned
pair exists. The audio pointers and count remain stock.

The firmware probe now overlays an entry jump in its private loaded-memory view.
It supplies conflicting stock flags at the original entry, so only the hook can
apply the published pair. Producer and firmware cores remain separate; the harness
copies the producer's actual publication into the firmware core each block.
This proves the entry interposition, not live frame delivery. No SYX is written.
Twenty-four adapter tests check the hook at both stereo endpoints, scalar/SIMD
register and status preservation, the displaced stack adjustment, argument
sentinels, and fallback on stale/missing/disabled/unaligned/out-of-range inputs.

**[D] Frame/render follow-up:** The early-block wrapper has an optional carrier
callback that increments the stamp and invalidates all old pairs, even on wrap.
The CANOPY raw render entry now publishes its ACTIVE result and configuration
readiness through the core carrier owner. This owner retains a previously active
track through reset tails and the release edge. The kernel keeps ACTIVE set for
the entire block containing final tail samples; reporting QUIET at that block's
end would release the downstream envelope before those samples were processed.
Connected instruction-core tests now run frame, render and envelope entries in
one memory image without copying flags or stamps. They supply the envelope-record
base and stub packet receive/reply; this is not running-firmware delivery.

**[D] Workspace binding follow-up:** The dispatcher at `0x1c6479` saves its R4
workspace argument; `0x1c647d` loads it into I1 and `0x1c648a` adds byte offset
`0xd604`. The new workspace shim executes that exact displaced MODIFY, stores
I1 as the envelope-bank base, and resumes at `0x1c648d`. It leaves general
registers, SIMD partners and arithmetic status unchanged. Four component cases
cover scalar/SIMD operation and two workspace addresses, guarding the adjacent
stamp against an accidental SIMD double store.

The firmware probe now patches both sites in memory and runs original dispatcher
instructions from `0x1c6479` through its bank store at `0x1c649d`. The captured
base equals the stock result at I6-25 and updates when the synthetic workspace
moves. The probe no longer writes the envelope-bank pointer itself. Firmware
declarations and the DT2 port now contain four hash-resolved sites and two replace
hooks; none is installed by the normal manifest. Full port regeneration lacked
the local ColdFire analysis database, so only these DSP sites were resolved with
the standard resolver and rendered into the existing port; all existing anchors
were preserved. Booted dispatcher execution remains unproved.

**[D] Stock configuration caller and musical chain:** The probe now enters the
original argument loader at `0x1c6a0a`, calls stock configuration at `0x1c6a1e`,
and stops after its caller restores the argument stack at `0x1c6a28`. Its
synthetic I5-relative inputs are mode at -7, attack -6, hold -5, decay -4,
sustain -3, release -2 and the final flag -1 (normal-word offsets). R6/R7 carry
the last two values already loaded before this snippet. Sustained proof settings
remain mode 1 and normalized 0/1/0/1/0, flag 0. This maps the DSP caller, not the
CF parameter writer or front-panel knob values. The implementation preserves
stock AMP as a master shape, as specified; it does not force all Sounds to sustain.

A new 260-block musical scenario executes the production sine kernel, render
publisher, frame expiry and patched stock envelope in one core and memory image.
It plays low-velocity C and E, adds high-velocity G, releases E, waits for E's
local tail, then invokes the production reset ramp. It checks independent tokens
and gains, finite nonzero post-envelope audio, exact sample equality across held
envelope blocks, a continuously advancing hold counter, and one shared release
only after the last audio block. Unlike the older constant-input policy scenario,
this path does not copy flags between processor instances. It directly invokes
note callbacks and marks the proof instance configured; live MIDI, registration,
the frame scheduler and downstream gain/filter/mixer are not included.

**[D] Downstream gain and velocity-cache follow-up:** The probe executes the
coefficient builder at `0x1c6a28` through `0x1c6a49` with controlled inputs.
For `a = DM(I5-32)`, `b = DM(I1+31)` and incoming F14 = c, the target stored at
the original I3 is `2 * clamp(a*a*b, 0, 1) * c`. Six cases cover square-law
attenuation, the cached factor, saturation, the machine-path factor and zero.
The probe then executes the actual scalar gain application from `0x1c6f4e`
through `0x1c6f8f`; all 32 input samples receive the expected gain and adjacent
sentinels remain intact. Entry must include the odd/even sample-count branch:
starting inside its odd-count path incorrectly processes 33 samples for a
32-sample block. This is a harness-entry requirement, not a firmware defect.
The exact control semantics of a/b/c are not yet established; in particular,
do not treat this entire gain as velocity and bypass LEVEL or modulation.

Separately, the probe executes `0x1c2546` through the velocity-cache write at
`0x1c25cd`. It reads the actual frame velocity word at `0x255910` (frame +0x34),
shifts it by eight, indexes a curve at `0x8045a9b8 + 512*curve`, multiplies the
lookup by 127/128, and writes the track-zero cache at `0x25242c`. The caller
clamps the curve selector to 0..4 before this slice. Twenty-five measurements
cover five velocities on each curve. Curve 0 yields 0.9921875 for every tested
velocity; curve 2 follows velocity/128; the other curves vary nonlinearly.
Curve 0 is therefore a candidate for neutral shared velocity while CANOPY keeps
its local per-note velocity. This is neutral across key velocities, not unity
gain. No velocity override is installed.

All 21 report checks passed in the combined bounded run (eight constant-input
hold blocks, 260 musical blocks, six gain cases and 25 velocity cases).

**[D] Velocity latch reaches the gain builder:** The cache reader at `0x1c33b1`
loads through I1, then the delayed branch at `0x1c33b5` stores that word into
`DM(I2+31)` at `0x1c33b8`. For the first voice this is `0x24f02c`: the update
loop starts I2 at `0x24efac` and advances it at `0x1c2d11`. The dispatcher starts
its corresponding pointer at workspace +0xdce4 (`0x24efac` with the stock
workspace), transfers it through M8 into I1, and advances I1 at `0x1c66a5`.
Its gain read at `0x1c6a2b`, `DM(I1+31)`, therefore reads this latched velocity.
This identifies factor b; the identities of a and c remain separate questions.

The probe now continues all 25 velocity lookups through this original latch
and the gain builder in the same memory. It seeds the slice registers explicitly;
it does not execute the intervening scheduler. Adjacent latch sentinels survive.
For each curve/velocity it checks gain targets with a=1, 0.5 and 0. With c=1,
the results are 2*b, 0.5*b and zero. Changing the upstream cache to 0.125 after
the copy leaves both the latch and these targets unchanged. All 24 aggregate
checks pass, including the existing musical and audio gain scenarios.

**[D] Ownership override placement:** An upstream cache override alone is
insufficient: it needs the stock copy to run. Prefer replacing only the velocity
operand at the coefficient-builder read, leaving the square-law control, clamp,
machine multiplier and gain ramp intact. Establish ownership before this stage;
the current-block carrier pair is published later by rendering and cannot serve
as its admission check. The predicate must include admitted CANOPY instances
and retiring local tails, reject stale/reset instances once quiet, and preserve
the stock operand for every other voice. Prove both stereo members, ownership
changes and DSP context preservation before declaring or installing this hook.

The proof implementation now does this at `0x1c6a2e`, immediately after the stock
velocity load. `velocity-entry.s` derives the voice from I1 and the captured
workspace (envelope base +0x6e4, four-byte stride), rejects missing/misaligned or
out-of-range pointers, and replaces only R1 for owned voices. It restores status,
square-law R2 and SIMD partners, then executes displaced R7=1 and returns to
`0x1c6a31`. As in the original instruction, R7=1 also updates S7 in SIMD mode.
The hook does not rewrite the latched stock value. The proven audio builder
path remains scalar; context preservation with SIMD enabled is not a SIMD audio
gain proof.

The block bridge calls optional `_instrument_carrier_prepare` after receive and
before reply. CANOPY prepares all 16 admission records from readiness plus its
six live/tail tokens and four pending tokens. Scanning the kernel matters when a
note is reset before its first render, with no prior ACTIVE publication. Records
expire explicitly at frame start, including stamp wrap. Both stereo members read
the same snapshot; quiet, unconfigured tracks return to stock velocity next frame.
This callback has no provider in the ordinary installed module set.

The firmware probe installs the gain hook only in private emulator memory and
runs the real builder for owned and stock voices 0, 1, 30 and 31. All eight cases
retain the stock cache, preserve the independent control coefficient, and select
127/128 only for owned voices. All 25 aggregate probe checks pass. Component tests
cover stereo membership, skipped preparation, wrap, retirement, malformed pointers
and scalar/SIMD context. Firmware declarations and normal installation of this
new hook remain pending; no live MIDI or final mixer proof follows from this run.

Validation for this checkpoint: 62 carrier component tests pass; the changed
MIDI adapter builds and its object check passes. Targeted Python lint/format,
changed documentation references and layercheck pass. The broader changed-file
preflight still reports 256 existing stale references in 78 files and cannot
start its routed host checks because `pnpm` is unavailable in the WSL environment.

**[D] Connected musical gain path and declared hook:** The 260-block C/E/G
scenario now runs admission, the patched coefficient builder, the sine kernel,
the patched stock AMP and actual scalar audio gain in one memory image. The
latched stock velocity alternates between 0.125 and 0.75 every block. Owned gain
stays at 0.5 * 127/128 (proof control 0.5, machine factor 1); after retirement it
follows the stock value again. Every post-AMP sample is checked against the
original gain stage's result, with adjacent buffer sentinels. Independent release,
per-note velocity, uninterrupted master hold and reset retirement still pass.
All 28 aggregate checks pass. The proof deliberately uses a settled coefficient
(zero ramp delta), synthetic control values and explicit slice entry; it does not
prove the live scheduler, changing-control smoothing or final mixer output.

Two hash-resolved velocity sites and one replace hook now appear in the firmware
declarations and DT2 port: operand at `0x1c6a2e`, continuation at `0x1c6a31`.
Both resolve against the pinned OS 1.17 image, preserving every existing anchor.
As with the earlier envelope sites, this is scoped DSP resolution; full port
regeneration still needs the missing local ColdFire analysis inputs. No normal
manifest claims or installs the new hook.

**[D] Conditional stage after gain:** The next per-voice loop at `0x1c6fb9`
checks a byte at workspace +0xdde4 +voice and envelope-record byte offset 20.
Only when both are nonzero does it multiply the block by a ramp from 1 to zero.
The probe executes one original loop body through `0x1c7009`, including its
odd/even count branch. All four flag combinations pass for 31 and 32 samples;
the ramp matches `1 - sample/(count-1)` within 1e-6 and adjacent sentinels survive.
The harness supplies the reciprocal ramp delta; this does not prove its caller
or behavior at counts outside those two cases.

The envelope flag is now driven through the original configuration caller for
these measurements, rather than written into the record. The caller's I5-relative
word -1 becomes stack argument +5 and is stored at record +20 by `0x1cbb0b`.
Both 0 and 1 arrive unchanged. Static inspection also identifies the transition
array as `0x24f0ac` with the stock workspace: the branch at `0x1c33ce` writes M14,
and the helper at `0x1c6056` clears it using M13 at `0x1c606e`. Those are DSP
writers, not a proven mapping to MIDI note-off, front-panel mute or a Sound setting.

The connected musical scenario now runs this stage after ordinary gain, using
the real envelope record and alternating a synthetic transition byte. The proof
configuration's final flag is zero; all samples remain unchanged through this
conditional stage, including local release and reset retirement. All 30 aggregate
checks pass. This result is conditional on that setting: an arbitrary Sound with
the flag enabled can still fade a held block when the transition byte is raised.
Resolve the controls and live event writers before deciding whether any additional
ownership interception is appropriate. No new bypass or firmware hook is added.

**[C] Trigger latch width:** The earlier description of a word per voice was
incorrect. Bounded OS 1.17 execution confirms a byte per voice at `0x24f0ac + voice`.
The post-gain probe now writes exactly that byte. This does not change the separate
four-byte velocity-cache stride used by the velocity hook.

**[D] Incoming trigger and machine change:** The probe reads the actual incoming
track mask at frame offset `0x22` through `0x1c2cda`, then executes the branch at
`0x1c2d1a` and the machine-type comparison at `0x1c33bc`. Across 24 cases (voices
0, 1, 30, 31; trigger absent/present; unchanged type with flag 0/1 or changed type),
only the selected voice's latch byte is raised. Without a trigger, the fade
parameter remains untouched. With a trigger and unchanged type, the incoming
flag is preserved; changing the machine type forces it to 1. All 31 aggregate
probe checks pass with `--blocks 8`.

The input flag comes from frame offset `0x12e + 0x60*track`; its public control
meaning remains unresolved. These are isolated DSP slices with synthetic frames,
not a running ColdFire-to-DSP event-delivery proof. The implementation policy is
to consume owned keyboard events before stock note-on routing and preserve the
stock machine-change fade. Sequencer triggers and live configuration delivery
still need a composed firmware proof before that policy can be enabled.

The combined pattern-position/MIDI frame test now covers absent carrier callbacks
and the real CANOPY carrier callbacks. It checks register and frame preservation,
frame-stamp advancement and expired/unowned velocity records after reset.

**[D] MIDI hook consumption boundary:** The ColdFire component suite now links
the complete production runtime trampoline, DIN trampoline, MIDI dispatcher,
ownership ledger and transport. A test wrapper supplies the firmware's existing
three-argument stack shape and records the selected continuation. USB sources
2/4 and DIN sources 8/16 consume owned note-on, note-off and velocity-zero
note-on messages, while unclaimed channels take the stock continuation. Reset
tombstones consume old releases; fresh presses on the disabled route remain
consumed. The callback preserves D0/D1/A0/A1, tested callee registers D2/A2, all
three arguments and the original stack position. USB upload messages reach the
runtime upload handler; DIN SysEx remains on its stock path.

This executes the real assembly branches and production C on the ColdFire core,
with the runtime's actual interrupt-mask helpers. Readiness and transport ACK
state are injected, and the continuations are observation stubs. It does not
execute the live USB/DIN parser, stock router or outgoing stock trigger mask.
Thus it closes the callback-to-continuation component gap, not the live ingress
or final-audio gate. No normal MIDI route is enabled by this test.

**[D] Stock USB parser boundary:** On the pinned OS 1.17 image, registration at
`0x400ccae2` supplies cable-0 callback `0x4011da4c`. The callback accepts channel
messages as well as SysEx; the older SysEx-only description is too narrow. Five
ColdFire instruction-core cases execute its original bytes with bounded test
allocation, queue-push and semaphore-wakeup replacements. They cover a note split
across three calls, running-status notes, clock interleaved between note data bytes,
replacement of an incomplete message by a new status, and channel-16 note/CC/bend.
Each queued message has the expected status/data and length. Interleaved clock is
queued separately without destroying running status; velocity-zero note-on stays
`0x90` for the ownership layer to normalize. All five cases pass.

The test pins the whole supplied SYX hash before using addresses. It replaces
the short-message allocator at `0x4011d956`, queue push at `0x40001896`, and wakeup
at `0x4000155c` only in the private test image. Parser state and service handles
are synthetic; the byte-parser body and message-length table are stock. This is
neither a USB packet/interrupt timing proof nor execution of the queue task.

Static inspection of that task finds the channel-enable test before USB routing
at `0x4011f1c0` (bit 1 of `0x429368ca`), then source 4 for the auto channel or 2
otherwise, and the existing hook at `0x4011f1e2`. DIN uses bit 0 and source 16/8
before its hook at `0x4011f178`. Thus live acceptance must exercise enabled and
disabled receive settings as well as ordinary/auto channels. Do not infer live
reachability solely from the parser or trampoline component results.

**[D] Booted firmware ingress:** The separate Vitest `midi-ingress` project now
passes against DT2 1.17's ready snapshot and the existing `digi-emud` binary. A
private observer in the port's reserved CF expansion region records each message
at the USB/DIN hook, then executes the displaced stock router call. With receive
enabled, note-on/off on channel 1 (set as auto) and channel 2 arrive with sources
4/2 on USB and 16/8 on DIN, exact three-byte payloads and no observer overflow.
Disabling each port's receive bit prevents its channel messages reaching the hook.
All four port/enable combinations pass without guest faults.

USB injection uses the emulator's byte callback; DIN uses its UART/eDMA path.
The test changes receive/auto-channel settings in emulator RAM and restores them.
It uses zero DSP and always forwards messages to stock routing: this closes
parser-to-hook reachability for these inputs, not live CANOPY ownership, DSP
delivery, hardware timing or final sound. The preparation command pins the SYX
hash; firmware tail bytes and the observer artifact remain ignored under `out/`.

**[D] Coupled MIDI lifecycle:** The standalone `canopy-transport` Vitest project
cold-boots a patched DT2 1.17 image with the real SHARC core. Its test-only CF
provider requests track-0 reset, submits CPS1 revision 1 after reset ACK, then
admits USB and DIN channel 1 only after the DSP configuration ACK. The production
MIDI hooks, CF ledger/frame sender, DSP receiver and CANOPY lifecycle callbacks
carry C/E on USB and G on DIN. Individual note-off and velocity-zero releases
change only the matching token's gate. Each batch advances the message sequence,
drains the acknowledged ledger and leaves transport/guest faults clear. The test
passes with three distinct tokens and all three gates released. No heartbeat or
ACK is injected. The fixture has no renderer, carrier hooks or public descriptor;
released tokens therefore remain in their slots. This proves live delivery and
release ownership in the emulator, not final audio, hardware timing or recovery.

**[D] Boot initialization correction:** The first coupled run found 168 unknown
bytes in the 256-byte track bank after reset/configuration succeeded. SHARC
`.PKBSS` reserves storage without loading zeroes; the component rigs had supplied
zeroes themselves. CANOPY banks and readiness now use loaded `.PKDATA`, while
fully overwritten transaction scratch remains `.PKBSS`. The coupled test requires
all bank bytes to be known and no tokens before the first key. Reset still
preserves existing release tails. The corrected live test and 65 instrument/carrier
component tests pass.

The emulator fixture's firmware preflight remains blocked by two data-as-code
ISA hits. In patched SHA-256
`00b9c31e8e66373714402af8d2a594e130859324df98a0364b9c95b0baf29e53`,
reported PCs `0xbce66e` and `0xbce710` map to byte addresses `0x2009ccdc` and
`0x2009ce20`: offsets `0x27c` and `0x3c0` in `_cp_tuning` at `0x2009ca60`.
Both lie outside the plan's executable span `[0x2009c388, 0x2009ca60)`.
The appended-block scanner in `packages/patchkit/src/dspscan.rs` scans these data
bytes as code. Object build re-decoding passes. Do not flash the fixture or bypass
preflight; resolving this scanner limitation is a remaining release prerequisite.

Resolve remaining event writers before adding
the hook to the production manifest. Use idle-to-active to start the envelope once; avoid
rearming it for subsequent keys. Retain the carrier through local release tails
and pending onsets, then retire both stereo voices on QUIET. Prove neutral shared
velocity and downstream LEVEL, pan, mute, filter and FX in running firmware.
The [CANOPY spec](../design/canopy.md) still requires that final-output gate before
the pad engine and UI are enabled.

Checkpoint validation: coupled transport 1/1 and instrument/carrier components
65/65 pass; layer regression 20/20, repository layercheck, lint, formatting and
TypeScript checks pass. Changed-document pathcheck is clean. The required broad
changed-file check is not green: existing pathcheck reports 256 stale references
in 78 files; devtools verification has one `patch:toolchains` routing assertion
and four missing `dn2-1.11.sqlite` failures (222 tests pass). Host checks first
passed 193/193 (16 skipped); the repeat had one unrelated firmware-pause timing
failure (12 ms), with 192 passing. No firmware is approved for flashing here.


**[D] Booted carrier and final PCM (2026-10-30):** The separate emulator-only
`test/canopy-carrier` fixture links the existing workspace, envelope and velocity
interposers, frame expiry, CANOPY pre-render ownership and raw renderer. Its raw
registry row shadows stock WERP type 1 only in this fixture. The Vitest
`canopy-carrier` project selects WERP through FUNC+SRC, DOWN and two YES presses,
then auditions TRIG 1 once. Selection alone left the DSP type cache at zero in
this run; the audition initialized it to 1. This explicit primer is a remaining
V2 initialization gap, not a release requirement to impose on keyboard players.
Registry type zero cannot select a renderer: it terminates the raw voice table.

After that setup, USB C/E and DIN G reach the real lifecycle callbacks. Oscillator
state advances; releasing C retires only C, releasing E (velocity-zero note-on)
retires only E while G remains held, and releasing G empties the foreground bank
and returns carrier ACTIVE to zero. No further stock audition occurs. Transport
and guest fault checks pass. The test allows up to 60 wall-clock seconds for each
one-second DSP release, because this coupled run is slower than real time; no
hardware performance claim follows.

During E's release with G held, the final PCM tap produced 96,640 scalar stereo
samples (1.0067 seconds), all nonzero and finite, peak 0.0914142 and RMS 0.0405009.
This is the first booted MIDI-to-renderer-to-final-output result, beyond the earlier
component gain/envelope proofs. It does not yet prove spectral/phase continuity,
pedal behavior, final silence, arbitrary Sounds, pan/mute/filter/FX controls or
hardware timing. The bounded live test passes (about 133 seconds); 83 targeted
carrier/layer tests also pass.

Carrier transition history and published pairs now use loaded `.PKDATA` rather
than unloaded `.PKBSS`, matching the bank initialization correction above. The
normal MIDI adapter still does not install these carrier hooks. The fixture has
narrow, path-and-ID-bound layer exceptions for its specific source includes,
private imports and three firmware continuations; unrelated anchors remain denied.

A sample-card variant lost its acknowledged configuration after admission, before
note testing. The passing proof uses the isolated synth boot without sample-card
loading. Boot/load watchdog recovery and coexistence remain open. This fixture
must not be flashed: it replaces a stock renderer, has no public V2 descriptor,
and does not resolve the previously documented firmware-preflight data scan issue.

Carrier checkpoint validation: the live carrier test passes; targeted
carrier/layer tests pass 83/83 and conformance-routing/layer tests pass 30/30
(overlapping layer cases). The required changed-file check passes 193 host tests
(16 skipped), 44 routing tests, 21 layer tests, lint, formatting and typing.
It remains red only for the existing 256 stale paths and five devtools verification
failures described above; 222 other verification tests pass. Changed-document
references are clean. The raw-row integration fixture is excluded by exact ID,
path and framework type from the standalone V1 machine rig: it has no V1 note-on
or parameter contract, and its booted Vitest project supplies the required live
lifecycle state. Ordinary raw machines retain their existing conformance checks.

**[D] Cold-boot admission boundary (2026-10-30):** The booted carrier test now
pins the stock SYX hash before reading the investigation addresses. After WERP
selection but before audition, the cached track-0 source pointer at `0x8000470c`
is zero, the outgoing row type at `0x80003cd0` is zero, and the DSP mirror at
`0x255970` is zero. After audition the source pointer is `0x426532ec`, its byte
at `+0xa2` is 1, and both row and mirror contain type 1. The snapshots are retained
in `out/canopy-carrier-admission.json`. This rules out an initializer that simply
refreshes the cached source: there is no cached source on this cold boot.

Static inspection of the supplied 1.17 MAIN image (base `0x40000400`) confirms
the stock refresh function at `0x4002d438`. It validates source and track, caches
the source pointer, copies 0x8e bytes from source+0x14 into the track parameter
row, copies 0x9a bytes from source+0xa2 into the outgoing machine row, and
tail-calls `0x400d9164`. This is broader than updating the type byte. Its
successful use as a direct V2 initializer remains unproved; do not call it with
the null cached pointer or replace its work with a type-cache poke.

The next admission proof must resolve the selected Sound from the live
project/kit, validate track/type and instance identity, refresh through the
stock configuration path, and confirm the DSP receives that configuration
before enabling keyboard ownership. It must work without any audition and
invalidate/retry on Sound or pattern replacement. The existing transport
fixture's acknowledged CPS1 configuration alone does not establish that stock
configuration readiness.

**[D] Final-output quietness:** The expanded live test initially rejected exact
digital silence after the last note and carrier retired. A fresh 4,480-sample
window measured RMS `2.83501e-8` and peak `5.07571e-8`, with no non-finite samples.
The residue's origin is not established. The acceptance check now measures both
idle and post-release output using fresh windows of at least 4,096 scalar
samples, requiring RMS and peak below `1e-6` (-120 dBFS). It drains at least
4,096 samples between carrier retirement and the final observation window.
The sine kernel's exact-zero idle contract remains unchanged; this tolerance
applies only to the final stock DSP output. Spectral continuity, sustain pedal,
arbitrary Sounds and hardware acceptance remain open.

The corrected bounded live test passes (138 seconds total). Idle output was
exactly zero across 4,416 samples; the post-release window contained 4,416 samples
with RMS `2.79082e-8` and peak `4.98258e-8`, both below the stated bound. The held
note window retained 96,640 finite nonzero samples and RMS `0.0405009`.
Admission snapshots repeated the null-before/populated-after result above.
No new initialization hook is installed by this checkpoint.

The required changed-file check passes layercheck, TypeScript typing, lint and
formatting; no related host or Python tests were selected. Its sole failure is
the existing repository pathcheck baseline (256 stale references in 78 files).
Targeted pathcheck on all three changed documents passes with zero stale links.

**[D] Selection-only initialization (2026-10-30):** The carrier fixture now
starts and sounds MIDI notes without any stock audition or sequencer trig. Its
CF initializer uses the declared project singleton and active-kit accessors.
For this track-0-only experiment, the kit's track object is at `+0x6c`; the
stock `track_type` path at `0x40036266` computes that address (stride `0x3c0`
for other tracks). The type getter at `0x4005063e` calls vtable slot `+0x28`
for the Sound pointer and reads its byte `+0xa2`. The fixture follows that
getter, checks pointer ranges/alignment and requires type 1 before calling
the stock refresh. These are pinned OS 1.17 layout facts, not a public SDK.

`cf.machine.fn.refresh_track` declares the refresh routine by its unique
172-byte instruction-window hash. Full port regeneration is unavailable here
because the local ColdFire analysis database is missing. The existing resolver's
exact-window matcher and port renderer generated only the added anchor, after
checking MAIN against the lockfile hash; existing anchors remain unchanged.

The shared transport fixture has an optional preparation callback. When present,
it waits before submitting CPS1; when absent, transport-only bootstrap follows
its previous path. Thus the carrier routes remain closed until selection,
stock refresh and real configuration acknowledgement. The carrier test checks
matching source/row/DSP type, exactly one refresh, then USB C/E and DIN G,
independent release, and final output quietness. No TRIG button is pressed.

The bounded test passes in 136 seconds. The admitted source is `0x426532ec`,
with source, outgoing row and DSP type all 1. Held output has 96,640 finite,
nonzero scalar samples, RMS `0.0405009`, and the same PCM digest as the earlier
audition-primed proof. After final release, a 4,416-sample window has RMS
`2.85231e-8` and peak `5.07571e-8`, below -120 dBFS.

This closes the isolated cold-boot primer gap only. The initializer is one-shot,
track 0, type 1, and uses the test fixture's configuration lifecycle. It does not
handle Sound/pattern replacement, re-admission, arbitrary track layouts or
concurrent lifecycle changes. Production V2 readiness and its DSP confirmation
contract remain open; normal firmware does not install this initializer.

Validation: carrier and transport-only live tests both pass, as do 21 layer
regressions. The transport manifest explicitly marks the preparation callback
optional; a weak C reference alone was insufficient for standalone linking.
Changed-document links, lint, formatting and typing pass. The required
changed-file check retains only the known 256 stale-link errors and five
devtools verification failures (task routing and missing DN2 analysis data);
222 other verification tests pass. No related host tests were selected.


## Observed identity changes and re-admission [D]

The track-0 emulator fixture now polls project, kit, Sound pointer, machine type
and selected pattern identity, including while configuration is in flight. A
change invalidates preparation, closes routes through `mi_instance_reset`, and
waits for reset acknowledgement before refreshing stock rows and obtaining a
fresh configuration acknowledgement. Old physical-key tombstones survive this
reset. Ordinary knob edits do not invalidate the instance.

The booted DT2 OS 1.17 regression passed in 180.65 seconds. It leaves WERP for
ONESHOT with C held, verifies reset and empty voice banks, then selects WERP
again. A fresh C remains held when the old C release arrives; a subsequent USB G
provides an ordered event barrier for that assertion. A stopped PTN+TRIG2 change
resets both held voices and closes routing on the destination's ONESHOT machine.
Selecting WERP there admits a fresh note, produces finite nonzero PCM and retires
on release. Final diagnostics report generation 4, ready phase 3, three stock
refreshes and two invalidations. The patched fixture SHA-256 is
`d8340a9ddc4212db58d77e4ca1398445274fbbd422d19508afce400353fbf772`.

An immediate post-reset output window initially exceeded the quietness threshold
(RMS about 3.04e-6) despite empty banks. Reset checks now drain a fixed quarter
second of stereo output before measuring a separate window. The -120 dBFS RMS
and peak limits remain unchanged. This proves settled quietness, not click-free
transitions or spectral continuity. Diagnostics and PCM statistics are retained
under `out/canopy-carrier-*`; the run log is `out/canopy-readmission-test.log`.

This supersedes the one-shot limitation above only for the tested identity
changes. UI-tick polling is not an atomic firmware lifecycle hook. In-place
same-pointer, same-type Sound replacement is undetected; running or queued
pattern changes may expose selected identity before the committed boundary and
remain untested. Project/kit/source pointer changes are observed by the code but
were not separately exercised live. Public V2 initialization, arbitrary tracks,
sample-card operation and full audio acceptance remain open. No device image or
installable CANOPY module is released by this proof.


## In-place Sound deserializer lifetime [D]

On DT2 OS 1.17 the Sound deserializer at `0x400db3b2` takes destination and
serialized-record pointers on the C stack. It rejects null pointers and records
whose word at +4 is not 3, then mutates the destination in place. The serialized
machine byte at +0xd2 becomes destination +0xa2. The destination includes data
beyond the parameter rows: the routine writes 64 twelve-byte records starting at
+0x140 and initializes additional state at +0x440. Tests therefore reserve a
larger synthetic Sound object rather than assuming the parameter-row size.

The fixture's new `cf.machine.hook.sound_deserialize` wraps this routine. Its
32-byte lookup window and eight-byte displaced prologue are hash-pinned in the
firmware declarations and DT2 port. The wrapper replays the prologue, preserves
the C calling convention and original boolean result, and brackets the original
call with begin/end notifications. Begin matches the tracked destination and
stock record version, then closes admission and requests the existing generation
reset before the first stock write. A depth counter prevents UI-tick preparation
while a matching load is active. The completion cookie balances that counter even
if selected identity changes; refreshing and configuration acknowledgement still
happen later through the ordinary fixture policy. Unrelated destinations and
rejected record versions do not invalidate the tracked instance.

`tests/patchkit/test_canopy_sound_load.py` passed two tests: unique anchor
resolution against the pinned MAIN image, and execution on the ColdFire core
through the real stock deserializer. Four component cases cover same-pointer,
same-type replacement, another destination, rejected record version, and nested
begin/end notifications. The probe observes the old destination marker when
suspension occurs and verifies preparation is blocked during the load. IRQ
helpers and the reset callback are component substitutes; real queue/reset
integration remains the responsibility of the booted fixture test.

This adds a concrete hook for one stock load path. It does not prove that browser
selection, kit reload, clipboard paste or every other Sound replacement goes
through that routine. A live UI-triggered in-place reload still needs a dedicated
acceptance scenario. No atomic multi-track or cross-task lifecycle guarantee is
claimed. Running-pattern initialization remains open: the existing pattern
finding identifies `0x4316501c` as a scheduled pointer that can lead playback, so
polling that pointer is insufficient to establish committed-boundary timing.

The existing booted carrier regression also passed with this hook installed
(182.07 seconds). Its keyboard/release, machine-change and stopped-pattern
re-admission assertions remain unchanged; this is coexistence evidence, not a
UI-triggered in-place load test. The fixture image SHA-256 is
`705b33d2fc03155fb4101ba39e711824513f9d39a7cc66e6e908c1b0e5457447`.
Logs are retained as `out/canopy-sound-component.log` and
`out/canopy-sound-live.log`.


## Panel-driven temporary reload [D]

A new booted DT2 OS 1.17 acceptance test selects the fixture's WERP shadow,
temporarily saves the pattern with FUNC+YES, holds USB MIDI C, then invokes
FUNC+NO. It uses panel input for saving/loading and no direct Sound-loader call.
The emulator has no sample card. The fixture image is unchanged from the Sound
lifetime proof above.

The first run passed in 114.28 seconds. The Sound pointer remained `0x426532ec`,
the observed load count advanced from 0 to 1, the generation advanced from 2 to
3, and the stock refresh count advanced from 1 to 2. The previous held voice
retired; a fresh C survived its predecessor's delayed note-off, verified by a
following G in the same ordered USB stream. The fresh voices produced finite
final PCM above -80 dBFS RMS and both retired after their own note-offs. This
supersedes the untested UI-triggered reload limitation for this specific command.
It does not cover browser selection, kit reload, paste, running-pattern commits,
sample-card operation or hardware.

The scenario is `tests/regression/runtime/canopy-reload.test.ts`, included in the
`canopy-carrier` Vitest project and excluded from the general runtime sweep.
State, screenshots and PCM statistics are stored under `out/canopy-reload-*`.
The temporary save/reload key combination is documented in Elektron's
[Digitakt II manual](https://www.elektron.se/wp-content/uploads/2025/06/Digitakt-2-User-Manual_ENG_OS1.15_250625.pdf),
section 10.8.6; the execution evidence here is from OS 1.17.

The expanded final-output run passed in 122.18 seconds. After re-admission, the
24,384-sample held-note window measured RMS 0.03756678 and peak 0.08387909, with
no non-finite samples. After both note-offs and a quarter-second drain, a separate
4,480-sample window had zero RMS and peak. The test requires only values below
-120 dBFS, not bit-exact zero, and reports no transport or emulator faults. This
proves settled output quietness; it does not establish click-free transitions.


## Running pattern admission waits for commitment [D]

The first booted running-switch test failed the source-ownership assertion:
CANOPY advanced generation before the destination's timed boundary committed.
The fixture had treated `pp_current_pattern()` as an immediately active pattern,
even though finding 76 identifies that value as scheduled ahead of playback.

Preparation now snapshots scheduled identity, committed identity, flags and
transport together with interrupts masked. During valid running lookahead it
retains an already-prepared source without reading destination Sound/kit data.
If the source was already invalidated by a Sound load, it stays unprepared until
scheduled and committed identities agree. Missing valid running-position data
invalidates preparation and closes routes rather than authorizing a refresh.
Stopped selection keeps its existing immediate behavior. The Sound deserializer
still takes priority: mutating the tracked object can close ownership before a
boundary, so this is not a promise to sustain notes through every load path.

The expanded ColdFire component proof passes: four existing Sound-loader cases
plus four preparation cases covering retained source, already-invalidated source,
invalid running position, and resolution after commitment. The first two prove
that preparation does not even consult the early project/Sound accessor.

The booted `canopy-pattern.test.ts` passed in 137.81 seconds with fixture SHA-256
`af6bb0f505ec3ed0a989a4de582ab7a8b78c4233bfdbca9916e4c45e761b9cbd`.
It prepares WERP on two patterns, starts internal-clock playback, holds USB C,
and queues a switch. The trace contains 144 coherent source samples, one
scheduled-destination/committed-source sample, and one committed-destination
sample, all still generation 5. The following polls confirm generation 6, empty
old banks and ready configuration. After STOP, a new C survives its predecessor's
release; a following G acts as the ordered USB barrier, and both fresh voices
retire after their releases. No transport or emulator fault is reported.

Trace samples spanning a changed boundary sequence are discarded. This is
ownership-order evidence, not a sample-accurate reset timestamp or PCM continuity
proof. UI polling and reset/configuration acknowledgements can delay re-admission
past the boundary. Invalid position during initial PLAY may reset the previous
instance; the test establishes its source generation after valid running
position and readiness. Pause/resume, external clock, rapid/cancelled switches,
other track layouts, in-place destination loads and hardware remain unproven.
Logs and the trace remain under `out/canopy-pattern-*`.


## Running pattern final-output measurements [D]

The running-switch regression now records raw 48 kHz stereo f32le from before
queuing the destination through settled reset, and checks separate source,
reset, fresh-destination and final-release PCM windows. The fixture image is
unchanged from the committed-boundary proof above. Source and destination RMS
must exceed 1e-4; reset and release RMS/peak must fall below 1e-6 after a
24,000-scalar-sample drain and at least 4,096 separate measurement samples.
All measured samples must be finite.

The first expanded run exposed a test race: a boundary-spanning snapshot was
excluded from the trace but still terminated the polling loop. The loop now
terminates only on an accepted coherent destination snapshot. This fixes test
observation, not firmware behavior.

The corrected regression passed in 179.68 seconds (175.58-second test body),
with no transport or emulator fault. Source RMS was 0.0264922862; the final
release window contained 4,416 scalar samples with zero RMS and peak.

The transition capture contains 92,640 stereo frames (1.93 seconds), peak
0.0419592112 and no non-finite samples. The maximum same-channel adjacent-frame
step is 0.0014368943 at frame 65,282. The reset window contains 4,416 scalar
samples, RMS 9.24584e-9 and peak 1.62981e-8. Fresh destination C/G produce RMS
0.0375176330 and peak 0.0838790983 over 24,448 scalar samples.

Capture and metrics remain under `out/canopy-pattern-*`. The maximum-step
measurement has no click-free acceptance threshold and is not localized to an
exact firmware boundary. Audio and ownership snapshots are not sample-aligned;
this does not establish absence of premature silence, reset-ramp length at the
final output, phase continuity, or hardware behavior. Pattern changes still
retire held notes and require fresh presses in the destination.


## Low-first/high-second final-output comparison [D]

The booted carrier scenario now starts USB C at velocity 24 from a quiet carrier,
waits 0.25 seconds of output, then adds G at velocity 127. It measures the held
mixture, releases C and waits for that token to retire, measures the remaining G,
then releases G and verifies quietness. A fresh velocity-127 G from silence is
the reference. Every held measurement follows a 0.25-second settling interval
and contains at least 24,000 scalar stereo samples. This exercises real MIDI,
stock carrier, gain and final-output paths on the same destination Sound.

The G-alone RMS ratio must lie within 0.98–1.02 of the fresh reference; the mixed
ratio must lie within 0.95–1.10 to accommodate low C's added energy and window
beating. Both bounds would reject a loud G scaled by the first key's 24/127
velocity. Ratios compare final RMS, not isolated spectral amplitudes. They do not
prove phase continuity, per-sample gain invariance, arbitrary stock settings,
other velocity curves or hardware behavior. The fixture still lacks pedal
handling and public V2 registration.

The extended test passed in 303.18 seconds (299.80-second body), with the unchanged
fixture SHA-256 `af6bb0f505ec3ed0a989a4de582ab7a8b78c4233bfdbca9916e4c45e761b9cbd`.
Each held window contains 24,448 scalar samples. Mixed RMS is 0.0384029233;
G after C retires is 0.0377035737; fresh G is 0.0377043753. The ratios are
0.9999787403 (after-low/reference) and 1.0185269755 (mixed/reference).
The before-reference and final-release quiet windows each contain 4,416 scalar
samples with zero RMS and peak. All windows are finite; the scenario reports no
transport or emulator fault. Existing machine/stopped-pattern re-admission and
stale-release checks also pass in this run. Evidence is saved as
`out/canopy-carrier-{low-high-mixed,high-after-low,high-reference}-pcm.json`,
`out/canopy-carrier-velocity-ratios.json` and `out/canopy-velocity-test.log`.


## Sustain ownership in the sine callback [D]

The CANOPY callback now handles CC64 and CC121 per MIDI port/channel. Each track
has a 64-byte pedal record: 32 down bits and four deferred-token/route pairs,
with reserved padding. Note-off under an active pedal records only the current
logical foreground owner (a pending replacement takes precedence over its
fading predecessor). Pedal-up releases matching deferred tokens; physically held
notes are unaffected. Stolen tokens remain harmless because release matching is
exact. A separate 64-byte scratch record commits with the voice bank only after
the whole event batch succeeds. No voice-kernel layout or MIDI wire format changes.
CC64 uses the standard 64 threshold; CC121 clears the matching route's pedal.
Track resets clear their pedal record; a newer stream epoch clears every track.

Nine SHARC instruction-core tests pass, including prior note/atomicity/burst
coverage and new threshold, port/channel isolation (including port 1/channel 15),
rejected pedal-up/down, pending replacement, stolen same-pitch note, track-local
reset and whole-epoch reset cases. The first test iteration incorrectly assumed
allocation started at slot zero; assertions now locate voices by token, matching
the kernel's free-slot selection policy. All 62 carrier component checks also
pass with the expanded callback state.

The booted `canopy-reload.test.ts` now follows its reload proof with a physically
released USB C under sustain. A DIN G releases normally while USB C remains held.
After another 120,000 scalar samples (1.25 seconds), a fresh 4,480-sample window
contains only sustained C: RMS 0.0295749351, peak 0.0419591852, no non-finite samples.
It then adds a physically held USB G and lowers CC64 to 63; C retires while G
remains held. G's physical release retires the last voice. The final 4,416-sample
window has zero RMS and peak, with no non-finite samples. The regression passes
in 211.12 seconds (207.33-second body), including the earlier in-place reload and
stale-release checks, with no transport or emulator fault.

The updated fixture has SHA-256
`9d75c484c2aaa63180a0937a078e8f4346974e6893212d8763a05338e854c8d6`,
1,934,624 bytes and 39 patch writes. Evidence remains under
`out/canopy-pedal-*` and `out/canopy-reload-{sustained,pedal-quiet}-pcm.json`.
This is still a WERP-shadow emulator fixture, not an installable CANOPY machine.
Pitch bend, CC120/CC123 handling, public controller capability negotiation,
compatible-pattern note retention and hardware sustain testing remain open.


## Route-owned All Notes Off and All Sound Off [D]

The callback now accepts CC123 and CC120. CC123 applies normal note release to
its port/channel, deferring under that route's sustain pedal. CC120 clears that
route's pedal/deferred records, cancels its pending onsets, and marks all its
sounding voices (including tails) for the existing at-most-64-sample emergency
ramp. Neither control resets the instance or changes other routes.

The controller record grows from 64 to 128 bytes per track, plus one 128-byte
scratch record. Ten token/route pairs cover the kernel's six sounding slots and
four pending replacements. After a successful onset, expired entries are pruned
against actual bank tokens and the new token is inserted. All metadata commits
with the voice bank; exhaustion fails the batch closed. The event wire format,
voice-bank layout and oscillator count are unchanged. Track/epoch resets clear
the expanded metadata. The CF ledger's existing physical-release tombstones are
retained; no new CF panic shortcut was added.

All 13 callback component tests pass. New cases cover sustain-aware CC123,
port 1/channel 15 isolation, panic with both tails and pending replacements,
fresh release after pedal clearing, and rejected-batch rollback of panic and
owner insertion. Existing eight-onset burst and reset tests still pass. All 62 carrier component
checks also pass with the expanded controller state.

The new booted `canopy-panic.test.ts` passes in 97.82 seconds (93.89-second body).
USB CC123 under sustain preserves C; USB CC120 removes USB C/E while DIN G stays
held. A fresh same-pitch C survives the old physical C/E releases, with an A
onset as an ordered USB barrier. A second USB CC123 releases fresh C/A normally,
proving panic cleared sustain. DIN G remains audible over 24,448 scalar samples:
RMS 0.0295228973, peak 0.0419733338, all finite. DIN CC120 then retires the last
voice; after a quarter-second drain, 4,416 scalar samples measure RMS 5.42948e-9
and peak 9.77889e-9, below -120 dBFS. Generation is unchanged, readiness remains
active, and no transport/emulator fault is reported.

Fixture SHA-256 is
`55534a01d0f66f57b7e3b9d3b3bf166c61db86757f8e4c012926e782f3ba4aa3`,
1,935,008 bytes, 39 writes. Evidence is under `out/canopy-panic-*`.
The 64-sample bound is a kernel/component property; the live test proves settled
final quietness, not a sample-aligned final-output stop bound. Pitch bend,
public V2 registration, compatible-pattern retention, latch/arp ownership and
hardware testing remain open. The current sine fixture has no latch or arp to
clear; those future owners must join the route panic transaction.


## Phase-preserving route pitch bend [D]

The callback now accepts 14-bit pitch bend with a fixed two-semitone range:
semitones = (value - 8192) / 4096. Center is exact; the maximum positive message
is one 14-bit unit short of +2 semitones. Each port/channel retains its bend for
future notes. CC121 centers bend and releases pedal-only notes on that route;
CC120 leaves pitch bend unchanged. Track/epoch reset centers the stored bends.

Controller state grows to 256 bytes per track plus one 256-byte staged copy.
Owner metadata packs route index and base MIDI note beside each token; 32 signed
bend words fill the added space. The voice bank remains 256 bytes. Its pending
record's previously reserved fourth word now holds signed bend, initialized to
zero on allocation and applied before the pending note's first sample.

Bend recomputes oscillator rotation from the original note and current bend,
without modifying phase, envelope, gate or gain. A float32 base-angle table and
small-angle polynomial rotation avoid cumulative retuning drift. Center restores
the original tuning coefficients exactly. Sounding tails and pending replacements
are updated alongside foreground voices. Rejected batches publish neither bend
state nor changed coefficients. This does not add smoothing or a configurable
bend range, and it does not apply stock TUNE a second time.

Component checks cover MIDI notes 0 and 127, 14-bit endpoints and one-unit bends,
error below 0.02 cents in the sampled cases, exact phase/envelope/gain preservation,
other-route isolation, CC121 restoration, bend before note-on, failed-batch
rollback, late bends of queued replacements and emergency tails, and reset.
All 23 callback tests pass, including a full eight-message bend batch with all
ten sounding/pending token slots occupied. This stress case uses a 30,000-step
instruction-harness ceiling; it is not a real-time CPU-budget acceptance result.
All eight standalone voice tests and 62 carrier component tests also pass.

The new booted `canopy-bend.test.ts` passed in 156.99 seconds (153.05-second body).
Positive left-channel zero crossings, linearly interpolated in raw stereo f32le,
measure held USB A4 at 440.000015 Hz, maximum bend at 493.876333 Hz (target
493.876337), and 493.876334 Hz after an unrelated DIN downward bend. USB CC121
restores 440.000006 Hz. Each capture follows 0.25 seconds of settling and includes
at least 0.25 seconds of audio. All samples are finite and audible, the same
note token survives the changes, and physical release retires it. No transport
or emulator fault is reported. This measures settled final pitch; exact phase
preservation is component evidence, not a final-output transition measurement.

Fixture SHA-256 is
`ae5f9a7fc9e5e8e50dbcb05475fcae25fdb7b4da06eeb2aeb9a12e3fc038a65e`,
1,935,648 bytes, 39 writes. Captures and reports remain under `out/canopy-bend-*`.
This remains an emulator-only sine fixture. Public V2 registration, compatible
pattern retention, chord/pad/arp modes, CPU-budget acceptance and hardware tests
remain open.


## Compatible pattern retention [D]

The fixture now distinguishes a pattern transition from replacing the instrument.
When track 0 is already admitted, both old/new Sounds are type 1, both patterns
are nonzero and differ, and the project is unchanged, it refreshes the destination
stock carrier without changing the MIDI generation or CANOPY configuration.
Running switches still wait until selected and committed pattern identities agree.
Machine/project changes, invalid timing and tracked in-place Sound loads retain
the reset path. This is limited to the fixed sine configuration in the WERP shadow;
it is not the public V2 registration or a general preset migration policy.

The ColdFire admission probe now exercises compatible stopped/running switches,
incompatible machine and project switches, plus the existing lookahead, unknown
timing and real stock-deserializer cases. Both Python tests pass, including all
12 processor calls. The synthetic getter/vtable only supplies a RAM Sound; the
admission function itself is the patched C code.

The first booted running-switch test passed in 212.59 seconds. A held USB C4 keeps
its original token and generation across the committed switch and remains audible.
Its original physical key-up retires it after the switch; a fresh C/G pair also
plays and releases to final quietness. No transport/emulator faults were reported.
The 87,744-frame transition capture has peak 0.04195921 and maximum adjacent-frame
same-channel step 0.001436894. Left-channel RMS across complete 1,024-frame windows
ranges from 0.02946210 to 0.02987585: no silent window in that capture. Destination
RMS is 0.02967367. These are default-Sound, internal-clock emulator measurements;
they do not prove arbitrary filter/FX settings, external clock or hardware.

The regression now asserts audible energy in every complete 1,024-frame window
and bounds adjacent-frame steps below 0.05 times peak for this settled C4 fixture
(the sine phase increment is about 0.0343 radians/frame). This catches gross gaps
and jumps; it is not sample-aligned proof of phase invariance for general audio.
Fixture SHA-256: `162753af4f781ebbf00a962746e36ccac9f9a35758f16024fadbf32af755b90b`,
1,935,776 bytes, 39 writes. Logs are under `out/canopy-retain-*`; transition PCM and
measurements are under `out/canopy-pattern-*`. Public V2 packaging and the remaining
stock control/lifetime acceptance are still required before an installable machine.

The final strengthened pattern regression and existing temporary-reload/sustain
regression both pass (403.04 seconds together; 201.32- and 196.70-second bodies).
This confirms that retention did not disable explicit same-pointer reload resets.
Changed-file checks pass lint, types and layers; formatting passes after applying
Prettier. The repository-wide path checker still reports the existing 256 stale
references in 78 files. Targeted checks of the three changed documents report zero
stale references. No full merge gate or device test was run.

## DSP reset and explicit boot initialization (DTII 1.17) [D]

Local stock firmware SHA-256:
`26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5`.
The extraction marker matches that file. MAIN OS section 3 loads at
`0x40000400`; disassembly below uses that base, not `0x40000000`.

The boot routine at `0x400cd0d8` calls `0x400cd01c` with argument 1 at
`0x400cd0e4`, before peripheral setup, allocation and section-7 decompression.
It calls the same helper with argument 0 at `0x400cd37c`, then delays before
sending command byte 3 plus the depacked stream. The helper branches on the
board byte at `0x402a1b58`. On one branch the nonzero argument writes `0xfd`
to `0xec09402c`, while zero writes bit 1 to `0xec094020`; on the other it
writes `0x7f` to `0xec094029` or bit 7 to `0xec09401d`. Both configure the
corresponding GPIO direction. These assert/release sites agree with the boot
sequence recorded in finding 06. Electrical reset behavior is not measured here.

A bounded reference scan of `0x40000400..0x40140000` finds those two direct
calls to the helper (99.95% decoded byte coverage). This does not rule out
indirect calls, computed addresses, other GPIO writes or references outside the
scan. Logs: `out/canopy-reset-control-disasm.log`,
`out/canopy-reset-entry-disasm.log`, `out/canopy-reset-stream-disasm.log` and
`out/canopy-reset-control-refs.json`.

The patcher integration test in
`tests/patchkit/test_instrument_boot.py` builds a fresh registry firmware and
reads its actual section-7 boot stream using the independent Python loader
reader. It requires explicit zero coverage for the session latch, sequence,
lifecycle enable, pending operation, complete command mailbox, registry ACK,
all eight registry rows, MIDI epoch/message and heartbeat. An uninitialized BSS
reservation cannot satisfy the check. Scratch written before use is excluded.
The test passes (`out/canopy-boot-initialization-test.log`).

This closes the image-content part of restart initialization, not the complete
reset gate. Next integration must revoke CF trust before reset assertion, cover
pending physical writes and establish whether the callback can run before the
runtime has initialized its state. Hooking this early routine directly to a
UI-only coordinator is not justified. A live DSP reset test must establish that
the firmware actually reloads the patched stream and that no old transfer or
MIDI route resumes. Production V2 admission remains disabled.
