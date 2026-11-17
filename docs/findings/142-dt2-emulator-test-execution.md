# DT2 emulator test execution

DT2 OS 1.17, 2026-11-09. **[D]** The ceiling is the work this change does
not accelerate: reference ColdFire execution, generated-block fallbacks, startup,
firmware waits and the test's own protocol traffic. Reference ColdFire still
cost about 0.55 CPU seconds per audio second in the paired workload. A DSP speedup
is therefore not a whole-suite speedup or a hardware timing claim. CANOPY's
separate acceptance limits remain **100,000 SHARC instructions per 32-sample
callback** and **1,600 per reset**; neither limit nor polyphony changed.

## Observation and supported build [D]

The original compiled CANOPY UI run used a current generic generated core, with
no DT2 image-specific block library. Its read-only status reported 5,597,406,852
interpreted DSP instructions, 61,220,988 fast-tier instructions and zero AOT
instructions after 17.2 audio seconds: approximately 99% of non-idle DSP work
was interpreted. DSP worker cost was 24.35 CPU seconds per audio second. This
observation identified the bottleneck; it is not the paired timing baseline.
Adding test workers would not shorten this individual emulator's serial DSP
work.

The supported `tools.sharc_dev.aot_gen dt2 --card-image ...` path generated the
DT2 1.17 library from the stock firmware, a card-specific ready snapshot and two
profile cycles. The current, fingerprint-checked shared core bootstrapped the
first run. The recipe retained `--model-safe --explicit-memory-model 0 --chain`.
Its final output has 1,059 blocks, 10,739 block instructions and 103,289 decoded
instructions. No emulator instruction semantics or firmware bytes were edited.

The input card has a logical size of 24,750,587,904 bytes. `ready.py` previously
materialized the entire image with `read_bytes()` when calculating its content
key. It now uses `hashlib.file_digest` on an open binary stream, preserving the
same SHA-256 and cache selection. The regression rejects a whole-card read and
checks reuse of the existing content-keyed ready snapshot. The ready, launcher
and fingerprint selection passed 62 tests. The observed hashing process used
about 37 MB RSS.

The DT2-only daemon was then built with the existing audited owner command:

```sh
uv run python -m tools.devtools.verify_build daemons --device dt2 \
  --env-out out/canopy-dt2-aot-daemons.json
```

The final DT2 generated directory was selected explicitly; old daemon overrides
and the second-core override were unset. The content key remained stable during
the build and the Cargo dependency audit passed. This avoids the unrelated DN2
prerequisite without bypassing build verification. After the complete CANOPY
proof passed, the verified DT2 entry was promoted to the local ignored
`dist/daemons.json` for subsequent tests.

## Paired native result [D]

The supported `tools.emulator.coupled build --device dt2` built the comparison
runner. A bounded ignored driver called `tools.devtools.coupled_ab.artifact_run`
under one exclusive benchmark lock, alternating A/B, B/A, A/B. Both labels used
the same binary, stock firmware, card, snapshots and `dt2-sample-trig` scenario;
only `SHARC_AOT=0/1` changed. Other fast-tier settings stayed fixed, so the
baseline is **AOT off**, not a fully interpreted SHARC configuration. ColdFire
remained `Reference`, with zero softfloat ABI substitutions in all six runs.
Each replay requested 1.2 audio seconds with a 240-second wall bound.

| Median of three runs | AOT off | AOT on |
| --- | ---: | ---: |
| DSP CPU seconds / audio second | 24.312913 | 1.927297 |
| ColdFire CPU seconds / audio second | 0.591748 | 0.548590 |
| Audio seconds / replay wall second | 0.044770 | 0.563924 |
| Interpreted DSP instructions | 380,407,323 | 2,031,579 |
| AOT DSP instructions | 0 | 381,084,925 |
| Fast-tier DSP instructions | 4,305,659 | 1,596,478 |

The paired DSP CPU ratios, AOT on/off, were 0.07978350, 0.07936948 and
0.07927049. Their median is **0.07936948**, or approximately **12.60 times faster
for the measured DSP work**. Host retired-instruction and cycle counters were
unavailable (reported as zero), so this finding makes no host instruction-count
claim. Guest tier counts were identical across repetitions of each label.

Every replay produced exactly 1.2013333333333334 audio seconds, 1,201,333,934
guest DSP instructions and the same nonzero audio and final canonical DSP state:

