# CANOPY Stage 4 completion and evidence history

Current status: **Stage 4 is complete at the functional emulator boundary.**
The final section records artifacts, acceptance and remaining Stage 5/6 work.
Earlier entries below are the chronological implementation history.

Requested: complete Stage 4 after Stage 3 commit `ba23c3f0`.
**Historical checkpoint (incomplete at that time).** This checkpoint adds the bounded C movement policy
and its component scenarios. It does not change the installed device, Stage 3
firmware, module manifest or packaged DSP. No new agents were delegated.

## Implemented policy

`modules/machines/canopy/src/movement.h` and `movement.c` expose a serialized,
allocation-free block scheduler. This code currently has no runtime caller.
One tick is 32 samples at 48 kHz. It consumes four admitted notes, not the
physical keyboard ledger. Each note retains velocity, port and channel.
Revision and explicit new-phrase identities are independent of the clock's
transport generation and committed pattern identity.

- UP, DOWN, endpoint-deduplicated UP/DOWN, AS PLAYED and seeded xorshift RANDOM.
- One to three octaves; unplayable octave extensions are omitted, never wrapped.
- Independent monotonic arp token domain, bounded gates and exhaustion handling.
- Generated-chord strum spreads onsets over at most 750 blocks / 500 ms. Every
  chord revision cancels pending old strums; already sounding common notes keep
  identity. PLAY onsets remain immediate.
- TRIG responds to new phrase, PATTERN to committed pattern identity, CONTINUE
  preserves ordinal/phase across membership changes. Transport generation
  always resets the clock. PANIC releases both lanes and cancels pending work.
- LOCAL has 30..300 BPM, 1/2/3/4/6/8 notes per quarter and 0..75 percent swing.
  Rational phase retains the remainder across triplets; alternating thresholds
  preserve pair duration. Live swing changes apply at a pair boundary. Tempo
  changes preserve phase. Clock/rate changes explicitly restart timing.
- MASTER accepts only committed boundaries and 1..16-step multiples, with no
  swing or inferred subdivisions. Missing/invalid frames pause scheduling;
  no local fallback occurs. Recovery waits for a new boundary, not the last
  cached step. STOP cancels pending strums and synchronized arp, retaining pads.
- Invalid configuration or input is rejected before mutation. The caller must
  fault/recover on skipped audio-block calls rather than trying to catch up.

The output bound is ten changes, with all releases before all onsets. A full
four-pad replacement plus arp transition exercises that bound. The PAD and ARP
token domains are explicitly separate; never feed arp tokens into the MIDI
physical ledger. The consumer must reserve the complete output batch before
advancing policy state.

## Evidence

`tests/regression/host/canopy-movement.test.ts` drives the actual C policy through
`canopy-movement.c`. Thirty cases cover ordering, octave clipping, route/velocity,
revisions, new phrases, pattern/transport reset, STOP/PANIC, live mix-disable,
strum, source switches, stale clock, invalid updates, counter wrap and exhaustion.
Triplet and swing comparisons run for 200000 blocks (133.33 seconds) against
absolute rational timing. Gate events cannot release the pad lane. Full-batch
canaries detect output overflow. Compilation enables undefined-behavior checks.

The policy also compiles with `m68k-elf-gcc -mcpu=54418 -O2 -ffreestanding
-fno-builtin -Wall -Wextra -Werror`, with no unresolved symbols. This is a CF
compilation check, not a CF instruction or DSP timing measurement.

Logs: `out/canopy-stage4-movement.log`, `out/canopy-stage4-types.log`.
The Stage 3 handover remains the source of current playable firmware/artifacts.

Required changed-file preflight passes all 30 selected host cases, lint,
formatting, types and layer checks. It exits with the existing global pathcheck
baseline: 256 stale references in 78 files. Scoped checks of all three changed
documents report zero stale references (`out/canopy-stage4-docs.log`). Full
preflight log: `out/canopy-stage4-changed.log`. No merge gate was run.

## Required next work

1. Integrate scheduling with the audio callback, preferably inside the DSP
   instrument so the ten internal changes do not overflow the eight-event
   external MIDI contract. The C policy supplies executable behavior for that
   implementation; its host tests alone are not Stage 4 acceptance. No UI-task
   clock or ordinary ACK-delayed MIDI queue can stand in for audio timing.
