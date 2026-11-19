# 360. Overloop as an always-listening looper

Step 3 of the redesign in [the plan](../plans/prism-overloop-redesign.md): Overloop records what is heard all the time
and, at a pattern change that was armed, keeps the last cycle of the pattern that was playing. `core/loop-controls` is
gone. Digitakt II OS 1.17, on the SHARC-only rig, on the host and on the emulator with the SHARC core. Nothing ran on
hardware and nobody has listened. Marks as in `docs/FINDINGS.md`; every result here is **[D]** (measured or read once in
this work) unless it is marked **[O]**.

## What changed

| | Before | Now |
| --- | --- | --- |
| Recording | by hand: REC armed a capture of LENGTH steps from START | all the time, into a rolling record of what is heard |
| The loop | the captured window, moved with START and LENGTH | the last cycle before an armed pattern change; nothing is set by hand |
| Controls | four kit values (REC, START, FADE, LENGTH) on a knob page, copied into the next kit at a pattern change by `core/loop-controls` (a policy run from the UI loop, with a DSP record of its own) | ARM, XFAD and DROP, held outside the kit by `core/loop-control`; nothing is copied between kits |
| Page | the fx service's painter over the knob page | a native page with keys, through the dispatcher `core/fxview`, which PRISM's page now uses too |
| Storage | the loop lease (8 MiB) | the loop lease and the second phrase bank, 1,032,192 frames each |
| Hooks into stock code | the FX view's update slot (`core/loop-display`) | the same slot, and the five FX view slots of `core/fxview` that PRISM's page had. No instruction of the firmware is replaced for Overloop |

Removed: the module `core/loop-controls` (its ColdFire and DSP objects, its import `_loop_controls_info` in `core/dyndsp`, its tests; it had no anchor or port entry of its own); the DSP half of
`core/loop-display` and its old page; the Python reference model and tests of the old engine (`tools/patchkit/overloop_ref.py`,
`modules/fx/overloop/tests/test_overloop.py`, `tests/patchkit/test_loop_controls_cf.py`, `tests/patchkit/test_loop_display.py`);
`sdk/dsp/contract/dk_loop_controls.inc` and `dk_loop_status.inc`; the function `pp_phrase_timing` of
`core/pattern-position`, which had no caller. New: `core/loop-control`, `core/fxview`, `sdk/dsp/contract/dk_loop.inc`,
`sdk/include/dk/loop.h` and `dk/fxview.h`. The patch sets `overloop`, `canopy-overloop` and `canopy-overloop-prism` name
`core/pattern-position`, which Overloop's engine imports; no phrase adapter is needed for Overloop.

## The boundary

No hook was added. The engine reads the record `core/pattern-position` already publishes every block (finding 133): a
send count, a boundary count, the start generation, the flags, the pattern's identity, the step and the pattern's steps.
A step boundary is committed with block precision and begins with the block whose record counts it, as the notes of that
step do. So the engine places every event on a block edge, and the loop is a whole number of blocks.

- **A change** is a boundary whose pattern identity differs from the last boundary's, in the same start generation.
- **A START** is a boundary of another generation.
- A record that is not valid or not running, a send count that did not advance by one, or a boundary count that advanced
  by more than one ends the run: no edge can be placed, nothing is captured, and the sound is not touched.

What an armed change keeps, L frames ending with the block before the change:

