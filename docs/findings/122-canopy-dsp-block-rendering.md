# 122: CANOPY DSP block rendering

DT2 OS 1.17, 2026-11-12. **[D]** The worst CANOPY process callback over all
ceiling tests fell from **99,849 to 77,017 instructions** per 32-sample block
against the unchanged 100,000 ceiling, in two bit-identical changes: wavetable
block rendering (99,849 to 89,255; the worst wavetable block 99,386 to 69,909)
and FM block rendering (89,255 to 77,017; section "FM block rendering"). After
each, output, instance state, status and every published buffer are
bit-identical to the previous build in all 1,164 recorded CANOPY DSP tests.
These are assembled-SHARC instruction counts, not hardware cycles; branch and
call penalties removed by the change are not counted, so the hardware saving is
probably larger **[O]**.

## Where the time went [D]

A per-label profile of the worst wavetable block (seven voices: four pads, two
tails and the arp, with wavetable phase modulation and a table transition)
attributed 72% of 99,005 instructions to the per-sample path: about 322
instructions per voice and sample. The voice loop was a software loop that
called the pad body, the wavetable tick, the PM tick, the sine lookup and one
or two table reads for every sample. The tick saved and restored nine
registers per sample and kept all its state in memory. The FM worst case
(91,455) spent about 46 instructions per voice and sample shifting the 23-word
halfband history, and the arp's bubble sort cost about 3,800 instructions when
a negative octave spread reversed its candidates.

## Changes

1. **Wavetable voices render a block at a time.** `voice.s` fills word 4 of 32
   eight-word records in `_wt_blk` with the pad oscillator, `_wt_render_block`
   (`wavetable.s`) replaces it with the wavetable output in four hardware-loop
   passes (control, phase modulation, table reads, blends), and the amp stage
   accumulates it. Block constants (sizes, mip offsets, table bases, the
   transition test) are loaded once; mip offset and table base fold into one
   address constant per table, an integer reassociation that cannot change the
   sum. Every float operation keeps its operand order. The per-sample tick, PM
   tick and table-read routines are removed; voices without a wavetable no
   longer call the tick. Records step through `M4 = 8`, saved and restored.
2. **The FM halfband history runs in a block buffer.** Each FM voice copies its
   15 even and 8 odd history words from the record into `_fm_hist` as
   (odd, even) pairs, appends each new pair at a moving pointer (`I4`, with `L4`
   saved and set to 0), and writes the last 23 samples back once per block. The
   record layout and the filter's operation order are unchanged; only the
   per-sample shift is gone.
3. **The arp sorts by stable insertion.** Same comparator as the bubble sort
   (pitch, or octave by the octave step's sign then unsigned token), so equal
   keys keep expansion order and the result is the same permutation.

Measured: about 197 instructions per wavetable voice and sample (from 322) and
about 1,340 fewer per FM voice and block.

| Worst block per test | Before | After |
| --- | ---: | ---: |
| `native_amp_fm_handoff_stays_within_ceiling` | 99,849 | 89,255 |
| `wavetable_fm_native_amp_seven_groups_and_transition_fit_ceiling` | 99,386 | 69,909 |
| `fm_source_handoff_with_eight_onsets_fits_ceiling` | 97,183 | 86,589 |
| `fm_worst_case_seven_groups_and_graph_morph_fit_ceiling` | 91,775 | 82,411 |
| `wavetable_worst_case_chord_arp_tails_table_transition_stays_within_ceiling` | 64,871 | 40,455 |
| `callback_instruction_ceiling` | 28,885 | 28,139 |

The FM paths (four-operator oversampling and its envelopes) are now the worst
case. Reset stays at 1,131.

## Equivalence method [D]

A pytest plugin recorded a digest of every SHARC call in the CANOPY DSP test
files (`test_canopy_v2`, `_voice`, `_instrument`, `_carrier`, `_wavetables`,
`_fm_tables`, `_native_amp_firmware`, `test_instrument_play`): halt, return
registers, output, instance state, status, captured audio and published
envelopes, and each data symbol separately. The baseline ran on `43536f14`.
After the change no output, state, status, audio, envelope, register or halt
digest differs (5,939 calls in 1,164 tests). The data symbols that differ are
new or removed scratch, saved registers, and variables that hold addresses,
which move because data sections were added. A value-level check of four of
the 16 test shards (292 tests) confirms it: every differing word of a shared
symbol is an address in both builds, except the wavetable register save slots
R0, R2 and R3, which the per-sample tick no longer overwrites, and R13, which
the block loop leaves at 1.0 instead of the old loop counter's 0 (dead after the
voice loop; callers of the renderer use only R0). All tests pass.

