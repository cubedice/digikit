# 365. The playing pattern against the scheduled one, the change modes, and the loop at STOP

What the owner found on the Digitakt II with image `8278188469b2dc9f` (main, `cd10e1a3c`): a queued pattern made Overloop
take its loop and move its crossfader to LOOP too soon, and a held loop played on after STOP. Digitakt II OS 1.17, read
in the image, on the host, on the ColdFire core, on the SHARC-only rig and on the emulator with the SHARC core. Nothing
here ran on hardware and nobody has listened. Marks as in `docs/FINDINGS.md`; every result here is **[D]** (measured or
read once in this work) unless it is marked **[O]** or **[C]**. No firmware fact here was checked by a second agent.

## The two pattern words of the sequencer

Read in the DT2 1.17 image (the Ghidra database `dt2-1.17-emac`, MAIN SHA-256 `a1e7b657…6d964`). The substep tick is the
timer interrupt `FUN_400d7b92` (vector `0x400001e4`, installed by `FUN_400d77ce`), which runs every 900,000 scheduler
ticks (six to a 16th at scale 1x) and schedules the notes of a step about one step ahead:

- `0x4316501c` is the pattern the tick **schedules** from (finding 133's "scheduled-pattern pointer").
  `0x43165020` is the queued pattern, `0x43165024` the one before the last switch.
- `0x44f37bd0 + 4t` is the pattern of track t's **current** step (`t` from 0). The tick sets the live word
  `0x44f37c54 + 4t` to `0x4316501c` when the track's step becomes current (its substep counter wraps), and copies the
  sixteen words to `0x44f37bd0` with the step words `0x44f37ba0` (current step), `0x44f37bb0` (next step) and the
  substep `0x44f37bc0` (`0x400d8492` to `0x400d8504`). START and CONTINUE (`FUN_400d624a`) and a second start path (the
  interrupt at `0x400d5bb4`, which runs when `FUN_400d6068` has set `0x43158f88`) set it to `0x4316501c`. The firmware's
  own reader is `FUN_400d8e00` (five callers from `0x4011f5c4` to `0x401204ec`): at an audio time it takes track t's
  current step and that step's pattern from these words, or the next step and `0x44f37c10 + 4t`.

A **change at the end** (sequential). At substep 2 of the second-to-last step the tick asks the mode object
(`0x4316513c`, method 1) for the next pattern and marks the switch (`0x43165128`, `0x43165020`). At substep 0 of the
last step it sets the current master step `0x43164f9c` to the last step, then switches: `0x43165024` = the old pattern,
`0x4316501c` = the new one, the master length override `0x43165130` its value, the next step `0x43164f98` the new
start step (`0x400d8616` to `0x400d8668`). The last step's notes were scheduled a step before, from the old pattern; the
new pattern's first notes are scheduled in the next tick for the edge after, where its first step is heard. So
`0x4316501c` moves **one step early**, and with it the length the provider read from it. The track words
`0x44f37bd0` move at that next edge, when the new pattern's first step becomes current: for every track, also with
per-track scales (a track slower than the master grid gets its substep counter set so that it wraps there; a faster one
takes the new pointer for an old step in between, so the provider takes it only at a master edge, below).

**Direct start and direct jump.** A request (`0x431650e0`, target `0x431650d0`) sets a countdown `0x431650dc` to the
substeps left in the step; when it ends, at the top of a tick, `0x4316501c` becomes the target, the start step is 0
(direct start, `0x431650d8` set) or the next step modulo the new length (direct jump), the substep is set so that it
wraps in the same tick, and every track's substep counter so that it wraps there too (`0x431650c8`). So both words move
in the same tick, at the master edge where the new pattern's first step begins. The provider was right for these before.
**[O]** Where the UI writes the request was not found statically (no absolute reference to `0x431650d0` or `0x431650e0`
outside the tick); the modes were not selected on the emulated device.

**Temp jump.** **[O]** Not analysed. The tick has a second end condition (`0x4316503c`, a step at which the pattern
ends, with `0x43165038` the start step of the one after), which the jump back may use; whichever of the two paths above
it takes, the provider below follows it.

**Confirmed on the emulator** (below): the scheduled word moves to pattern 2 about one step (125 ms at 120 BPM) before
the track word does.

## What Overloop did with it

`core/pattern-position` froze `0x4316501c` and its length (`0x43165130`, else pattern + `0x1db74`) into each boundary
marker. At a queued change the marker of the old pattern's last step carried the new pattern, so Overloop counted the
change there: the loop was taken and the crossfader went to LOOP one step before the new pattern was heard, and what was
played live in that step was lost (the loop is one cycle earlier). When the new pattern is shorter than the old one's
last step (16 steps to 8), the marker's step was not a step of its length: the provider marked the timing invalid, the
run ended, and the armed change took **no loop at all**. Overloop's existing device test could not see the early cut:
until the real switch the loop of pattern 1 sounds like pattern 1 itself.

## The fix: the boundary carries the playing pattern

`core/pattern-position`'s marker now carries the pattern whose step begins at the boundary, and its length:

- The playing pattern is track 1's word `0x44f37bd0`, read through a new data anchor, `cf.pattern.site.playing_lit`:
  the literal of the tick's `lea 0x44f37bd0,a6` at `0x400d84ae` (rule `cf_window_hash`, 12 bytes from `0x400d84ac`,
  offset 2, unique in MAIN; in `devices/fw/pattern.toml` and the DT2 1.17 port file, entered by hand because
  `mise run patch:port` fails at an unrelated anchor, finding 359). No hook is added: the existing tick hook reads it.
