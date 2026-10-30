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