- PCM SHA-256: `cd6c42246e8d6ea8df5c8f553e52516237104a736460ddfa17399fee47d3f459`.
- Canonical state SHA-256: `e8315d1e6b36cc0456765fb188493f346bd556b55dc07afbd77a47f1d01fbe17`.
- 71,488 nonzero PCM values; peak 0.03685752674937248.
- No missing blocks, nonfinite PCM, halts, step errors, code mismatches, actual
  DSP traps or runtime guard hits.

AOT counted 14,416 block exits to the single-instruction interpreter per replay.
These are the normal `EXIT_TRAP` rollback/fallback path in
`packages/emulator/sharc/src/lib.rs`, not unresolved DSP traps or runtime guard
failures. Exact state equality includes the effects of those fallbacks. A
separate source review confirmed that canonical state export drains queued
frames and excludes host timing/tier counters, so this equality is meaningful.

## Acknowledged input batching and concurrency [D]

The shared `tests/regression/runtime/encoder-value.ts` helper now accepts an
opt-in `maxStep`, retaining its previous default of one increment. Each packet
still waits for native handling and the settled DSP control acknowledgement,
validates the exact movement, and obeys the same deadline and packet limit.
The 19 named host checks passed, including delayed acknowledgement, filtered
wake-up input, unexpected movement and bounded failures.

The Stage 5 CANOPY caller uses at most 16 increments per packet for continuous
parameters. Discrete choices keep single-choice steps, and the dedicated slow
selector/sensitivity checks remain unchanged. Its static continuous-parameter
packet estimate falls from 524 to 46. This is a packet-count reduction; a
complete compiled UI wall-time measurement is separate from the stock paired
backend result above. Startup timing spans now identify firmware load, module
verification and restart readiness separately.

Independent test files may use the existing isolated worker/session concurrency.
Actions targeting one emulator remain ordered, with one state owner and one
acknowledged parameter change at a time. The paired timing measurement uses an
exclusive benchmark lock rather than competing workers. No shared DTII state or
single UI proof is driven concurrently.

## Provenance and scope [D]

The generated core lives only under ignored `out/native/dt2-audio/gen`; its
fingerprint is
`4ff4eddefdd722cb9983f1f3884718fd866d586c11a8d5574c664b897e9e352f`.
`out/native/gen-index.json` registers it for packed DSP image SHA-256
`7fe2f0efcf63529b912bc8dbccbf97d9715f99cf525af67c4d5462420e7dcfbe`.
The card SHA-256 is
`2a926ea52170d7c13e4ac45eddfca8bb361afb93d22fc6ca6b88d9a041a54d46`, matching
the content key of `out/ready-pk-dt2-117/by-card/2a926ea52170`.

The audited daemon key is
`e9f803af54e6bf533c229af822fa2d1cf46d9d98425352b82686fd62e3ca0f9a`.
The paired runner SHA-256 is
`d4be17e8c1ff4c5d09031411efa9d19978136e8b36f58faa72b4d0ba16f6dd3e`.
Full input hashes, per-run commands/environment, native reports, WAVs and state
dumps are preserved in ignored `out/canopy-dt2-aot-paired/`, with the summary in
`receipt.json`. Generation/build logs use the prefix `out/canopy-dt2-aot-`.
`out/canopy-aot-reference-observations.json` records the earlier untimed UI-run
diagnosis separately.

One later read-only snapshot of the parent's running patched CANOPY UI session
confirmed `dsp_core: dt2-1.17` and 13,076,127,859 AOT instructions after 45.6
audio seconds. DSP CPU cost was 3.03 seconds per audio second; this unpaired
observation is not another speedup measurement. The CF policy remained
`Reference`; CF error was null, DSP traps/missing blocks/halts were absent and
the heartbeat reported no stall or underrun. The patched code recorded
27,907,240 code-mismatch checks and 262,346,096 appended interpreted
instructions, distinct from the stock proof's zero code mismatches. Those
fallback counts are retained in `out/canopy-dt2-aot-patched-status.json` and
`out/canopy-dt2-aot-patched-audio.json`; this spot check does not complete the UI
test.

The paired result is **stock-sample backend equivalence**. Patched or dynamically
loaded module code may fall back to the interpreter. Separately, the complete
compiled CANOPY UI/audio/preset test passed with this daemon in **403.12 seconds**
(398.354 seconds in its test body), using the exact frozen firmware and module
recorded in [finding 141](141-canopy-wavetable-fm.md). Log:
`out/canopy-wtfm-optimized-ui.log`. Its fresh receipt includes wavetable FM,
all four standalone-FM algorithms, native levels, source memory and CPST v5
recall. The first generic-core run timed out after 60 minutes; differences in
card seeding, input batching and cache warmth mean these durations are not a
paired suite benchmark. No physical DTII or USB audio measurement was performed.
