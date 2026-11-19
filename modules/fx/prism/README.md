# PRISM

PRISM is a stereo repeater on one tape: a delay whose time is picked in eight zones, with repeats, a colour filter and a
diffusion network inside the repeat path, a skew between the two sides, reverse and hold. It is modelled on how the Make
Noise Mimeophon behaves; whether it sounds like one is for ears to decide, not for a test.

It is a send effect of the effect framework: each track has a PRISM send amount, PRISM's output is added to the FX
return, and the dry tracks are not touched. Steps 1 and 2 of [the redesign](../../../docs/plans/prism-overloop-redesign.md)
are built: the engine, proven on the SHARC-only rig ([finding 358](../../../docs/findings/358-prism-repeater-engine.md)),
and the send path with the one page, proven on the emulated Digitakt II
([finding 359](../../../docs/findings/359-prism-send-effect-and-page.md)). Nothing ran on hardware, and nobody has listened.

The first target is Digitakt II OS 1.17. The engine contains no firmware address: its audio comes from the effect
framework, its controls from the control mailbox it imports, its tape from the storage lease it imports.

## Where the sound comes from and goes

- **In:** the tracks, each times its PRS send amount. The amount is on the second page of the track's FX key (press FX
  twice), the cell `PRS`, 0 to 127; the gain is the amount squared over 127 squared. All amounts start at 0, so PRISM is
  silent until one is raised. The amounts are kept in digikit's settings on the +Drive (`fx/send/digikit/prism`), not in
  kits or projects.
- **Out:** the wet signal times MIX, added to the FX return.
- PRISM takes the first free cell of that send page when it loads (the Digitakt II has two). It does not displace a stock
  effect: delay, reverb and chorus keep their sends on the first page. With both cells taken by other digikit send effects
  that load before it, PRISM loads and is not run.

## The page

FUNC + FX opens the FX pages; PRISM's is one of them (send effects come first). The real screen:

```
 PRISM                              ECHO    387 MS
─────────────────────────────────────────────────
 MIX   ZONE       RATE       RPTS       COLR
 ┌─┐   ECHO       387 MS     48         0
 │ │   ····▮···
 │█│              FREE
 │█│  ───────────────────────────────────────────
 │█│   HALO       SKEW       INPT       XFAD
 └─┘   0          0          LIVE       --
 100                                    NO LOOP
```

| Control | Does |
| --- | --- |
| LEVEL | MIX, 0 to 127 |
| A | ZONE, four ticks a zone. Press: FLIP (`FLIP` under the zone name, inverted) |
| B | RATE. Free: the time in milliseconds, a coarse step a tick; with FUNC held a fine step (1016 of them across the zone). Press: SYNC on or off (`SYNC` or `FREE`); with SYNC the encoder steps through sixteen note lengths, four ticks each, from 1/32 to two bars |
| C | RPTS, 0 to 127; 96 holds a repeat at its level. Press: HOLD (`INF` and `HOLD`, inverted) |
| D | COLR, 64 is the centre: the cell shows the distance and `DARK` or `BRIT` |
| E | HALO, 0 to 127 |
| F | SKEW, 64 is the centre: `L 12` means the left side is the longer one by 12 steps, `R 12` the right |
| G, H | INPT and XFAD: drawn in their place for the loop (steps 3 and 4), and nothing moves them yet. Their presses, and FUNC + NO, do nothing |

The header shows the zone and the time the engine plays now (it glides after a free RATE move). The values are kept in
digikit's settings (`phrase/digikit/prism`), the same for every kit and project; HOLD is not kept. UP, DOWN and FUNC + FX
move between the FX pages as on any of them.

MIX is this page's and only this page's: the effect row's own MIX of the framework stays at unity, and a kit holds no
PRISM value.

## Controls of the engine

The engine reads control words (`sdk/dsp/contract/dk_phrase.inc` has the layout, `src/prism.h` the constants that can be
tuned); `core/phrase-control` fills them from the page's values.

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
 input (the tracks by their PRS amounts) ────────────┐
                                                     ▼
 tape ──► two heads a side ──► DC blocker ──► COLR ──► HALO ──┬──► x MIX ──► FX return
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
pnpm nx:pilot run digikit/prism:test-rig          # the engine on the SHARC-only rig, 47 cases, about 2.5 minutes
pnpm nx:pilot run core/phrase-control:test-host   # the control provider: values to control words, the SYNC note table, the transport
pnpm nx:pilot run core/phrase-ui:test-host        # the page: encoders, presses, the cells' texts, what is drawn
pnpm nx:pilot run digikit/prism:test-device       # the emulated Digitakt II: the page, the send path, beside CANOPY and Overloop
pnpm nx:pilot run digikit/prism:pack              # the package for the Digitakt II
```

`tests/engine.rig.test.ts` runs the assembled engine behind the production effect adapter, with stand-ins for the storage
descriptor and the two mailboxes (`tests/provider.s`, `tests/engine-rig.ts`). Each claim above has a number there: the
click measure at every kind of jump, the pitch during a glide and through a jump, the level from repeat to repeat, the
tilt a repeat at both ends of COLR, HALO at 0 against a build without it, the times of the two sides, the held loop turn
against turn and the bank's words while held. It prints the numbers at its end. Every block of every case is held to the
effect contract (the hook returns, the registers come back, no fault, finite output), the module's stores are held to
its state, its output and its bank, and the heaviest block is the `dsp_cost_fx` of `module.toml` (17,381 instructions,
core/fx included; rig instructions, not a cycle count of the SHARC).

The device tests run the patched firmware with the SHARC core. `tests/ui.device.test.ts`: the page opens, each encoder
and LEVEL moves its value, its control word and its cell, the three presses switch, FUNC + RATE is fine, G and H change
nothing. `tests/send.device.test.ts`: a note of a track with a PRS amount comes again after the engine's time at the gain
of the send law times MIX; a track with amount 0 is not heard by PRISM; with every amount at 0 the output is, sample for
sample, the one of the card without PRISM. `tests/coexistence.device.test.ts`: CANOPY, Overloop, PRISM and remote on one
card.

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

## What is not there yet

- INPT (PRISM's input from Overloop's loop), XFAD (Overloop's crossfader) and ARM are steps 3 and 4 of the redesign. The
  control and status words have their places for them (`PH_C_INPUT`, `PH_C_XFADE`, `PH_C_ARM_SEQ`, `PH_S_INPUT`,
  `PH_S_LOOP`, `PH_S_ARMED`), all zero now.
- Nothing ran on hardware, and nobody has listened.
