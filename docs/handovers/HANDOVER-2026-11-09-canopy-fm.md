# CANOPY four-operator FM

The user approved implementation of the FM proposal. Work is on
`codex/short-loop-proof`, based on `640dc649`. No physical DTII writes or audio
capture were performed for this implementation. Earlier hardware results apply
to the wavetable revision, not FM.

The added source-handoff stress matrix combines eight
onsets, fractional tuning, drift/bend/wheel, random/as-played arp order and signed
spread at an arp step boundary. It initially exceeded the unchanged 100,000
instruction ceiling. Token invalidation removes duplicate history clearing;
combining independent loads with oscillator calculations brought the upward
protected path to 98,951 instructions (98,631 direct). The six diagnostic
callback records have bit-identical PCM, state and status before/after the
scheduling change. This is an instruction bound, not a hardware timing result.
Independent source review checked register aliasing and balanced address bias.
The ordinary strict-ceiling suite passes all ten handoff/reentry cases in
108.90 seconds (`out/canopy-fm-strict-handoff.log`). Returning to FM with dirty
history payload also matches a cleared-history reference exactly.
The expanded downward/as-played matrix exposed additional sorting cost. The arp
now shrinks bubble passes, exits after an already sorted pass, caches the order
and unrolls the fixed five-word swap. Its comparator, stability, padding and
candidate order are unchanged. All 18 before/after callback records have exact
PCM/state/status equality (`out/canopy-fm-sort-before.json` and `-after.json`).
Worst entry is now 98,614 instructions direct and 98,934 protected. All 26
strict handoff/reentry cases pass in 289.31 seconds, including all six stress
configurations in both source directions (`out/canopy-fm-strict-handoff-final.log`).
The rebuilt package below passes both final compiled acceptance tests. Earlier
compiled runs were stopped before FM UI checks and are not passing results.

## Implemented contract

- FM is WAVE choice 7 / source family 3, after METAL.
- Four sine operators provide TWIN, STACK, SUM and CHAIN routings. BAL mixes the
  audible X/Y taps; X feeds Y before volume scaling in CHAIN.
- SOUND D is DEP. FM TONE exposes WAVE, ALG, BAL, RTA, RTB and FDBK. A conditional
  fourth FM ENV page exposes timbre ATK/DEC/SUS/REL and signed ENV/VEL/WHEL.
- WAVE, algorithm and ratio lists use four raw encoder ticks per item. WAVE is
  view ID 30, separate from active parameter 16, which means ALG in FM.
- Native TUNE, source LEV, dedicated LEVEL/DATA and held-SRC values retain the
  existing native editing/drawing paths. FM ENV A is custom ATK; H is native LEV.
- The native page vector owns four entries and safely cycles 2/3/4-page layouts.
- CPSA uses header version 10 and the existing 32-word control-bank transport.
  Twelve upper halfwords are interpreted by source family; CPS9 cannot select FM.
  Schema 4 acknowledges the active bank; schema 3 cannot acknowledge FM edits.
- CPST v4 is 128 bytes and stores remembered legacy COLOR, wavetable settings,
  and FM settings independently. Versions 1/2/3 migrate on read without altering
  the old sound or rewriting storage. Native project persistence/p-locks remain
  outside these custom presets.
- Persistent state grows to 2,976 bytes. The existing 100,000 process and 1,600
  reset instruction ceilings remain unchanged. These are component instruction
  limits, not hardware cycle measurements.

The renderer uses 2x internal sampling, an interpolated original sine
table and a 31-tap halfband decimator. Transitions involving FM fade out over
128 samples, then fade in over 128 samples, retaining held-note ownership and
running only one source engine. This introduces a brief level dip, approximately
5.33 ms at 48 kHz. Graph changes smooth edges within one FM renderer. The
high-register depth taper is documented in the design; its target includes the
carrier and requested/current modulator ratios. Each voice interpolates changes
over 32 samples. The added depth cap does not attenuate feedback; feedback retains
its separate original high-frequency bound.

## Verification

131 focused host checks pass: 78 preset, 40 SRC and 13 native-render cases.
The native test uses real view formatting to check FM labels, ratio labels,
signed modulation extremes, held values, native protected regions and level
delegation. Logs are `out/canopy-fm-cf-host-final.log`,
`out/canopy-fm-cf-host.log` and `out/canopy-fm-native-render-host.log`.
The broader host run subsequently found two stale text expectations: the new
ANLG-to-FM source hint and the configuration-bank error label. Both test-only
corrections pass all 45 cases in the two affected files against final production
code (`out/canopy-fm-host-expectations-final.log`).