2. Carry the mapper's committed membership, generated/PLAY mode and explicit
   phrase identity coherently. Preserve immutable packet retries and common
   sounding voices. Do not infer arp membership from audio release tails.
3. Add the seventh (arp) group with shared release-tail allocation, smooth
   PAD/ARP audio blending, reset/fault handling and bounded per-block work.
   Merely adding a second independent full voice bank violates the pool design.
4. Expose movement controls through applied configuration revisions and the
   setup menu. Named SRC pages remain Stage 5.
5. Implement/measure the synchronized tempo/phase extension on internal and
   external clock. Pattern-position ABI 2 has no verified tempo, fine divisions,
   per-track swing or sample timestamp. The C MASTER first gate is provisionally
   one block until an observed interval exists, and its eight-second stale bound
   is unqualified. Fix these before offering musical synchronized playback.
6. Execute protected DSP ownership/guard/headroom/instruction proofs and actual
   running-firmware arp/strum/transport tests, including clock loss, recovery,
   tempo changes and existing Stage 1ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“3 keyboard/sound regressions. Do not mark
   Stage 4 complete until these pass. Hardware/coexistence remains Stage 6.

## DSP primitive checkpoint

The next implementation adds `_cp_start_one`, `_cp_release_one` and
`_cp_render_one` to the production SHARC voice kernel. A separate arp group can
use exactly one 32-byte slot; the single-group renderer never reads the four
pad pending records. Starting over an occupied slot is rejected, and releases
match its token. Shared-tail allocation and scheduler invocation remain open.
The existing six-group pad entry keeps its behavior.

`src/dsp/blend.s`, included in the CANOPY module build, adds `_cp_blend`: a
32-sample linear PAD/ARP blend with a per-sample 20-ms smoother. Its explicit
8-byte state holds current/target mix. Invalid current or target (negative,
above one, infinity, NaN) is rejected before writing audio or state. It does
not own envelopes or gates and therefore cannot cut off keyboard ownership.
The caller remains responsible for audio finiteness validation. Neither this
helper nor the independent arp entry is invoked by the current V2 callback yet.

**[D]** All 97 cases in `test_canopy_voice.py` and `test_canopy_v2.py` pass,
including the new exact-slot guards, token matching, blend endpoints/headroom,
per-sample ramp and invalid-state cases, plus existing direct/protected pad
regressions. Runtime 165.04 seconds. Log:
`out/canopy-stage4-dsp-primitives.log`. One initial blend test run caught use of
an aliased float/integer loop register; the counter now uses R10 and the full
rerun passes.

The module compiles and both objects pass build-current checks; the DSP object
re-decodes 1251 instructions. Logs: `out/canopy-stage4-dsp-build.log` and
`out/canopy-stage4-dsp-current.log`. No firmware was patched, no DKM was packed,
and no physical device was changed by this checkpoint. Stage 4 remains
incomplete: integrate the scheduler, shared tails, coherent chord/configuration
snapshots and verified clock provider, then run firmware/audio acceptance.

The final blend snaps to its exact target below 1/32768 (-90 dB) to avoid a
residual lane from float32 smoothing stagnation. All 15 blend cases pass after
this change, including two new endpoint-settling cases
(`out/canopy-stage4-blend-final.log`). There are 99 unique voice/V2 cases across
the full 97-case run and this focused follow-up. The 20-ms envelope test is
unchanged; its final sample remains within 1e-6 of exp(-1).

The DSP checkpoint's required preflight passes layering and formatting, and
reports the same 256 global stale references. It also initially flagged three
missing explicit `zip(strict=...)` choices in the new tests; these were corrected
and focused Ruff checks plus all 15 blend tests pass. The full preflight log is
`out/canopy-stage4-dsp-changed.log`; changed-document pathcheck is clean in
`out/canopy-stage4-dsp-docs.log`.


## Local UP arp integration checkpoint

The production V2 callback now invokes `src/dsp/arp.s` and the smoothed
PAD/ARP blend. This supersedes the unused-helper status above. CPS4 has ten
words: version, family, color, drift, attack, release, mix, local BPM, notes
per quarter and gate percent. KEYBOARD setup exposes the four movement values
under LOCAL ARP - UP. Instance storage and admission policy are 640 bytes.
Legacy CPS1/CPS3 remain accepted. Configuration validates before applying.

