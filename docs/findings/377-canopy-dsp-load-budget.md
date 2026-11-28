# 377. A DSP load budget for CANOPY: stock trigs first

Why did stock tracks lose notes while CANOPY played ARP and POLY, and what keeps CANOPY from causing it? DT2 OS 1.17, the
hardware set with `digikit/debug` selected and a diagnostic image that counts DSP voice starts, 2026-11-27 (the check of the
budget on it 2026-11-28); the budget on the
SHARC core and the emulator, 2026-11-28. Marks as in `docs/FINDINGS.md`.

## The measurement [D]

One pattern of the owner: CANOPY on track 1 with ARP and POLY, stock sample tracks beside it (the report named tracks 9 to 11).

- The DSP block handler's own cycle count (word 0 of its reply record, finding 125) against a block of 32 samples at 48 kHz,
  666,666 cycles at an assumed 1 GHz:

  | | largest block |
  | --- | --- |
  | stopped | 73 % |
  | playing, CANOPY muted | 90.5 % |
  | playing, only tracks 9 to 11 | 75 % |
  | playing, CANOPY with ARP and POLY | 95 to 103 % |

- With CANOPY muted the pattern loses nothing. With ARP and POLY the ColdFire accepts the same notes (its priority gate counts
  the same number), but the DSP starts about a third fewer voices: voice starts per accepted note fall from about 1.45 to
  about 1.0, on every track, also with only tracks 1 and 9 to 11 playing. No block is skipped (block to interrupt ratio 1.0000)
  and USB audio has no dropped or repeated block.
- So the loss begins somewhere between 91 % and 95 % of a block. CANOPY's ARP and POLY alone cost about 130,000 cycles a block
  (20 %).
- Ruled out on the device: the track keyboard's voice pool (CANOPY borrows no stock voice), the native layer, the scheduler
  hook, the stock priority gate, the `_cs_voice` limiter of finding 375 (it touches chord copies only) and Euclidean mode
  (the emulator starts every voice of the owner's Euclidean tracks).

The mechanism is not proven **[O]**. The leading reading: the ColdFire's frame for a block arrives after a late DSP block has
begun its successor, so a one-shot trig in that frame is never read. Either way the remedy is the same: the block must stay
under the threshold, and CANOPY, which had no run-time budget, is the part that can give way. The owner's rule: stock trigs
first; CANOPY degrades instead.

## The budget [D]

Two parts, as the stock chord budget of finding 375 (`_cs_budget` of `core/dsp-voice`) has them: a trusted measure and a
consumer that sheds.

### The adapter decides (`core/instrument-play`, `_iv_budget` in `src/dsp/play.s`)

The instances run before the stock voices of their block, so they are budgeted from the block before. Once per block, before
the slots run, the adapter reads the last block handler's cost (`_dv_rec[0]` of `core/dsp-voice`, written by `core/dsp-link`;
word 0, rotated by 16 bits) and compares it with `_iv_budget_limit`:

- over the limit: the pressure rises by one, up to 7;
- `_iv_budget_calm` blocks in a row that each stay `_iv_budget_margin` or more under the limit: it falls by one;
- in between: it holds, and the count of calm blocks starts again.

| word | default | meaning |
| --- | --- | --- |
| `_iv_budget_limit` | 615,000 | cycles a block may cost (92 % of the assumed block; `_cs_limit` is 560,000) |
| `_iv_budget_margin` | 25,000 | how far under the limit a block must stay to count as calm (more than one voice gives back) |
| `_iv_budget_calm` | 150 | calm blocks (100 ms) that lower the pressure by one: full pressure takes 0.7 s to go |
| `_iv_pressure` | 0 | the pressure, 0 to 7 |
| `_iv_budget_cost`, `_iv_budget_overs` | | the cost last read; the blocks over the limit since boot |

All are plain DSP words: a device session reads and pokes them over the link. Without a reply record (no `core/dsp-link`
block yet) nothing changes.

The pressure reaches an instrument in bits 12 to 14 of the timing flags of its V5+ input (`transport_flags`, word 12;
`sdk/include/dk/instrument.h`), the same value for every instance. Why there:

- The input record is versioned and each version is validated strictly, by the registry and by CANOPY (reserved words must be
  zero, sizes must match). A new version or a reserved word would need a descriptor flag, the registry's admission and every
  fixture to change, and an older CANOPY on a newer image would fault on the first loaded block.
- The timing flags are a bit set that readers mask: bits 0, 1, 8 to 11 and 16 to 31 were assigned, 12 to 15 free. An
  instrument that does not know the bits renders as before, a newer CANOPY on an older image reads 0, and the module names no
  runtime symbol.
