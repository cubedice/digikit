# Overloop

Overloop is a looper that listens all the time. It records what is heard, the master mix behind every other effect, into a
rolling record. Press ARM, change the pattern, and at the change the last cycle of the pattern that was playing becomes a
loop: it goes on playing where the old pattern would have, the crossfader stands at LOOP, and the new pattern runs
underneath, unheard. Turn the crossfader to LIVE to bring the new pattern in.

Steps 1 to 3 of [the redesign](../../../docs/plans/prism-overloop-redesign.md) are built; this is step 3
([finding 360](../../../docs/findings/360-overloop-automatic-loop.md)). Digitakt II OS 1.17, proven on the SHARC-only rig
and on the emulated device. Nothing ran on hardware, and nobody has listened.

## Playing it

FUNC + FX opens the FX pages; Overloop's is one of them.

| Control | Does |
| --- | --- |
| press H | ARM: the next pattern change keeps the cycle that was playing. One-shot: the change uses it up. A second press takes it back |
| H, or LEVEL | XFAD, the crossfader: 0 is LIVE (the input, untouched), 127 is LOOP (the loop alone) |
| FUNC + NO | Drop the loop |

The page shows the state (`IDLE`, `ARMED`, `LOOP`, `LOOP+ARM`), what an armed change would keep (`16 ST 2.00 S`) or what
the loop holds, a timeline with a mark at the step that plays or at the loop's place, and the crossfader as it sounds.

What happens, case by case:

- **A change at the pattern's end** (the usual one): the loop is the cycle that just ended, to the block. What is heard
  after the change is what was heard one cycle earlier.
- **A direct change in the middle of a pattern:** the loop is the last L frames before the change, L the length of the
  last whole cycle. It starts in the middle of a bar and goes on from there.
- **Less than one cycle since START:** L is the frames a step has taken so far times the pattern's steps; the loop holds
  what was heard before START as well (the record runs while the transport is stopped). Frames from before the module
  was loaded are silence.
- **A change while the transport is stopped** takes no loop, and the arm stays for the next change while it plays.
- **STOP** leaves the loop playing. **START** begins it again with the pattern's first step (over 256 frames, without a click).
- **A tempo change after the capture** leaves the loop its recorded length.
- **Arming again while a loop plays** is allowed: the next change keeps what is heard then, the new pattern and
  whatever of the old loop the crossfader mixes in. If that change comes sooner than about half its cycle after the
  one before, the earliest part of the new loop is silence (the record in the other buffer is still being filled).
- **A cycle longer than the buffers** (21.5 s: 128 steps below about 89 BPM) is cut to its last 21.5 s. The page says
  `LAST 21.5 S ONLY` while armed and `CUT TO ITS LAST PART` on the loop.
- With the crossfader at LIVE the loop is silent and stays ready. It stays until the next armed change replaces it or
  it is dropped.

The crossfader, the armed state and the loop are not kit values: they are the same in every kit and do not change at a
pattern change. Nothing of Overloop is saved; after a restart there is no loop and the crossfader is at LIVE.

A kit saved with an earlier Overloop holds values for its four old knobs (REC, START, FADE, LENGTH). They are not read
any more: the kit loads as any kit does and changes nothing in Overloop.

## How it works

```
 input (the master mix) ──┬──────────────────────────────► x (1 - XFAD) ──┐
                          │                                               ├──► output ──► rolling record
                          │        loop ──► seam ──► x XFAD ──────────────┘                (the other buffer)
                          │                              └──► tap (for an effect that takes the loop as its input)
```

- **Two buffers** of 1,032,192 frames (21.5 s) from `core/loop-storage`: its loop lease and the second phrase bank
  (PRISM's tape is the first). One holds the rolling record. An armed change freezes it as the loop and the record goes
  on in the other one, where the frames heard before the change are filled in from the loop's buffer, a block each
  block, so that a second change soon after the first finds them. Both descriptors are checked every block; no address is embedded.
- **The boundary** comes from `core/pattern-position`: a step boundary begins with the block whose record counts it, as
  the notes of that step do, so the loop is a whole number of blocks and is exactly what was heard. A boundary of another
  pattern is the change; a boundary of another start generation is a START. No hook into the firmware was added.
- **The seam:** the last 480 frames of the loop run into the 480 frames before its start, so the turn is as it was
  recorded. For a pattern that repeats those are the same samples, and the turn is exact.
- **The crossfader:** output = input + (loop - input) * XFAD, moving by at most 1/256 a frame. LIVE passes the input
  bit for bit; LOOP is the loop alone. At an armed change it jumps to LOOP in the block of the change.
- **The controls** are held by `core/loop-control`, outside the kit; the fx service hands them to the engine as the
  parameter words of the effect's row. The engine publishes its status and the tap there (`sdk/dsp/contract/dk_loop.inc`).
- **The page** is `core/loop-display`.

Sources: `src/dsp/overloop.s` (the engine), `src/overloop.h` (its state and the constants that can be tuned),
`src/module.c` (the effect's declaration).

## Verification

```bash
pnpm nx:pilot run digikit/overloop:test-rig        # the engine on the SHARC-only rig, 23 cases, about 15 s
pnpm nx:pilot run core/loop-control:test-host      # the control home
pnpm nx:pilot run core/loop-display:test-host      # the page: keys, turns, the texts of every state
pnpm nx:pilot run digikit/overloop:test-device     # the emulated Digitakt II: an armed pattern change with two patterns
pnpm nx:pilot run digikit/overloop:pack            # the package for the Digitakt II
```

`tests/loop.rig.test.ts` runs the assembled engine behind the production effect adapter as a master effect, with
stand-ins for the storage descriptors, the position record, the status mailbox and the tap (`tests/provider.s`,
`tests/loop-rig.ts`). Each case above has a number there, and the heaviest block is the `dsp_cost_fx` of `module.toml`.
`tests/transition.device.test.ts` plays two patterns of the test instrument on the patched firmware, arms, changes the
pattern, and compares what is heard across the change with one cycle earlier.

### Listening

```bash
pnpm nx:pilot run-many -t build,cli -p sharc-rig patchkit          # the rig and patchkit binaries, once
node modules/fx/overloop/tests/render-audio.ts out/runs/overloop-audio
```

writes `<name>-in.wav` and `<name>-out.wav` for a change at the pattern's end and for a direct change in the middle of a
pattern, and a `README.txt` with the time of every event. The files are renders of the rig, not recordings of a device,
and are never committed.

## What is not there yet

- PRISM does not take the loop as its input, and PRISM's page does not show the crossfader or ARM (step 4). The tap and
  the control home are there for it.
- Nothing ran on hardware, and nobody has listened.