The arp uses admitted foreground/pending pad owners, excludes the two release
tails and keeps its own seventh oscillator/envelope. Source ownership cancels
delayed onsets; the active source's bend retunes without resetting phase. Busy
arp replacement uses a <=64-sample ramp; shared-tail allocation is still open.
The rational local phase runs per 32-sample block, independently of PLAY/STOP.
BPM/rate updates retain phase. No master-clock mode or silent fallback is exposed.

[D] Component evidence: 93 passing V2/adapter cases plus one initially skipped
ColdFire keyboard case (`out/canopy-stage4-local-components.log`). That skipped
case passes with DIGIKIT_CF_SCRIPT set (`out/canopy-stage4-local-cf.log`). An
additional timing/guard run passes 12 cases, four overlapping the earlier run
(`out/canopy-stage4-local-timing.log`): 102 distinct component cases in total.
The cadence proof executes 600 consecutive DSP blocks at 137 BPM x 3 and
300 BPM x 8, directly and through the protected ABI, checking integer phase,
onset count and retained pad ownership. Declared process/reset ceilings remain
100000/1000 instructions; the measured cases peak at 23598/495. This is bounded
callback evidence, not a hardware throughput or performance comparison.
All 29 existing keyboard/settings host cases pass
(`out/canopy-stage4-local-host.log`); focused Python lint/format passes.

Separate emulator artifacts: `out/patch/canopy-stage4-local.syx` (1996192 bytes),
SHA256 f84342329909cb2ca53d758f8339d56339b2f5411e28e990af54f8ec8618ed3e;
`out/patch/canopy-stage4-local.dkm` (13620 bytes), full-file SHA256
3141befe6fb04c5cb3581ac7b611985c1da0efb8b0ad75a7ad85be8d8cd4d65e.
The packed payload hash is different from the full-file hash. New 640-byte
state requires the matching core policy image; do not pair this DKM with the
previous 544-byte Stage 3 core. No physical device was flashed.

Full Stage 4 remains incomplete: other orders/octaves, strum, explicit phrase
snapshots and restart modes, shared arp/pad tails, synchronized tempo/phase,
swing and the associated firmware/transport acceptance remain open. The C
movement policy is still a reference component, not the runtime implementation.

[D] Running-firmware acceptance passes in 479.59 seconds
(`out/canopy-stage4-local-live.log`). The isolated DT2 OS1.17 emulator boots the
matched image/module, selects CANOPY through the stock chooser, enables MIDI
through KEYBOARD setup, and applies ARP MIX through real encoder input. Held
60/64/67 produce exactly those three ascending candidates while retaining pad
ownership; full arp mix produces finite final stereo PCM (RMS 0.01661365, peak
0.03100340). Mix zero clears the arp while retaining pads; note-offs then clear
the pad voices and the adapter stays healthy. No DSP state writes fake playback.
Only stock MIDI receive-port settings are initialized as fixture data. The
first run timed out advancing mix with four-detent increments; the successful
run turns by the remaining distance and still waits for applied DSP values.
This proves local integration, not all Stage 4 timing or physical-device sound.

Required preflight passes Ruff, layers, 263 host tests (16 optional skips),
oxlint, Prettier and types. Its only failure is the unchanged repository-wide
pathcheck baseline: 256 stale references in 78 files. Scoped changed-document
pathcheck is clean (`out/canopy-stage4-local-docs.log`). Module and adapter
build-current checks pass (`out/canopy-stage4-local-current.log`).


## CPS5 production movement integration (current, supersedes local-UP checkpoint)

The DSP now ships all five orders, 1..3 octave expansion, local rational
swing, whole-master-step scheduling, phrase/pattern/continue restarts, generated
strum and shared arp/pad release tails. Per-instance storage is 768 bytes with
sixteen CPS5 controls. The setup menu exposes named choices; final SRC remains
Stage 5. The C movement implementation is still a reference, not a runtime caller.
CONTINUE keeps phase across empty chords. Delayed generated notes cancel on
chord replacement/release/STOP; PLAY keys remain immediate. No new voice bank
or heap allocation was introduced.

