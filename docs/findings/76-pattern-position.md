# Pattern position bridge (Digitakt II OS 1.17)

## Scope

**[D]** `core/pattern-position` exposes sequencer scheduling state to DSP modules through the required optional
runtime import `_pattern_position_info`. It is a patch-time provider for OS 1.17, with no DN2 port. The bridge is a
prerequisite for arming a loop at a selected pattern step. It does not change `digikit/overloop` controls or implement
quantized capture. Its record describes firmware state; sample-accurate alignment needs separate evidence.

Implementation and ABI: `modules/core/adapters/pattern-position/README.md`, `sdk/include/dk/pattern_position.h`,
`sdk/asm/dk_pattern_position.inc`. Firmware, extracted sections, emulator cards, traces and PCM remain under ignored
local paths. No instrument was flashed.

## Resolved stock state

**[V]** The locations below were checked against OS 1.17 bytes by an independent agent. Older sequencer notes describe
OS 1.16 and are not an address source for this patch.

| Input | SHA-256 |
| --- | --- |
| Stock OS 1.17 SysEx | `26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5` |
| MAIN, base `0x40000400` | `a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964` |
| DSP section | `0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2` |

| Location | Meaning used by this bridge |
| --- | --- |
| `0x43164f94` | 32-bit transport: 0 reset, 1 running, 2 paused |
| `0x43164f98` | 16-bit next scheduled master step |
| `0x43164f9a` | 8-bit substep |
| `0x43164f9c` | 16-bit current master step returned by getter `0x400d67fe` |
| `0x4316501c` | Opaque scheduled-pattern pointer; can change ahead of audible playback |
| `0x43165130` | Signed master wrap override; values greater than 1 take priority |
| scheduled pattern + `0x1db74` | Signed fallback wrap length; values at most 1 are reported as unknown |
| `0x43158f7c` | Raw 32-bit scheduler clock, not an audio sample count |
| `0x40966840` | ColdFire audio-block counter |

The getter's 30-byte window hashes to `0c400342e4f6780e77328eff69836796eb76129b2985ba05459dc721d4b0a`.
The length selection/wrap window at `0x400d8820` (50 bytes) hashes to
`8d68c36f75d8b8069f952b85aeaf4bc9742cd5ee254d011d5cbc541ee034ebd7`.
Effective wrap length is not a claim about every track length or the UI's M.LEN/CH.LEN semantics.

## Capture and delivery

**[V]** Named hook `pp.cf.start` at `0x400d66d8` observes successful start/resume. `pp.cf.tick` at `0x400d8dca`
observes the substep interrupt's tail. Both replace a countdown store inside an interrupt-masked region, replay that
store, preserve touched registers, and return to the stock SR restoration. Start/resume increments a separate generation;
both hooks increment the snapshot event sequence. That sequence counts snapshots, including substeps, not musical steps.

**[D]** `cf_window_hash` resolves each CF anchor from exactly one even-aligned instruction window without a Ghidra
function boundary or embedded vendor bytes. Ambiguous, missing and malformed rules fail closed. The port pins the full
image and the hook's six bytes. New CF anchors were reproduced through this resolver; the DSP anchor was checked from
the existing handler hash/offset. A complete all-anchor port regeneration was not run because local Ghidra data is absent.

**[D]** The optional `core/dsp-link` callback writes a 16-word snapshot on every audio-send frame, including idle code-link
frames. It reserves receive bytes `[0xa7c, 0xabc)`, reducing code-channel payload from 171 to 155 words on both sender and
receiver when the provider exists. With no provider the original capacity remains. The core checks DT2 geometry before
calling the writer, which writes only data halfwords in the PUSHR queue. Wire words use a big-endian low halfword followed by a big-endian high
halfword. The copied snapshot, refreshed transport, sequence and checksum are protected by a short interrupt mask.

