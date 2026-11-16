# 117: CANOPY wavetable phase modulation

DT2 OS 1.17, 2026-11-09. Counts below are for that build; block rendering
later cut the worst wavetable block to 69,909 with bit-identical output
(finding 146). **[D]** The acceptance ceilings remain **100,000
instructions per 32-sample process callback** and **1,600 per reset**. The
revised interior-frame stress measured 97,524 instructions directly and 97,888
through the protected dispatcher; reset measured 1,131. These are bounded
assembled-SHARC emulator counts, not physical execution-time or hardware-capacity
qualification. DSP acceptance is complete; compiled UI acceptance is described
below.

## Implementation contract [D]

`modules/machines/canopy/src/dsp/wavetable.s` applies one sine modulator to the
read phase of the selected, continuously morphed wavetable. The existing carrier
phase remains the pitch authority. This preserves position/scanning, table
transitions, four pad voices, two shared release tails and the active arp. The
stock AMP envelope still controls each note's amplitude. Standalone four-operator
FM remains a separate source family.

The 32-word control bank becomes CPSB (version 11). Upper halfwords 28..31 hold:

| Field | Wire range | Default | Meaning |
| --- | --- | --- | --- |
| DEP | 0..100 | 0 | Base modulation depth |
| RAT | 0..15 | 3 | Sine-modulator frequency ratio |
| ENV | 0..200 | 100 | Signed timbre-envelope send, biased by 100 |
| WHEL | 0..200 | 100 | Signed route-specific CC1 send, biased by 100 |

Ratios are 0.25, 0.5, 0.75, 1, 1.25, sqrt(2), 1.5, sqrt(3), 2, 2.5, 3, 4, 5,
6, 7 and 8. All source families retain the four settings, but only wavetable
rendering applies them. CPS9/CPSA still require these four upper halfwords to be
zero. Schema 5 reports all 32 custom values and moves native tuning to detail
word 42; older schemas retain tuning at word 28. The physical transport and
instance sizes do not expand. Matching firmware and module builds are required.

Per-owner PM history uses words `337 + 48*n` through `342 + 48*n`, for `n=0..7`:
token, modulator phase, smoothed ratio, smoothed depth, carrier increment and one
reserved word. These are six previously reserved words within each mutually
exclusive FM history. Token reuse and source handoff invalidate the tags; reset
still clears the same 744-word instance. A new arp at the parent's exact carrier
increment inherits its beginning-of-block PM phase/ratio/depth. Different pitches
retain independent PM history while keeping the existing envelope/route lineage.

Depth combines base, envelope and wheel sends, clamps to 0..1, and smooths per
sample. Ratio changes also advance per sample without resetting phase. The phase
offset is `sin(modulator_phase) * depth^2 / (2*pi)`: at most one radian before the
pitch/ratio taper. Zero DEP with neutral ENV/WHEL follows the unmodulated read
phase exactly; nonzero sends can create modulation even when base DEP is zero.

The taper uses `q = max(1, current_ratio, requested_ratio) * carrier_increment`
and `clamp((0.2-q)*10, 0, 1) * min(1, 0.007/q)`. Its square root scales depth
before depth is squared in the phase expression. A conservative mip-frequency
bound covers the depth/ratio reachable during the current block, with small
float-roundoff margins. It retains the existing mip/frame/table interpolation
and avoids selecting the final requested brightness before the control ramp
has reached it. Negative phase wrapping also handles addition rounding to one,
so the interpolation endpoint cannot read beyond the table.

## Corrected budget coverage and measured candidate [D]

The earlier seven-group stress used positive WT envelope/wheel sends. At its
wheel value, the position clamped to final frame 7, which skips interpolation
with a following frame. Thus its sub-100,000 result did not cover the full
interior-frame path. Holding raw position at 345 (internal frame coordinate
3.45) with neutral WT sends and zero scan,
while retaining maximum PM sends and a table transition, exposed strict-limit
failures. Diagnostic-only larger limits measured 114,758 instructions directly
and 115,122 through protection; those runs were failures against the unchanged
100,000 ceiling.

The revision keeps phase fractions and blend weights in registers, retains both
mip addresses while reading adjacent frames, and combines independent loads with
the original arithmetic. It also removes redundant scratch spills and reuses a
sine lookup entry for an already-wrapped phase. Transition/PM scratch parameters
are explicitly contiguous data for their relative loads. No interpolation,
voice or AMP stage is removed, and the original floating-point operation order
is retained. Independent source review found no register, addressing or parallel
load/store ordering defect. The final exact neutral PCM comparison passes below.