Optional process-input V3 retains the 72-byte V2 prefix and appends movement
metadata and opaque pattern identity. Event word six is still sample offset.
Mapper output carries explicit phrase identity plus generated/PLAY mode in a
new immutable MAPPED packet; a same-chord new phrase can commit metadata alone.
Physical MIDI sequence numbers and ownership remain separate. CPS1/3/4 input
configuration and V2 process input remain accepted.

[D] Current component evidence: the four-file regression run passed 271 cases;
its one failing ColdFire fixture lacked newly optional extern stubs. Adding the
four explicit stubs makes that case pass (272 cases across the two runs).
Logs: out/canopy-stage4-regressions.log and out/canopy-stage4-ingress.log.
The final CONTINUE change passes 40 movement/master/continue cases. The final
mapped-packet rejection and instruction-limit selection passes 15 cases.
Logs: out/canopy-stage4-continue.log and out/canopy-stage4-final-boundaries.log.
The declared process/reset ceilings remain 100000/1000 instructions. These
are callback bounds, not hardware throughput measurements. The mapper/settings
suite passes 31 cases, including immutable retries and same-chord phrase commits.
Generated strum/phrase tests pass 17 cases before the independent CONTINUE fix.

The matching firmware is out/patch/canopy-stage4.syx, SHA256
720fe148cf348a855df26d32a82a8cc8e39cc21c83c194f269e556c89eaed5a7.
The module is out/patch/canopy-stage4.dkm. Do not pair it with the older 640-byte
admission-policy image. All three changed core adapters were explicitly rebuilt
before patching: patchkit patch alone reuses tracked DKO objects.
No physical device has been flashed or changed by this work.

[O] Stage 4 is not complete. Local UP audio and DOWN/octave setup passed in the
running emulator. MASTER produced no measured interval after PLAY in the first
full test; the reordered diagnostic run is investigating this. Separate paced
USB-clock delivery produces 126-block step intervals before the emulator stops
with ColdFire exception vector 5 (divide by zero). The fault also occurs with
no CANOPY DKM loaded, so it is not evidence that the CANOPY DSP caused it.
Do not disable exception checks or claim external-clock acceptance. Diagnostics
are in out/canopy-stage4-clock-fault.json and the clock/live logs. Fine synchronized
subdivisions, swing, clock-loss/recovery and final running acceptance remain open.


The reordered firmware diagnostic localized the internal MASTER failure: its
input has valid running flags/boundaries, but the old admission policy reset
the held chord during START's brief pre-commit timing gap (generation 3 to 4;
all foreground tokens cleared). A bounded START allowance now retains only an
already admitted identical project/kit/Sound/pattern/machine, for at most 64
CF audio blocks. It never admits a new instance, expires closed, and does not
permit invalid timing after the first valid boundary. Scheduled destination
getters remain deferred until commit. Four native production-policy cases pass
(out/canopy-stage4-start-hold.log), including expiry and unsigned wrap.
The firmware run against the START fix is still being qualified.

The external-clock exception reproduces via DIN as well as USB, with no CANOPY
DKM loaded. DIN reaches 125-block intervals before exception vector 5. Logs:
out/canopy-stage4-clock-din.log and out/canopy-stage4-clock-health.json.
Do not conflate this exception with the separately diagnosed START admission bug.
The required preflight passes 265 host cases (16 optional skips), lint, format,
types and layers; only the unchanged 256-stale-reference/78-file pathcheck baseline
fails. Scoped changed-document pathcheck passes. These results precede the START
policy addition, whose focused four cases pass separately.


## Stage 4 clock and strum qualification, 2026-11-01

[D] The final START-admission firmware (out/patch/canopy-stage4-final.syx,
SHA256 2db02faa062a3ab1b4c786caf6ef78de3a87594511e1d88ba2296c13e11eb0db)
passed MASTER onset/gate/final PCM, STOP/restart without clearing the held chord,
LOCAL UP final PCM, and DOWN/octave controls. That combined run then stopped
at the strum test's first encoder packet. The isolated strum run now passes
100 ms generated-chord delay, release cancellation, and no delayed resurrection
(out/canopy-stage4-strum-filter.log). The stock encoder idle filter can consume
an initial turn; the corrected test repeats turns until the applied value
matches instead of requiring every packet to change a value.