- One reader compared the whole word with zero (the arp's fine clock, for "no provider in this frame"): it masks the bits
  now, and a test holds that. An older CANOPY takes that path only with pressure and no clock provider at once.

`core/instrument-play` is a DT2 adapter; the other device does not build it.

### CANOPY sheds (`_cp_shed` in `src/dsp/voice.s`)

An instance has four foreground pad slots, two tail slots it shares with the arp, and the arp group. Once per block, after the
events, it applies the pressure:

| pressure | what goes |
| --- | --- |
| 1 and up | no new tail: a pad steal queues behind the same-slot ramp and an arp step ramps its group in place (the paths that existed for "no tail free"); the tails that sound end |
| 2 and up | released foreground voices and a released arp group end |
| 3, 4, 5, 6 | at most 3, 2, 1, 0 held pad owners sound: the newest are masked |
| 7 | the arp group is masked too |

- Every voice leaves through the eased gate-2 ramp of finding 365 (at most 64 samples from its level, no corner at either
  end). Nothing is cut.
- Tails and released voices end for good: they were ending.
- A masked voice ramps to zero and keeps its token and gate. The chord is still held, the arp keeps its members and its clock,
  a later note-off finds its voice, and a note that starts under pressure is owned and silent. When the pressure falls the
  mask lifts and the voice fades back in with its own attack. A masked voice that is silent and held is not rendered at all.
- At pressure 7 the instance costs what an idle one costs.
- At pressure 0 the output is bit-identical to the output before the budget (the pinned takes of `test_canopy_budget.py`,
  recorded at the commit before it); a block costs 54 instructions more.

Instruction counts of one instance on the SHARC core (seven voices: four pads, two tails, the arp; mean of a block while the
pressure is on, pads released half way; `CANOPY_BUDGET_REPORT=1`):

| pressure | Velvet | wavetable | FM |
| --- | --- | --- | --- |
| 0 | 15,988 | 37,889 | 61,575 |
| 1 | 12,115 | 28,008 | 45,044 |
| 2 | 8,051 | 16,220 | 24,603 |
| 3 | 7,771 | 15,054 | 22,400 |
| 5 | 6,867 | 12,398 | 17,692 |
| 7 | 4,446 | 5,280 | 5,251 |

The defaults were 570,000 and 40,000 at first. That limit lay under what the measured pattern costs without CANOPY (90.5 %,
about 603,000 cycles), so CANOPY would have been held at high pressure by load that was not its own. 615,000 lies above it and
under the range where the loss began; the device check below ran with 615,000 and 25,000.

## On the device [D]

The same pattern, an image with the budget and the cost work of this finding, limit 615,000 and margin 25,000, runs of 60 s
(about 94,000 blocks), DSP voice starts counted by the diagnostic image:

| | voice starts per accepted note, tracks 9 to 11 | largest block |
| --- | --- | --- |
| before the budget, CANOPY with ARP and POLY | 0.85 to 1.1 | 95 to 103 % |
| with the budget, CANOPY with ARP and POLY | 1.28 to 1.55 | 97 to 98 %, in isolated blocks |
| with the budget, CANOPY muted | 1.34 to 1.59 | 83 % |

- With CANOPY playing, the stock tracks start their voices as with CANOPY muted.
- 79 to 88 blocks of a run were over the limit, and the pressure stayed at 0 or 1: CANOPY shed its tails for moments and
  no held voice.
- Single blocks still reach 97 to 98 %: the budget acts on the block after, and the trigs were kept all the same. How much
  of the result is the budget and how much the lower cost of CANOPY's blocks was not separated **[O]**.

## What it does not do [O]

- It predicts a block from the one before. The stock load moves slowly (a sounding stock note costs about 8,200 cycles a block
  for as long as it sounds, finding 375), but the first block of a burst is not protected.
- A pattern whose stock tracks alone cost more than the limit leaves CANOPY no room: it sits at high pressure there, mostly
  silent. If the 130,000 cycles are seven equal voices, one costs about 18,000 cycles (2.8 %). The limit, margin and calm
  count are the words to tune on the device.
- The device check is one pattern and 60 s runs; the limit of 615,000 is 3 % under the 95 % where the loss was seen.
- Several instances share one pressure and each sheds by it; the cost of four loaded instances was not measured (finding 345).
- The fix lowers CANOPY's load. Why the stock engine loses trigs near a full block, and whether another module can push it
  there, stays open.

## Checks

| Where | What | Result |
| --- | --- | --- |
| SHARC core | `modules/machine/digikit/canopy/tests/test_canopy_budget.py`: pressure 0 renders the pinned takes (Velvet, wavetable, FM; direct and through the registry); for each family and pressure 1 to 7 the order above, the owners kept, no click (the second-difference measure of finding 354 where the pressure comes and goes, below 2.0 times the rest of the take, and no larger step between samples than the take without pressure), cost falling with each level; the voices back after the pressure; pressure bits alone are no clock provider; ignored below V5 | 38 passed |
| SHARC core | `test_instrument_budget.py`: the adapter's rise, hold and fall with the margin and the calm count; the bits in a V5 input and none in a V4 one; CANOPY through the admitted callback silent at full pressure with its owners held, and back | 3 passed |
| Emulator | `load-budget.device.test.ts`: the adapter reads the stock handler's cost (about 230,000 instructions a block there); with the limit poked to 1 the pressure is 7, held MIDI notes of the CANOPY track fall silent and stay owned, a new note is silent, a stock track's press has the same envelope as before; with the limit back the pressure returns to 0 and the held notes sound again | passed |