- It is taken at a master step edge (the substep `0x43164f9a` is 0) and at START; between edges the pattern taken at the
  last edge stays, so a track faster than the master grid cannot move it into a step.
- Its length: the scheduled pattern's (override first) when the two are the same; while the scheduled one has moved on,
  the length taken for the playing one at the edge before; else its own (`+0x1db74`). Its scale for the clock period
  (`+0x1db79`, `+0x1db7a`) is the playing pattern's.
- `pp_current_pattern()` still returns the scheduled pointer.

The ABI does not change: word 6 of `_pattern_position_info` (and `pp_position[6]`) was an opaque pointer and still is;
it now moves at the boundary where the new pattern is heard. No Overloop code was needed for the change itself: a
boundary of another pattern is the change, as before.

| Mode | Before | Now |
| --- | --- | --- |
| Sequential (at the end) | the loop a step early; none when the new pattern is shorter than the old one's last step | at the block where the new pattern's first step begins; the loop is the cycle that ended, exactly |
| Direct start | at the boundary where the new pattern begins (right) | the same |
| Direct jump | at that boundary (right) | the same |
| Temp jump | **[O]** not measured | **[O]** not measured |

**For CANOPY** (`core/instrument-play`). Its ColdFire side compares the scheduled pointer with `pp_position[6]` and
holds while they differ ("a scheduled destination is not yet the sounding identity"). That window is now the whole last
step, until the new pattern is heard, instead of about one substep: it re-resolves its Sound at the real switch, not
during the old pattern's last step. Its DSP side gets the pattern word as the instrument input's `pattern`; the
wavetable's pattern-phase reset at a change (identity change, or a boundary of step 0) now comes once, at the switch,
where it came at the early edge and again at the switch before. CANOPY's arp and tempo take only the tempo and the clock
record, which are unchanged. **For PRISM's SYNC** (`core/phrase-control`): it reads only the tempo in word 5; nothing
changes.

## The loop at STOP

Overloop's engine follows the transport in the record: the RUNNING flag (word 5) and the raw transport (word 14: 0
stopped, 1 running, 2 paused).

- **STOP, or a pause**: at the first record that does not run, the loop side fades out over 256 frames (5.3 ms) with the
  eased weight a·a·(3 - 2a), and from then on the input passes as at LIVE, bit for bit: live playing (the keys, CANOPY,
  the input) is heard whatever XFAD says. The crossfader takes its target at once while nothing of the loop is heard (a
  drop completes then). The loop, XFAD and an arm are kept. The tap is cleared once (`LT_AUDIBLE` 0): PRISM with INPT
  at LOOP hears silence. The status shows `LS_STOPPED` (64).