[D] The external-clock divide-by-zero is localized, with no CANOPY DKM loaded:
DT2 1.17 instruction 0x4011f052 divides by the 24-pulse timestamp sum shifted
right by nine. That sum was zero because DTCN0 at 0xfc07000c was unmodelled.
Exception diagnostics now expose read-only ColdFire registers; the original PC
is the exception frame at A7+4, not the handler PC. Artifact:
out/canopy-stage4-clock-fault-registers.json.

[D] Passive physical DTII reads of the firmware's accumulated DTCN timestamp
at 0x42940a4c confirm an advancing approximately 132 MHz clock. Sixty reads over
about twelve seconds, without playback, firmware, settings or module changes,
are saved in out/canopy-stage4-hardware-timestamp.json. The sparse refresh and
200 ms polling give an endpoint estimate of 131.606 MHz, consistent with the
132 MHz bus clock; this is not a measurement of the DTMR register. MAIN has no
DTMR0 initializer. Emulation supplies the minimal inferred bootloader state:
DTIM0 bus/1, no prescaler, free-running, interrupts disabled.

The optional counter model advances from guest instruction time, independent
of IRQ scheduling; disabled trace lanes retain register-only semantics. Bus
clock/prescale, reset, counter-write clearing, stop/resume, reference restart,
wrap, instruction-rate rescaling and snapshot fraction/epoch are covered.
External-pin/extended clock modes remain unmodelled. Counter state is saved in
DT2SNP02; DT2SNP01 loads retain their legacy counter semantics. Fresh firmware
loads inherit the new counter. The memory-only snapshot reader accepts both.
Reference: NXP MCF5441x Reference Manual chapter 39,
https://www.nxp.com/docs/en/reference-manual/MCF54418RM.pdf.

[D] Fourteen DTIM cases, three rate-rescaling cases, runtime counter-snapshot
round-trip, read-only fault diagnostics and both memory snapshot formats pass.
The bounded USB external-clock firmware proof now passes: internal 102 BPM
steps alternate 220/221 blocks; paced 180 BPM input yields eight consecutive
126-block steps, then MIDI STOP clears running/valid without an exception.
Logs: out/canopy-stage4-clock-bounded.log, out/canopy-stage4-timer-final.log,
out/canopy-stage4-timer-regression.log, out/canopy-stage4-timer-snapshot.log,
out/canopy-stage4-timer-diagnostics.log and out/canopy-stage4-snapread.log.

[O] Stage 4 still needs finer synchronized movement and tempo-change/loss
qualification. Static DT2 1.17 evidence: 0x4002f296 adds twice the tempo value
at 0x402b67fc to transport phase 0x80004880 each audio block. The tempo unit is
1/120 BPM (102 BPM reads 12240); this gives 21600000 phase ticks per quarter.
External-clock path 0x400d7bba aligns phase to the scheduler clock, then advances
the next pulse deadline by 900000 ticks (24 pulses per quarter). This arithmetic
is single-read evidence, not yet a live subdivision/phase alignment proof.
A wire-v3 phase experiment is in progress. It retains the existing wire size
and normalizes the public position record back to ABI 2 for Overloop; a separate
clock record has its own version. Do not claim fine-clock completion yet.


### Fine-clock integration and pulse freshness

[D] Input V4 (88 bytes) forwards the wire-v3 phase/deadline pair while keeping
instrument descriptor/reset ABI 2. Direct/protected triplet, swing, tempo,
wraparound, backward-correction, discontinuity and restart checks pass. The
matched component run passed 208 tests; subsequent pulse-freshness/adapter
checks passed 36. Logs: out/canopy-stage4-matched-components.log and
out/canopy-stage4-pulse-freshness.log. The live menu exposed a stale adapter
clock-enum validator; it now admits MASTER PHASE and permits its swing, while
still rejecting swing in MASTER GRID. The added atomic configuration check passes.