All 47 native page allocation checks passed, including 2-to-4, 3-to-4 and
4-to-3-to-4 transitions. Adapter checks total 117 passes across the admission and
source-switch runs, with one existing skip. They include CPSA range/duplicate/
atomic admission and legacy compatibility. Logs are
`out/canopy-fm-core-admission.log`, `out/canopy-fm-core-pages.log`,
`out/canopy-fm-core-page-toggle.log` and `out/canopy-fm-core-source-switch-pass.log`.

The executed DSP checks and independent review corrected integer/float register
aliasing, unsupported conditional stores, incomplete arp history inheritance,
and drift matching. Full history now follows matching pitch and drift; opposite
detuning/transposition keeps independent signal history. The zero-drift guard
recognizes both IEEE positive and negative zero.

The steady-state rendered quality matrix passed all 17 cases in
`out/canopy-fm-quality-final.log` (803.14 seconds). Six waveform cases compare the
actual assembled kernel to independent analytic sine equations. Eleven
integer-ratio, feedback-off stress cases have off-harmonic energy below -35 dB;
the worst measured case is CHAIN, MIDI 76, ratios 1/1, BAL 100 at -39.87 dB.
That first run preceded the final onset/drift refinements. The complete matrix
then passed again on the refined source before the handoff optimization; this
output is preserved in `out/canopy-fm-quality-before-handoff-optimization.log`.
After oscillator instruction scheduling changed, all 17 cases passed again in
725.08 seconds (`out/canopy-fm-final-render-quality.log`), with identical
reported waveform/spectral measurements. The later arp-only sorting change
does not run in these pad-only quality cases.
It is neither a feedback spectral qualification nor a
silicon measurement. Analytic taper exploration is preserved under
`out/canopy-fm-taper-*`; model-only results must not be conflated with this
executed-kernel matrix.

The earlier 50-case focused DSP run and 16-case signed-zero/taper/arp/budget
subset passed before the handoff optimizations. The final source now passes
all 508 V2 cases: 246 direct in 888.56 seconds and 262 protected/packet cases
in 903.42 seconds (`out/canopy-fm-final-v2-direct.log` and
`out/canopy-fm-final-v2-protected.log`). These cover the maximal event/voice
loads, same-pitch inheritance, route-scoped wheel/reset, bounded feedback/churn,
independent taper interpolation and dirty-history source reentry. Eight-event,
seven-group execution is 93,214 instructions direct and 93,534 protected;
the largest tested source-handoff callback is 98,614 direct and 98,934 protected,
below the unchanged 100,000 ceiling. Full reset is 1,131 / 1,600 instructions.
Three asset reproducibility/filter/sine-error tests also pass. These are emulator
instruction bounds, not a paired timing benchmark or hardware CPU guarantee.

Regression typechecking passed. Stage 5/6 tests have been extended for FM,
source memory, native page navigation, preset recall, held-note graph changes,
Overloop crossfades and seven-group stress. Supply the new matching artifacts
explicitly through `CANOPY_UI_IMAGE/PACKAGE` and `CANOPY_COEXIST_IMAGE/PACKAGE`;
their default filenames refer to earlier builds.

The final compiled Stage 6 coexistence test passes in 1,896.06 seconds
(`out/canopy-fm-acceptance-frozen.log`). It exercises all four FM algorithms at
DEP 127 / FDBK 100 with held notes and an arp, shared release tails, Overloop
crossfades and file uploads. The recording continues after releasing CANOPY's
notes, and no guard or instrument fault is reported. Audio and ownership
observations are saved in `out/canopy-stage6-coexistence.json`. This is emulated
integration evidence, not hardware timing qualification.

The final compiled Stage 5 test passes in 3,196.62 seconds. All four algorithms
produce audio while retaining the held note. The conditional FM ENV page,
native source level, independent source memories and CPST v4 save/recall pass.
Source LEV reduces measured FM RMS from 0.01809964 to 0.0000000294 without
changing the FM bank. The earlier dedicated track LEVEL check reaches exact
silence. Evidence is `out/canopy-fm-native-evidence.json` and
`out/canopy-native-levels.json`. Vitest reports two files/two tests passed in
3,201.68 seconds, with exit status zero, using the exact artifacts below.

All five FM page captures were preserved under `out/design/canopy-fm-ui` and
independently reviewed. No clipping, overlapping labels or visual glitches were
found. Eight comparisons of native TUNE/source-LEV regions, including held-SRC
values and FM ENV level, found zero differences across 79,040 pixels against
the matching wavetable screens from the same compiled run. The whole-image
positive control differs by 9,136 pixels. Hashes, source provenance and exact
rectangles are recorded in `native-pixel-comparison.json` in that directory.
This is an emulator comparison of retained native regions, not a new stock-ROM
or physical-device capture.

