# PRISM

PRISM is a stereo repeater on one tape: a delay whose time is picked in eight zones, with repeats, a colour filter and a
diffusion network inside the repeat path, a skew between the two sides, reverse and hold. It is modelled on how the Make
Noise Mimeophon behaves; whether it sounds like one is for ears to decide, not for a test.

This is step 1 of [the redesign](../../../docs/plans/prism-overloop-redesign.md): **the engine**, proven on the SHARC-only
rig ([finding 358](../../../docs/findings/358-prism-repeater-engine.md)). The page, the send route and the control
provider still belong to the engine before it (steps 2 to 4), so on a device image PRISM does not work on this branch: see
"What does not work yet".

The first target is Digitakt II OS 1.17. The engine contains no firmware address: its audio comes from the effect
framework, its controls from the control mailbox it imports, its tape from the storage lease it imports.

## Controls

The engine reads control words (`src/prism.h` has the table and the constants that can be tuned). No page drives them yet.

| Control | Range | What it does |
| --- | --- | --- |
| ZONE | 1..8 | Picks the time range: 1 to 10 ms, 5 to 40 ms, 20 to 120 ms, 60 to 350 ms, 150 ms to 1 s, 0.5 to 2.7 s, 1.3 to 5.4 s, 2.7 to 10.7 s. A change crossfades to the new time; the pitch does not bend. |
| RATE | 0..1 | The time inside the zone, `low * (high / low) ^ RATE`. A change glides: the pitch of what is on the tape bends, like tape (at most an octave down or a fifth up). |
| SYNC | on, off | On: the time is the frames the control provider gives (a note length at the tempo). Each step crossfades. |
| RPTS | 0..1 | Repeats. 0 is one echo; 0.75 holds a repeat at its level for ever; above it the level grows by up to 1.1 a repeat until the limiter holds it. |
| COLR | -1..1 | Colour of the repeats. Centre: flat. Toward dark every repeat loses highs (down to a low pass at 800 Hz), saturates a little and drifts slowly in pitch. Toward bright every repeat loses lows (up to a high pass at 1 kHz). |
| HALO | 0..1 | Smears every repeat a little more than the last. 0 is an exact bypass. It never changes the level of the repeats. |
| SKEW | -1..1 | Above 0 the left side is shorter and the right longer by the same ratio (up to 1.5); below 0 the reverse. A change glides. |
| FLIP | on, off | The repeats play backwards: each stretch of the time is read from its end to its start. |
| HOLD | on, off | The input is closed and what is on the tape repeats without loss. ZONE, RATE and FLIP then pick a part of the held sound and leave the tape as it is. Releasing records again. |
| MIX | 0..1 | Level of the wet signal. The engine writes the wet signal only. |

Every jump of the read position (a zone change, a SYNC step, FLIP, HOLD, the seam of a reverse stretch or of a held loop)
is a crossfade of 20 ms between two read heads with equal power.

## The engine

```
 input ──────────────────────────────────────────────┐
                                                     ▼
 tape ──► two heads a side ──► DC blocker ──► COLR ──► HALO ──┬──► x MIX ──► output (wet only)
  ▲                                                           │
  └──────────── + ◄──── limiter ◄──── x gain of RPTS ◄────────┘
```

- `src/dsp/prism.s`: the entry, the checks of the mailbox and the lease, the plan of a block (times, hold state, each
  side's head program), the status.
- `src/dsp/heads.s`: the read heads: glide, crossfade, reverse segments, the held loop.
- `src/dsp/loop.s`: the DC blocker and the colour filter; the output, the limiter and the write to the tape.
- `src/dsp/halo.s`: the four delay lines as all-pass filters.

The tape is the first bank of the storage lease, 1,032,192 stereo float frames on the device (21.5 s); the longest time
is half of it. COLR and HALO sit between the heads and the tape, so each pass through the loop adds to them. While HOLD
stands the tape is not written, so the held sound cannot degrade; COLR and HALO then work on the output only. Details and
the reasons for each choice: finding 358.

## Verification

```bash
pnpm nx:pilot run digikit/prism:test-rig     # the engine on the SHARC-only rig, 47 cases, about 2.5 minutes
pnpm nx:pilot run digikit/prism:pack         # the package for the Digitakt II
```

`tests/engine.rig.test.ts` runs the assembled engine behind the production effect adapter, with stand-ins for the storage
descriptor and the two mailboxes (`tests/provider.s`, `tests/engine-rig.ts`). Each claim above has a number there: the
click measure at every kind of jump, the pitch during a glide and through a jump, the level from repeat to repeat, the
tilt a repeat at both ends of COLR, HALO at 0 against a build without it, the times of the two sides, the held loop turn
against turn and the bank's words while held. It prints the numbers at its end. Every block of every case is held to the
effect contract (the hook returns, the registers come back, no fault, finite output), the module's stores are held to
its state, its output and its bank, and the heaviest block is the `dsp_cost_fx` of `module.toml` (17,379 instructions,
core/fx included; rig instructions, not a cycle count of the SHARC).

### Listening

```bash
pnpm nx:pilot run-many -t build,cli -p sharc-rig patchkit               # the rig and patchkit binaries, once
node modules/fx/prism/tests/render-audio.ts out/runs/prism-audio        # every case: about 2.5 minutes, 210 MB
node modules/fx/prism/tests/render-audio.ts out/runs/prism-audio hold-moves zone5-burst   # or the named ones
```

writes `<name>.wav` (the input plus the wet signal, each at half level), `wet/<name>.wav` and, for a full run,
`README.txt` with each file's input, controls and changes: every zone with a plucked note and with a chord, RATE moved
free and in SYNC steps, zone changes, RPTS from 0 to 1, COLR from dark to bright, HALO in steps, SKEW, FLIP, and HOLD with
ZONE, RATE and FLIP moved. It prints one line a file (`hold-moves: 28.0 s of audio in 28 s`). The files are renders of the
rig, not recordings of a device, and are never committed.

## What does not work yet

- The control provider, the page and the routing (`core/phrase-control`, `core/phrase-ui`, `core/phrase-route`) still
  publish version 2 of the control words, for the engine before this one. This engine reads version 3 and refuses their
  mailbox: on a device image it faults every block and plays dry. `digikit/prism:test-device` (`ui`, `capture`,
  `coexistence`) and `digikit/prism:test-conformance` are expected to fail until step 2.
- Nothing ran on the emulated device or on hardware, and nobody has listened.