[D] Stock external clock can freewheel: dropping F8 without FC did not stop
committed boundaries. The old boundary-age-only expectation was incorrect.
OS1.17 0x4011eea8 reads DTCN0 and accepted clocks update last timestamp
0x44e086e4, sum 0x44e086d4, count 0x44e086dc. Clock receive is 0x429368d6;
0x40138f64 selects the highest bit in source mask 0x402b6800, with MIDI source
bit 1 set by 0x40138fce. The new instrument freshness policy latches an observed
external source while running and expires at three pulse periods (40..500 ms).
A subsequent stock fallback cannot silently restart the arp; STOP or receive OFF
clears the latch. Pulse return permits reobservation. Wire-only flag bit 2 marks
staleness; the receiver removes it and adjusts the checksum for legacy ABI 2,
while clearing the separate clock capability flags. Both master instrument
modes then silence without revoking pad ownership. CF policy tests cover wrap,
expiry, fallback latch, pulse return, bounded timeout and new internal run.

[O] Final acceptance runs are underway on firmware SHA256
0ed894b86c58ca237a053c9b5346dc1c9bada03c58677954bef003941eee84fc.
Do not call Stage 4 complete from component tests alone. Logs are
out/canopy-stage4-clock-qualified.log (DIN, scale/wrap and recovery) and
out/canopy-stage4-qualified-live.log (matched CANOPY, UI/audio and USB recovery).
No physical module, selection or firmware was changed during this work.


### Stage 4 completed: functional emulator acceptance

[D] The full production musical acceptance passes (1113.31 s total test time):
MASTER GRID and MASTER PHASE selected via KEYBOARD; synchronized triplets and
swing; finite final audio; STOP silence with retained keyboard ownership;
restart; USB pulse loss without STOP; recovery at slower tempo; explicit STOP;
LOCAL UP membership, DOWN/octave control and blend; generated-chord strum and
release cancellation. Log: out/canopy-stage4-qualified-live.log. Final captured
phase-mode audio RMS 0.08360847, peak 0.22527526; local arp RMS 0.02779781,
peak 0.06434289. These are workload observations, not worst-case hardware CPU
or all-settings headroom qualification.

[D] Final provider acceptance passes (286.02 s): internal 102 BPM master steps
220/221 blocks; double scale 110/111 blocks with seven-step wrap (1..6,0,1,2);
DIN 180 BPM 125 blocks; pulse expiry while legacy position stays valid; resumed
120 BPM 187/188 blocks and STOP. Phase advances exactly 24480 ticks per received
frame at internal 102 BPM in both normal and double-scale observations, proving
phase units independent of master scale. Log: out/canopy-stage4-clock-qualified.log;
data: out/canopy-stage4-clock-{internal,scaled,external,recovered,loss}.json.
No provider outputs or DSP musical state were fabricated. The scale/length
fixture edits stock pattern inputs, then restores them before DIN playback.

Final artifacts (SHA256):
- out/patch/canopy-stage4.dkm:
  0b090b3974bb86e7f6126525e72c966e590962091c71175ace6e504930186ed7
- out/patch/canopy-stage4-final.syx:
  02df04c612756c80e6e0c53117ebd970584c890cc8bba09ee778ad79c34c66e1
- Full musical run's out/patch/canopy-stage4-prewrap.syx:
  0ed894b86c58ca237a053c9b5346dc1c9bada03c58677954bef003941eee84fc

The final image adds only the persistent expired-clock latch to that musical
run's provider, preventing a full 32-bit timestamp wrap from briefly reviving
a lost clock. This final difference passes its CF-core regression plus the final
DIN/scale firmware proof; the full musical suite was not repeated for this
isolated latch. Both artifacts use the same CANOPY DKM and adapter logic.
The complete current source is build-checked separately. Never substitute the
older stage-4-local or 640-byte instance artifacts.

Validation: 208 component tests passed before pulse-expiry addition; 36 relevant
provider/adapter cases passed with expiry; final two targeted wrap/expired-input
checks passed. Native timestamp tests: 14 counter cases, snapshot round-trip,
read-only diagnostics and two snapshot-reader cases pass. Changed-file preflight
passes 269 host tests (16 optional skips), Python lint/format/types, JS lint,
format/types and layer check. Its only failure is the pre-existing repository
path baseline: 256 stale references in 78 files. Changed-document references
are checked separately. The final small fixture/test additions receive scoped
format/type checks, rather than rerunning unrelated host tests.

Stage 4 is complete at the functional emulator boundary. Hardware load,
USB/DIN keyboard latency, coexistence and release approval remain Stage 6.
No physical firmware/module/settings changes were made. Stage 5 (presets and
named SRC pages) is the next implementation stage.
