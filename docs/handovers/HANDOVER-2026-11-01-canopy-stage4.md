# CANOPY Stage 4 movement policy checkpoint

Requested: complete Stage 4 after Stage 3 commit `ba23c3f0`.
**Stage 4 is incomplete.** This checkpoint adds the bounded C movement policy
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