| Case | L | Rig result |
| --- | --- | --- |
| Change at the pattern's end (step 0 follows the last step, every step since the last step 0 in order) | the frames of the cycle that just ended, as played | cycle 2560: loop 2560; five turns against one cycle earlier, difference 0 on both sides; click measure (finding 354) at the boundary and at each turn 0.89 x steady; level of each turn 1.000000 |
| The same with steps of 320, 352, 288 frames in turn (a tempo that is no whole number of blocks a step) | the last cycle as it was played: 2592 where the cycles were 2592, 2528, 2560, 2592 | exact |
| Direct change in the middle of a pattern | the frames of the last whole cycle; the loop starts where the pattern stood one cycle earlier | change at step 4 of 8: loop 2560, three turns against one cycle earlier, difference 0, click 1.00 x steady |
| Less than one cycle since START | (frames since the run's first boundary / boundaries) x the pattern's steps, rounded | 3 of 8 steps of 320: loop 2560; it holds what was heard before START as well. Frames from before the module was loaded are silence, not old memory |
| Change while the transport is stopped | no loop; the arm stays for the next change while it plays | status unchanged, input bit for bit |
| STOP while a loop plays | the loop goes on | - |
| START while a loop plays | the loop begins again at its first frame with the pattern's first step; the old position fades out under it over 256 frames with an eased weight | from 256 frames on the output is the loop from its start, difference 0; click 0.86 x steady |
| Tempo change after the capture | the loop keeps its recorded length; what an armed change would keep follows the new tempo | steps of 416 after a loop of 2560: the loop turns every 2560, a change would keep 3328 |
| A second armed change while a loop plays | what is heard then (the live pattern and the loop as the crossfader mixes them) | crossfader at 64: three turns, difference 0, click 0.89 |
| The same, one cycle after the first | the whole cycle; the frames before it for the seam come from the other buffer | difference 0, click at the turn 0.89 |
| The same, 5 of 8 steps after the first | the last 8 steps heard, three of them from before the first change | difference 0 |
| The same, 2 of 8 steps after the first | **the limit:** the first 4 of the loop's 8 steps are silence | as stated |
| A cycle longer than a buffer | the last N - 512 frames (N the buffer's frames); the status says so before (`LS_NEXT_CUT`) and after (`LS_CUT`) | 5120 in buffers of 4096: the last 3584, exact; its turn 0.95 x steady |
| ARM without a change | nothing; a second ARM takes it back | input bit for bit |

On the device's buffers the longest loop is 1,031,680 frames, 21.49 s (128 steps below about 89 BPM are cut). The plan
says 21.8 s: the loop lease alone would hold that, the second buffer is a phrase bank of 1,032,192 frames, and the engine
uses what both hold.

## The storage

Two buffers of N frames, each two planes of floats: the loop lease (`_loop_buffer_info`) and the second phrase bank
(`_phrase_storage_info`), both from `core/loop-storage`. PRISM's tape is the first bank. Both descriptors are checked on
every block before any access (magic, version, extent inside external memory, no overlap, at least 4096 frames); a
descriptor that is not valid makes the adapter count a fault and play the input. A lease that changes under the running
engine clears the record, the loop and the position and goes on in the new buffers without a fault.

One buffer holds the rolling record of the effect's **output**, so the loop is what was heard: the tracks, the stock
effects, the send effects' returns, and whatever of an older loop the crossfader mixed in. An armed change freezes that
buffer as the loop and the record goes on in the other one. That record begins empty. The frames heard before the change
are copied into it from the loop's buffer, one block each block and the newest first, to the frames before the record's
first one, until it holds all of them or is full (about 160 more instructions a block while it runs). After t frames the record
holds 2t, so a second armed change finds the L frames it keeps and the 480 before them when it comes at least
(L + 480) / 2 frames after the first; sooner, the part that is not there yet is silence (the limit in the table).

## The seam and the crossfader

- **The seam.** The last 480 frames of the loop (a quarter of a loop shorter than 1920) run into the 480 frames before
  its start: `out = loop + w (pre - loop)`, w rising from 0 to 1 as `a a (3 - 2a)`. The turn is then as it was recorded
  when the loop's start was reached the first time. For a pattern that repeats these are the same samples and the turn is
  exact. With a linear weight a seam between unlike audio measured 3.02 x steady, with the eased one 0.95.
- **The boundary.** At the change the loop starts at its first frame at full level in the same block: the output is the
  frame recorded one cycle earlier. Nothing fades, so there is no dip (level of each turn 1.000000).
- **The crossfader** is today's law: `out = in + (loop - in) g`, g moving by at most 1/256 a frame and landing on its
  target. At g = 0 the input is copied (bit for bit, measured), at g = 1 the loop (difference 0), between them the law
  bit for bit in single precision (g = 0.5039370059967041 at 64). A move from LOOP to 64 takes 127 frames; its click
  measure is 1.12 x steady. A drop at LOOP reaches LIVE after 256 frames (1.82 x the louder of loop and live): the linear
  move is the law that was there, kept.
- At a capture g is 1 at once and **held** there until the control home has read the new loop's count back, so an old
  crossfader value is not heard in between.

Cost: the heaviest block (a START while the crossfader moves and the record is being filled) is 4384 instructions with
`core/fx`, which is `dsp_cost_fx`; a block without a loop is at most 1616.

## Sizes

The image of the fork's set (`canopy-overloop-prism`, built from the stock firmware with apply, preflight and verify):
`cf.expansion` 1,314,848 of 1,396,736 bytes (144 fewer than after step 2; 81,888 free), `dsp.l2` 566,060 of 922,756
(256 more: the tap). The set `overloop`, without any phrase adapter: `cf.expansion` 1,177,912, `dsp.l2` 532,720.
`overloop.dkm` is 4,924 bytes. The four packages build and the two services bind.

## Where the controls live

`core/loop-control` ([README](../../modules/core/adapters/loop-control/README.md)) is the one home: XFAD (0 to 127) and
the counters of ARM and DROP, in the image's memory, read and written through `sdk/include/dk/loop.h`. They are not kit
values. Overloop's page uses it now; PRISM's page is not wired to it.

- **To the engine.** The engine is an ordinary master effect, and its controls are the parameter words of its row. A new
  mechanism of the fx service, held values: for every loaded effect the service asks the image function
  `fx_held_values(effect_id, native_page, raw[8])`, found by the hash of its name among the image's `[[info]]` entries.
  When it answers 1 the values come from it and the kit is never read for that effect; the row's MIX is the record's
  default. Without such a function (every image without `core/loop-control`) nothing changes.
- **From the engine.** The sixteen status words of `_loop_ui_status` return in the reply carrier of `core/dsp-link`, as
  before. A new loop count makes the home set XFAD to LOOP.
- **ARM is a press**, not a knob: press H. It is a counter the engine watches for a change, so a press arms and the next
  one takes it back; the capture uses it up.

**Two effects with native pages.** `core/fxview` ([README](../../modules/core/adapters/fxview/README.md)) owns the five
table slots of the FX view that `core/phrase-ui` used to write, and calls optional functions of the page adapters
(`phrase_fxv_*`, `loop_fxv_*`), each of which acts only on its own page. Overloop's adapter learns its page code from the
held-values call. The first version of the adapter looked for its row through the effect IDs in the image's
`fxpages_info`, as the old page's code did; the fx service keeps those in its own copy of the table, so the lookup never
matched on the emulated device and the page was never on top. Whether the old page had the same fault was not measured.

## The page

Section 3.2 of the plan, built as `core/loop-display` ([README](../../modules/core/adapters/loop-display/README.md)):
state at the top right, two text lines, the timeline with a mark, the crossfader as it sounds, `ARM` and `XFAD`. Where it
differs from the plan's drawing:

- ARM is the press of H; H and LEVEL turn XFAD. The plan had ARM as a knob "because a plain effect page has no keys";
  with the dispatcher it has.
- The line says `NEXT CHANGE KEEPS` and below it `16 ST 2.00 S`; the pattern's name (`A02`) is not shown. The engine
  knows the pattern's identity word, not its name.
- There is no pattern cell at the left of the header.
- FUNC + NO drops the loop here too.
- The buffer notice is `LAST 21.5 S ONLY` while armed and `CUT TO ITS LAST PART` on the loop.

## An old kit

A kit saved with the earlier Overloop holds raw values for the row's four knobs (REC, START, FADE, LENGTH) and its MIX.
The effect is still `digikit/overloop` with the effect id `loop` (effect ID `0xca1b`), so the kit loads as before. Its
values are not read: for a held effect the service never calls the kit read (host test
`held_values_come_from_the_adapter_and_never_from_the_kit` in `fxpage.rs`, which panics if the kit is read), and MIX is
127. The owner sees Overloop's page with the crossfader at LIVE, no loop and nothing armed after every restart, in every
kit, and no change of them at a pattern or kit change. The old values stay in the saved kit, unused.

## For step 4

- **The tap.** `_loop_tap` (68 words, `LT_*` in `dk_loop.inc`): magic `LTAP`, version 1, the block count of the block it
  was written in, a flag (1: a loop was mixed), 32 left and 32 right samples of loop x crossfader. Written by the engine
  in every block with a loop, cleared once when there is none. On the rig it equals the output at LOOP (difference 0) and
  loop x g in single precision at a crossfader of 30 (difference 0). A send effect runs before the master insert, so
  PRISM reads the block before: one block late, as the brief asked.
- **The home** is ready for a second page (`dkrt_loop_xfad`, `dkrt_loop_arm`, `dkrt_loop_drop`, `dkrt_loop_status`).
- The phrase control words keep their places for INPT, XFAD and ARM (finding 359); nothing writes them.

## Open

- **[O]** Hardware: nothing ran on a device. The position record's timing against the heard boundary is from the
  emulator (finding 133 and the device test below).
- **[O]** A second armed change sooner than (L + 480) / 2 frames after the first holds silence in its first part.
- **[O]** The home sets XFAD to LOOP when it reads a loop count it has not seen. An engine that starts anew and takes a
  loop within one UI pass, with the count the home last saw, is not followed (the loop is taken, the crossfader moves to
  where the control stands). Not reached in a test.
- **[O]** Left in the tree and unused: the painter path of the fx service (`fxp_graph` calling an `[[info]]` entry
  `loop_ui_paint`, which no module has any more), and two comments in `core/platform` that name `core/loop-controls`
  (the shared platform object was not rebuilt for a comment).
- **[O]** One holder of held values: the service asks one function. A second module with values outside the kit needs a
  list.
- **[O]** `mise run patch:port` fails in this checkout at an unrelated anchor (finding 359). No anchor or port entry
  changed in this step; the five FX view anchors of `devices/fw/phrase-ui.toml` only name `core/fxview` as their user.

## Proofs

| Layer | Command | Result |
| --- | --- | --- |
| Rig | `pnpm nx:pilot run digikit/overloop:test-rig` | 23 cases pass; the numbers above are its `out/runs/overloop-engine/measures.txt`. It also holds every store of the module, under a hostile context, to its state, its output, the two buffers, the status and the tap |
| Host | `core/loop-control:test-host`, `core/loop-display:test-host`, `dkrt-svc-fx:rust-tests` | pass: the home (values, counters, the held answer, the page code, a new loop), the page model (ARM one-shot, XFAD, every text, the cut notice), held values never from the kit |
| Assembly | `core/runtime:test-assembly` | pass; one new case: the image of the `overloop` set has the `[[info]]` entry the service looks up by name hash, the `emulator` set has none |
| Device (DT2 emulator, SHARC core) | `digikit/overloop:test-device` | pass, see below |

`modules/fx/overloop/tests/transition.device.test.ts`, with the test instrument on two patterns (trigs on steps 1, 5, 9,
13 and on 1, 2, 4, 7, 8, 11, 15, 16) at 120 BPM and no kit helper in the image:

- the engine reports a cycle of 96,000 frames after pattern 1's first cycle; the page says `CYCLE 16 ST 2.00 S`;
- press H: `ARMED`, `NEXT CHANGE KEEPS`, `16 ST 2.00 S`;
- PTN + TRIG 2: the loop is taken at the end of the cycle, 96,000 frames, the home's XFAD is 127, the page `LOOP`,
  `LOOP 16 ST 2.00 S`, `XFAD LOOP`;
- the main output, captured from before the ARM until after the crossfader's move: each sample against the one 96,000
  frames earlier, across the change and 2.2 turns of the loop, differs by 0 (largest and RMS, relative to the peak);
- XFAD to 64: the engine's level is 64/127, the page `XFAD 64`; to 0: level 0, `XFAD LIVE`, the loop still held;
- at LIVE the cycle that follows differs from the one before it (the loop of pattern 1) by 1.85 x the peak at most, and
  the cycle after that repeats it with difference 0: pattern 2 is heard, alone;
- STOP, XFAD 50, ARM, PTN + TRIG 1: no loop is taken, the arm stays, XFAD is 50 (`LOOP+ARM`, `XFAD 50`); a second press
  takes the arm back; PLAY and an unarmed change to pattern 2 leave the loop, its 96,000 frames and XFAD 50;
- FUNC + NO: no loop, `NO LOOP`;
- a cycle longer than the buffers, with the second buffer's frames set to 65,536 by a debug write to the DSP: the engine
  starts anew in the smaller buffers, the armed page says `LAST 1.36 S ONLY`, and the armed change takes a cut loop of
  65,024 frames (`LOOP 16 ST 1.36 S`, `CUT TO ITS LAST PART`).