Two tests changed: the PM wrap-endpoint edge case now drives the block pass
(`_wt_pm_block`) instead of the removed per-sample tick, and the CPS1 legacy
fixture stubs the new wavetable interface.

## Pitfalls found [D]

- `.GLOBAL` on a `.PKBSS` symbol also creates a code symbol at offset 0, and the
  linker resolves references to it: `I0 = _wt_blk` loaded the code base. The
  emulator ran consistently on those addresses, so the output looked right.
  PKBSS symbols are global without the directive (other files use `.EXTERN`).
- `MODIFY(I0,M4) (NW)` with a modify register did not advance I0 in the
  emulator; without `(NW)` it advanced by bytes. Post-modify through a memory
  access (`DM(I0,M4)`) steps words as `DM(I1,M6)` streams do. Which encoding
  hardware applies to the register form was not checked **[O]**; CANOPY no
  longer uses it.

## FM block rendering (2026-11-12, second change) [D]

FM voices now also render a block at a time (`fm.s`), in five passes over
12-word control records in `_fm_ctl` (stride `M4 = 12`, saved and restored):

1. **Envelope.** The timbre envelope's stage changes at most twice per block,
   so the per-sample stage dispatch is hoisted: a released voice runs a
   branch-free release loop, a held voice past decay fills the block with the
   sustain level, and only attack and decay blocks test their boundary each
   sample (and fall through into the next stage).
2. **Control.** Amplitude (stock AMP samples or the legacy ramp), key-tracking
   scale and the smoothed depth give the per-sample modulation scale.
3. **Ramps.** The nine graph/ratio ramps run in two register loops (one add
   and one parallel store per ramp and sample) instead of a load, a step load
   and an add per ramp.
4. **Operator graph.** Two internal samples per output sample with the three
   phases and the delayed B output in registers; ratio A x increment is formed
   once per output sample (the same product both internal samples used).
5. **Decimation.** The existing halfband taps, then amplitude and gain.

Every float operation keeps its operand order and the record keeps its layout.

| Worst block per test | Before | After |
| --- | ---: | ---: |
| `native_amp_fm_handoff_stays_within_ceiling` | 89,255 | 77,017 |
| `fm_source_handoff_with_eight_onsets_fits_ceiling` | 86,589 | 74,097 |
| `fm_worst_case_seven_groups_and_graph_morph_fit_ceiling` | 82,411 | 69,952 |
| `wavetable_fm_native_amp_seven_groups_and_transition_fit_ceiling` | 69,909 | 69,909 |

The per-sample FM work fell from about 276 to about 223 instructions per voice
and output sample (handoff profile). The FM handoff remains the worst callback.

Equivalence, recorded as above against `661f1feb`: no output, state, status,
audio, envelope, register or halt digest differs in 5,962 calls of 1,164
tests (1,163 pass, 1 skipped, as before). Every differing data symbol is new
FM scratch (`_fm_ctl`, `_fm_m4`, `_fm_rec`) or holds an address. The
value-level check of the same four shards found no other change: every
differing word of a shared symbol is an address in both builds.

With the resulting package (`canopy-fm-release.dkm`) and the same image, the
four-instance proof and the compiled Stage 5 (414 s) and Stage 6 (440 s)
proofs pass (`out/canopy-fm-proof.log`, `out/canopy-fm-stage5.log`,
`out/canopy-fm-stage6.log`); the proof's meter read 236,585 busy instructions
per block and its capture 0.0512 RMS, within the earlier runs' spread.

## Not covered

Hardware cycle time, and four simultaneous worst-case instances on the device,
remain open **[O]** (finding 121).

## Emulator checks [D]

With the optimized package (`canopy-opt-release.dkm`) and the unchanged
four-instance image, the any-track/four-instance proof and the compiled Stage 5
(416 s) and Stage 6 (412 s) proofs pass (`out/canopy-opt-proof.log`,
`out/canopy-opt-stage5.log`, `out/canopy-opt-stage6.log`). The proof's sound
does not use the wavetable path, so its DSP meter barely moves: 236,378 and
236,434 busy instructions per block against 236,524 and 236,426 for two runs of
the previous package. Its captured level depends on when the host starts the
capture after the notes (0.0517 and 0.0549 RMS for the previous package, 0.0403
and 0.0542 for the new one), so it is not an equivalence measure.