## Rebuilt artifacts after handoff optimization

- Firmware: `out/patch/canopy-fm.syx`, 2,073,760 bytes, whole-file SHA256
  `1153a18c40fbbc2467c2d485af77d41aa8640137eb4d32ca7077d22037b55e6b`.
- Module: `out/patch/canopy-fm.dkm`, 230,820 bytes, whole-file SHA256
  `e67d043f8bb95005bfeeaf9b02d60741b1116e76d9c2f05cef17dc7988768e8b`.
  Patchkit's printed DKM content hash is a different, internal hash;
  `d6cdf798e2ac3c23ae37367f3affdf04c0b18c9aa91a097efa2e5a6ef2ccb0aa`.
- The CANOPY object decodes 5,501 instructions. Its sections contain 23,952
  text bytes, 194,612 data bytes and 2,240 BSS bytes, before loader placement.
  Each instance separately requires 2,976 bytes. The existing factory tables
  dominate shared data; FM adds an original sine/ratio table and callback scratch.
- Firmware preflight passes: MAIN staged 3,664,168 / 4,193,280 bytes; container
  1,636,224 / 3,145,728 bytes. Logs: `out/canopy-fm-preflight.log` and
  `out/canopy-fm-package-release.log`. Tracked core adapter objects were rebuilt;
  the content-module objects remain in the ignored `out/cache/dko` cache.

The generic `needs`/browser fit path remains V1-only and reports a malformed
DSP section for this V2 instrument, as with the preceding CANOPY package.
Compiled V2 admission and coexistence are the relevant integration checks;
do not present that legacy calculator as a fit proof.

`DIGI_GATE_BASE=640dc649 mise run test:plan` selects broad DTII consumers because
the shared native page allocator and playback adapter changed. The full affected
gate ran once before the final handoff optimizations using
`TMPDIR=/var/tmp/digikit-canopy-fm DIGI_GATE_BASE=640dc649 mise run test`.
It finished in 10m42s with 67 successful tasks, eight failed tasks and 35 tasks
not run (`out/canopy-fm-gate-complete.log`). This is not a passing merge gate.

- The regression host target passed 458 of 460 cases; its two stale expectations
  are corrected and the affected files pass all 45 cases, as recorded above.
- Rust passed 442 cases and failed one firmware image pin. Substituting only the
  base DTII `core/machines` object reproduces the old pin exactly; the new object
  changes section 3 by 20 bytes, with every other section identical. The pin was
  updated and its focused test passes (`out/canopy-fm-pin-causality.log`,
  `out/canopy-fm-typed-pin-test.log`). The absent DNII image means that unchanged
  pin was not re-executed.
- Formatting failures affect 16 unchanged TypeScript files and two unchanged
  Python files. All 44 reported layering violations were verified to contain the
  same lines in base `640dc649`; none is newly introduced by FM.
- Nx host tests encountered 30-second graph subprocess timeouts and an RPC
  timeout. No Nx source changed; this is an environment/timing failure, without
  a separate baseline rerun establishing its reproducibility.
- The missing `out/native/dn2-audio/gen` prerequisite blocked the daemons and
  coupled builds. Python tests, CANOPY load conformance, native-page proofs and
  regression device targets consequently did not run through this gate.

The direct compiled Stage 5/6 checks and named DSP owner checks provide their own
results; they must not be presented as completion of every skipped gate target.
Native Linux temporary storage avoids the Windows filesystem's lack of Unix
sockets and the small `/tmp` tmpfs; this changes no test assertions.

## Completion and release boundary

Implementation, packaging, focused emulator acceptance and visual review are
complete. The broader affected gate remains non-passing for the reasons above;
hardware sound, latency and worst-case CPU qualification are separate open work.
No installation was performed.

The completed compiled run records `out/canopy-fm-acceptance-frozen.log` and uses
the rebuilt package above. The 17 rendered quality cases, 508 V2 cases and
28 remaining legacy instrument/voice cases are complete. A legacy CPS1 fixture
initially lacked disabled FM linkage; its two cases pass after adding inert FM
hooks, while the other 26 remaining cases pass unchanged.
Logs are `out/canopy-fm-final-remaining.log` and
`out/canopy-fm-legacy-fixture-final.log`. Production FM code remains excluded
from that fixed-sine oracle.

Do not install on hardware without a new installation request.