- **START**: at the first boundary after the transport runs again the loop begins at its first frame with the pattern's
  first step and fades in over 256 frames to where XFAD stands.
- **CONTINUE** after a pause: the loop **goes on where it stood** when the transport paused (the phase is kept at the
  pause), and stays in time with the pattern. Restarting at the first frame would put the loop's first step under the
  pattern's paused step. On the emulated DT2 the stock CONTINUE (`FUN_400d624a` with the transport at 2) repeats the
  paused step as the first boundary of a new start generation, and the sequencer goes on from the substep it had
  scheduled when it paused (`0x431650ee`), which is ahead of what was heard: with the loop going on from where it stood,
  the loop lagged the pattern by 608 to 848 frames (12.7 to 17.7 ms at 120 BPM; the samples of the device test). So
  at the step after the repeated one the engine sets the loop to where that step begins in it (the phase at the last
  boundary before the pause plus the last whole step, the old position fading out under the new one over 256 frames);
  from there it is in time to a block. Without the repeated boundary (not seen) it would begin at that place at once.
  A pause and then STOP make the next START begin at the first frame.
- A START while the loop is still heard (no stop seen, or within the fade out) fades the old position out under the new
  one as before (finding 360).

**[C]** Finding 360's "STOP while a loop plays: the loop goes on" no longer holds; its START row holds with the fade in.

The engine's state grew from 240 to 288 bytes (the transport, the last boundary for a CONTINUE); its heaviest block
(`dsp_cost_fx`) from 5028 to 5323 instructions.

## Proofs

Lowest layer first, in the WSL checkout at the branch head; device tests on the DT2 1.17 image on the emulator with the
SHARC core.