| Strict candidate case | Busy callback | Following callback | Reset |
| --- | ---: | ---: | ---: |
| Interior, ADSR, direct | 97,524 | 86,517 | 1,131 |
| Interior, ADSR, protected | 97,888 | 86,881 | 1,131 |
| Interior, AHD, protected | 97,806 | 86,803 | 1,131 |

All three cases retain four pads, both tails and arp during the busy callback,
interpolate frames 3/4 in both transitioning tables, and use spread -3. ADSR
attack is 1/31. The AHD witness uses attack 1/16, hold 8 samples, sustain 1 and
finite decay/release factors 0.9991; all seven AMP sidecars reach the AHD
post-hold phase 4. The
protected ADSR reduction from the failed interior candidate is 17,234
instructions. Logs are ignored artifacts:
`out/canopy-wtfm-interior-budget-optimized3.log` and
`out/canopy-wtfm-interior-ahd.log`.

## Spectrum scope and acceptance [D]

The committed tests in `tests/patchkit/test_canopy_v2.py` include an independent
phase/mip waveform reference, ownership/reset cases, a high-pitch PM cutoff
witness, and a 30-case spectrum matrix: five tables, MIDI pitches 60/84/96 and
ratios 1x/8x. That matrix measures the final 4,096 samples of 256 blocks with a
Hann window, requiring nonharmonic residual below -40 dB and finite, non-silent
output. Energy near the integer harmonic lattice, including DC, is allowed;
aliases on that lattice are not distinguished. This is a bounded residual
measurement, not a universal alias-free claim or coverage of inharmonic ratios.

The final 102-case behavior selection passes, including the expanded 36-case
direct/protected budget matrix (513.45 seconds; 644 unrelated cases deselected).
All 114 existing wavetable regressions pass (300.56 seconds). The 30-case
spectrum matrix passes in 980.80 seconds, with residuals from -47.35 to -44.68 dB
against the -40 dB threshold. Logs: `out/canopy-wtfm-final-behavior.log`,
`out/canopy-wtfm-final-legacy-wt.log` and `out/canopy-wtfm-final-quality.log`.
The historical PCM comparison against `b816958f` passes all 30 scenarios and
27,840 bit-identical samples, with matching relevant persistent state. It spans
five tables, three native AMP modes, and end/interior frame positions, including
table changes, arp/tails and steals. The old renderer needs a diagnostic-only
250,000-instruction runner allowance; the new renderer remains at 100,000.
This is equality evidence, separate from strict budget acceptance. Receipt:
`out/canopy-wtfm-zero-ab.json`.

[D] The final build's TONE and WT FM captures have identical native TUNE and
source LEV cells, in normal and held-SRC modes, at matching values 0.02/127.
The page indicators outside those cells change as expected. This is same-build
page consistency, not a fresh stock-default pixel comparison. Images and receipt
are in `out/canopy-wtfm-native-ui/`.

The complete compiled SRC proof passes in **403.12 seconds** with the verified
DT2 AOT daemon, acknowledged continuous-control batches and a fresh module-seeded
card. It retains the individual operation guards, exact module readback,
selection/restart and list-sensitivity checks. Coverage includes WT-FM live
controls/audio, shared timbre envelope, native TUNE/LEVEL/LEV, held values,
independent source banks, all four standalone-FM algorithms and CPST v5 recall.
Final runtime guard checks pass. Log: `out/canopy-wtfm-optimized-ui.log`;
receipt: `out/canopy-fm-native-evidence.json`.

The passing run records firmware SHA-256
`1e0ec14c05ccbdffa79cd498f9ff0edfd00596b95de7237e5e9dc7544026e592`
and whole module SHA-256
`d6ba479cc701212ad8e49df3754d5b2617fc75d4a7fcd205136ad82c078448de`.
The first compiled run exceeded its 60-minute overall timeout; it remains a
failed run. Its setup and cache warmth differ, so the final duration is not a
paired whole-suite speedup claim. [Finding 142](142-dt2-emulator-test-execution.md)
records the separate paired backend comparison. No physical installation or
new hardware performance proof is claimed here.