**[V]** DSP entry `0x1c7501` is after the stock handler loads the receive-ring pointer into I3 and before its command
dispatch/render. The injected entry and return are plain jumps. The displaced I4 dispatch-table load runs in the tail.
Independent decoding of the prepared image confirms all 178 receiver instructions and no calls, returns, hardware loops,
or PC-stack/counter operations. The receiver preserves touched R/S, DAG, MODE1 and arithmetic-status registers. This is
static placement/context evidence, not hardware timing or a live effect-consumer observation.

**[D]** The 80-byte DSP record contains the 16 wire words plus receive/reject/accept counters and transmit delta. Bad
header, version, checksum or repeated/backward transmit sequence clears magic and validity; unsigned sequence wrap is
supported. Validity does not expire automatically. Reset/pause clears valid/running every audio frame; a stalled external
clock can leave a valid but old position, so consumers must inspect snapshot age and event progression. Pattern pointer,
start generation and current step are intentionally separate fields.

## Running-sequencer comparison

**[V]** The paired OS 1.17 emulator run passed in 505.898 seconds. Both images use the same runtime/framework and
private sine-module card; the second additionally includes the position provider. Panel input selects the sine machine
and programs trigs at steps 1, 5, 9 and 13. The baseline no-trig control produces no note-ons and exactly silent PCM.
The positive captures contain 11 note-ons spanning more than two pattern cycles, with identical relative frame spacing
and zero drift. All 241,184 stereo frames of the common capture are byte-identical at zero PCM offset; the hook capture
has one extra 32-sample block at its end. Audio is finite and audible (peak 0.06365344). No individual notes were realigned.

All 7,538 hook capture frames have valid running snapshots, monotonically advancing transmit sequence, and an exact
same-frame post-render DSP mirror with zero rejections. The reported master steps traverse 0 through 15 in order.
Snapshot age is at most seven CF blocks; it is one block at each tested note-on. The final 100 frames after STOP have
both running and valid cleared. `_fx_blk` remains zero because this proof loads no effect row. Thus the mirror is
observed after rendering; consumption from an active effect has not been dynamically established.

Evidence: `out/runs/pattern-position-firmware/report.json`, unique run
`position-e26192fd61f34b3893c9301d8bb08687`. Baseline image SHA-256 is
`dafb223013931038d8eecd84e9bba11fd669973069de2b0c24b7b184f4d33919`; hook image SHA-256 is
`6b6eb81ea333b16ecaaeaa36b9172a4d4ebb182cbe09d3bf8f584ffe63f72619`. This run uses `DIGI_INTC_FORCE=1`
and `SHARC_FAST_AUTO=256,64`. It is accelerated emulator evidence, not interpreter parity, hardware evidence or a
performance claim. An independent agent reviewed the report, trace and timing below.

**[D]** Correct step numbers at note time do **not** establish that the step-change edge is the audible boundary.
For all ten tested note-ons whose corresponding current-step edge is actually captured, that edge precedes the note-on
by 32 DSP blocks: 1,024 samples, or 21.333 ms at 48 kHz. For example, native step 4 changes at frame 10,933; the step-5
note is sent at 10,965. The native step-0 wrap at 13,183 precedes the step-1 note at 13,215 by the same interval.
The first record in a capture window is an initial value, not evidence of an edge.

Each of the 11 tested note-ons instead follows the observed same-step `substep=1` edge by one DSP block. These are
measurements of this internal-clock, 16-step, approximately 120 BPM case; neither 32 blocks nor substep 1 is a portable
capture rule. Sparse uniform trigs also leave phase-label ambiguity (the runner labels the first onset as step 1).
Next timing work should use an asymmetric trig sequence to disambiguate phase and an all-16-step sequence to reference
every boundary, then observe the bridge from inside the actual effect. Do not arm capture directly on word 7 changing.
The runner now includes descriptive edge-to-note timing diagnostics without making those offsets acceptance rules.
Reanalysis of the retained trace is stored separately as
`out/runs/pattern-position-firmware/position-e26192fd61f34b3893c9301d8bb08687.timing-reanalysis.json`, with evidence
hashes and the initial-value exclusion. It does not replace the original report or represent another firmware run.

## Validation and remaining work