| Layer | Check | Result |
| --- | --- | --- |
| Host, C | `core/pattern-position:test-host`: `tests/playing_harness.c` runs `pp_playing` with the queue: START, 15 edges of A (16 steps), the edge of A's last step with the scheduled pointer at B (8 steps) carries A, step 15, length 16; its substeps queue nothing; a track word at B between edges moves nothing; B's first step comes at the next edge with length 8; a direct change, the same step again with another pattern, the override, a playing pattern that kept its length, one with its own | pass |
| ColdFire core | `tests/patchkit/test_pattern_position_cf.py` (`cf_script`): `pp_snapshot` and `pp_due` with the firmware image's own literal at `0x400d84ae` and the tick's words poked: the four commits are (1, A, 0, 16), (2, A, 14, 16), (3, A, 15, 16), (4, B, 0, 8), the frame carries B, step 0, 8; the anchor's window resolves once | 23 passed |
| Rig | `digikit/overloop:test-rig`, 45 cases (the 36 of finding 363 kept): STOP fades the loop side out with the eased weight bit for bit, click 0.89 x steady, then the input (the keys) bit for bit, the tap 0 and `LT_AUDIBLE` 0, loop and XFAD kept, `LS_STOPPED`; START: the loop from its first frame after the fade in (difference 0), click 1.00, XFAD 100 set while stopped is the law bit for bit after it; PAUSE at loop phase 800 and CONTINUE: the loop from 800, the next step on the loop's step, click 0.88; a CONTINUE 64 frames ahead with the repeated step and one without it: the loop at the next step's place after 256 frames (difference 0); a pause then STOP: START at the first frame; an armed change while stopped takes nothing and the next one while playing does; a change queued in step 3: sequential, direct start and direct jump take the loop exactly where B begins (frames 7680, 6080, 6080; B's steps 1, 1, 4), the output up to there the input with the live sound bit for bit; the control with the old record (`identity: 'scheduled'`): the loop a step early (frame 7360), the live sound of that step lost (0.144), and no loop for a change to 4 steps (with the playing pattern: the loop). Heaviest block 5323 instructions (`dsp_cost_fx`) | pass |
| Device | Overloop `transport` (new): pattern 1 with trigs on 1, 5, 9, 13, ARM, pattern 2 queued in step 2 to 4, track 1 played on its key from then on; sampled every 20 ms: the scheduled word moves to pattern 2 at frame 247200 of the capture, the playing word at 252960 (5760 later; record step 15 then), the record's identity and the loop count at 253920, and the output equals one cycle earlier from 253536 (the keys are heard up to there): the cut is at the real switch, not at the scheduled one; the loop is the cycle (96000), XFAD 127. STOP: the status stopped, XFAD 127, the tap's flags and samples 0, the 24 windows of 50 ms after STOP only fall (largest rise -0.0077: the pattern's notes ring out, no note of the loop starts), the key heard (1.0 of the peak against 0.12 ringing); START: the loop from its first frame 96 frames after PLAY. PAUSE (PLAY): raw transport 2, stopped; CONTINUE: a new generation, and every sample of the loop's phase lies in the step the record plays (before the realignment at the next step it lagged by 608 to 848 frames) | pass |
| Static | lint, fmt-check, rust-fmt, typecheck, python-checks; `check:layers`; `patchkit:objects-check`; `patchkit gen --check`; device-literals; `core/runtime:test-assembly`; `dkrt-svc-fx:rust-tests`; host tests of pattern-position, loop-control, loop-display, phrase-ui, phrase-control, instrument-play; `test-rig` and `test-conformance` of PRISM and Overloop; `@digi/sharc-rig-kit:test-conformance-rig` | pass |
| Python | `py_select run --base main` (with `DIGIKIT_CF_SCRIPT`) | fail in four files: `test_pk_current.py` and `test_pk_new.py` (known), and `test_instrument_registry.py` (2) and `test_midi_events.py` (2): an unresolved `instrument_lock_source` and a missing `core/runtime/src/cf/midi_dispatch.c` (absent on main too), in links of adapters this branch does not change; they skip without `cf_script` |
| Gate | `mise run test:subset -- A:core/phrase-ui A:core/dynfx A:core/fxpages A:core/fx A:core/instrument-play A:core/loop-display A:core/pattern-position C:transport --run`: 46 device test halves on DT2 and DN2 (Overloop's `transition` and `transport`; PRISM's `ui`, `send`, `transition`, `coexistence`; CANOPY's seven on DT2; the files of `core/fxpages`, `core/dynfx` and `core/fx`; the transport files), `gate-smoke`, `gate-pins` | all passed, 1732 s |
| Hardware set | `hw_build11.sh`: `patchkit apply --set canopy-overloop-prism`, `preflight`, `verify`, `pack-all`, `services --bind-dir` | every step exit 0; nothing installed. **Image id `9ee1ed898e39214e`**, 2,143,904 bytes; `cf.expansion` 1,318,652 of 1,396,736 bytes, `dsp.l2` 566,460 of 922,756; `overloop.dkm` 6,668 bytes, `canopy.dkm` 147,768; the image, packages and services are the ones the device tests loaded |

The gate skips every device test without these tags: the runtime's boot, storage, loading, layout and fault tests, the
LFO, sample and machine tests, the tools. Not run: `mise run test:full`, anything on hardware, the direct change modes and
temp jump on the emulated device.

The firmware facts (the two words, the switch paths, the CONTINUE) are read once in the image and confirmed on the
emulator for the sequential change and CONTINUE; no second agent checked them against the image bytes (**[D]**).

## Open

- **[O]** Hardware: nothing of this ran on the device, and nobody has listened.
- **[O]** The UI's writer of the direct-change request, and temp jump.
- **[O]** After a CONTINUE the loop lags the sequencer until the next step (608 to 848 frames on the emulated DT2 at
  120 BPM), where it is set to that step's place with a 256-frame crossfade; that place uses the last whole step's
  frames, so it is right to a block. The lag at other tempos and on the device is not measured.
- **[O]** Per-track scales: the boundary's pattern is taken from track 1's word at master edges; the analysis says every
  track's word moves at the edge where the new pattern begins, but no per-track-scale pattern was run.