**[D]** Focused tests execute both assembled CF hook paths and the DSP receiver, including register preservation,
hostile DAG lengths and PEYEN/ALUSAT modes, exact wire bounds, code-channel coexistence, invalid descriptors, counter
wrap, replay rejection and stopped transport. The firmware proof runner separately rejects truncated PCM/logs, stale or
missing DSP observations, reordered steps and a no-trig negative control. Its source is
`tools/patchkit/pk_pattern_position_proof.py`; relevant tests are the four `test_pattern_position_*.py` files plus
`tests/patchkit/test_dsplink.py` and `tests/patchkit/test_loop_storage.py`.

The initial focused run passed 115 tests with two missing-DN2-firmware skips. Its one failure was an older ring-toggle
anchor test requiring absent generated caches; that test now decodes hash-verified stock firmware directly and its DT2
case passes (DN2 still skips). Three later edge-timing regressions also pass: the final targeted run passes all 46
proof-runner cases and the DT2 ring-toggle anchor case, with one DN2 skip. Across those runs, 119 distinct focused
cases pass and two DN2 cases skip.

**[D]** The required `verify:pre` was attempted. It passed changed-file lint but found a formatting error in the new
DSP-link tests; that file and the later diagnostic edits were formatted and linted successfully. Global path checking
fails on 724 existing stale references in 120 files, and `pk_build build-all --check` stops at the missing pinned
ColdFire Rust toolchain. Framework routing also selected emulator scenarios and started cold emulator builds; the
already-failing broad run was bounded and its owned processes stopped, so its selected suite did not complete.
Logs and the stop record remain in `out/runs/pattern-position/`. This is not a clean full pre-check or merge gate.
Changed-file path checking finds no stale paths outside three pre-existing references in `docs/FINDINGS.md`; the new
index entry resolves. `git diff --check` passes.
All six changed compiled objects match fresh builds: three in `core/dsp-link`, two in the provider, and DT2 `core/dyndsp`.
The unchanged DN2 `core/dyndsp` object still differs only in toolchain metadata; a fresh in-memory comparison confirms
identical source hash, sections, symbols and relocations. That original object is preserved. Audit:
`out/runs/pattern-position/objects.json`.

**[O]** Selected-step capture, pattern-change timing, pause/resume playback semantics, external clock, swing/microtiming,
per-track scale/length, tempo changes and hardware load remain separate validation tasks. No quantization control should
be inferred from this experimental observation ABI.

## 2026-10-28: ABI 2 and armed capture integration

[D] Rebased onto upstream `5e62a52`. The provider now queues frozen master-step markers and matches existing timed type-7 PLAY / type-5 step dispatch deadlines. The DSP consumer arms on REC, captures from the next selected START boundary for LENGTH steps (0 = current master cycle), closes before the next excluded block, and holds until rearmed. START and LENGTH latch on arming. Usable stereo storage is 1,048,544 frames within the unchanged 8 MiB lease.

[D] The focused tests execute the new consumer on the SHARC core and the marker queue/packet writer on the ColdFire core. They cover ordered boundary progress, custom lengths across wraps, interrupts/context, packet rejection, incomplete-take invalidation and capacity bounds. These synthetic tests do not establish real firmware/audio alignment. The earlier paired results above describe ABI 1 only.

[D] Paired running firmware run `position-e3e8e4d4c652494cbbdd2b5a9a60b1b2` preserved 241,184 stereo frames bit for bit, with zero PCM offset and zero drift across 11 note events. All 7,535 valid running records were accepted. Tested committed step boundaries coincide with note events, including first PLAY; boundary-to-transmit age is one block. STOP clears valid/running. Report: `out/runs/position-v2/report.json`.

[D] Actual-effect run `armed-f18302802b0242be9e21b24a1dce49d6` used UI knobs without guest-memory writes. Default LENGTH 0 captured exactly 96,000 stereo frames (one 16-step cycle at 120 BPM), with every captured sample matching actual effect input. Held REC preserved the buffer after STOP. LENGTH 1 and START 4 captured 5,984 frames from step 5 to step 6, also exactly matching input. FADE reached the effect correctly. The block-quantized one-step duration is 187 blocks; capture includes the opening block and excludes the closing block. Report: `out/runs/armed-v2/report.json`. The proof reported success; its outer shell subsequently returned a syntax error, so that shell exit is not a passing check.

[D] Module conformance and consumer tests pass: 29 passed, one skipped. Separate bridge/anchor tests pass 36 cases; storage/link tests pass 27 with two missing-firmware skips. The packaged effect is 3,012 bytes. Providers now live under `modules/core/adapters`, with neutral `core/` IDs after the Overloop rename, and SDK headers follow the rebased layout.

[D] After that adapter migration, all 45 bridge/anchor/storage tests pass again. Regenerated declarations pass `gen --check`; changed Python files pass lint; the new provider, SDK and loop documentation paths resolve. Rebuilding the selected framework image produces the identical tested SHA-256 `67d3946c6653d0609f902013999c870b8e9c80a70c8e2a606a2c2461a8b75d6f`. Layer checking reports only five cached `test/c-demo` compiler-helper imports, outside these modules.

[O] These accelerated emulator proofs use the retained pre-rebase emulator binary and a 16-step, internal-clock 120 BPM pattern. They do not establish hardware behavior, current-emulator rebuild parity, non-default lengths/scales, swing or external clock. Variable tempo intervals and cross-wrap lengths are covered synthetically, not by live firmware. Broad changed-file preflight is not clean: it encountered repository path/layer findings, four devtools verify test failures, and unavailable pnpm. Focused checks are reported separately. No fixed 32-block correction is used.

## 2026-10-28: Overloop 0.3 live windows and destination controls

[D] The DSP now records up to 128 step boundaries in its 644-byte instance state. Playback START selects an offset on the captured source grid, wrapping within the capture; LENGTH selects an exact recorded-step window, capped at the complete recording. Zero means the whole recording. These controls update every block, while capture timing still latches on REC. The recorded boundary table preserves variable step durations and bounds every read to initialized audio. A complete take can close on a contiguous source-final-step to destination-step-zero transition; a pending arm is consumed instead of starting an unintended destination take.

[D] `core/loop-controls` runs before the runtime's FX-page polling through an optional ColdFire trampoline call. It identifies the loaded Overloop row by its packed effect ID, snapshots its source controls, waits for the destination's committed pattern identity, then writes the destination kit's START/LENGTH, REC=0 and FADE=127 when the source REC was enabled, otherwise FADE=0. It handles in-place kit loads and a load completing within the first destination step. While stopped, selection applies immediately. The immutable DSP import `_loop_controls_info` prevents Overloop 0.3 from loading against an image missing this adapter. No Rust-for-ColdFire rebuild is required.

[D] Forty focused tests passed: 16 ColdFire handoff combinations (both sides of the REC threshold, separate/in-place destination arrays, early/late loads), plus 24 SHARC capture/playback tests. They include unequal recorded step durations, live window changes, wrapped windows, incomplete-take rejection, capture closure on a switch, and consuming an unfulfilled arm. The final module conformance run passed 32 tests with one skip; packed size is 3,668 bytes and DSP memory demand is 2,816 bytes.

[O] The new handoff tests execute compiled code with synthetic kits and pattern identities. They do not prove stock firmware kit-load ordering, chained-pattern behavior or hardware/UI timing. The existing running-firmware audio evidence above predates this control handoff. The short existing gain ramp remains; a knob snap is not a sample-discontinuous audio gain jump.

[D] Both provider-import table checks pass after exporting `_loop_controls_info` through `core/dyndsp`. Final firmware SHA-256 is `0eeaf95e99deb69ae625501d9c394f49bf58e4b3a4dbb1b0a439593024eed55d`; preflight passes with only MAIN and DSP modified. The packaged module SHA-256 is `6f29fd58dce8c72daa0bea93911c8f6da0387a6077dcc29652c59977c9e04fbc`. Changed Python lint, layering, and changed-document path checks pass. Repository-wide path checking reports 256 stale references in 78 files outside this update; see `out/runs/overloop-03-changed.log` for the broader check results. This is not a full merge-gate pass.
