# PRISM

PRISM is a stereo repeater on one tape: a delay whose time is picked in eight zones, with repeats, a colour filter and a
diffusion network inside the repeat path, a skew between the two sides, reverse and hold. It is modelled on how the Make
Noise Mimeophon behaves; whether it sounds like one is for ears to decide, not for a test.

It is a send effect of the effect framework: each track has a PRISM send amount, PRISM's output is added to the FX
return, and the dry tracks are not touched. With Overloop it plays a transition: it hears Overloop's loop instead of the
tracks (INPT), and Overloop's crossfader and ARM are on its page. Steps 1 to 4 of
[the redesign](../../../docs/plans/prism-overloop-redesign.md) are built: the engine
([finding 358](../../../docs/findings/358-prism-repeater-engine.md)), the send path with the one page
([finding 359](../../../docs/findings/359-prism-send-effect-and-page.md)), and the loop as its input with the transition on
its page ([finding 361](../../../docs/findings/361-prism-loop-input-and-return.md)), proven on the SHARC-only rig and the
emulated Digitakt II. Nothing ran on hardware, and nobody has listened.

The first target is Digitakt II OS 1.17. The engine contains no firmware address: its audio comes from the effect
framework, its controls from the control mailbox it imports, its tape from the storage lease it imports.

## Where the sound comes from and goes

- **In:** the tracks, each times its PRS send amount. The amount is on the second page of the track's FX key (press FX
  twice), the cell `PRS`, 0 to 127; the gain is the amount squared over 127 squared. All amounts start at 0, so PRISM is
  silent until one is raised. The amounts are kept in digikit's settings on the +Drive (`fx/send/digikit/prism`), not in
  kits or projects.
- **In, with INPT at LOOP:** Overloop's loop, at the level its crossfader gives it, times a fixed trim of -6 dB
  (`PRISM_LOOP_TRIM` in `src/prism.h`, to be set by ear); the track sends do not reach PRISM then. Without Overloop or
  without a loop that side is silence.
- **Out:** the wet signal times MIX, added to the FX return. **While Overloop is loaded, PRISM's output goes to Overloop
  instead**, which adds it after its crossfader at the same level, so PRISM is heard in full at every position of the
  crossfader, and the loop does not record it (a loop that held PRISM's echoes would play them twice). In plain words: with
  Overloop on the card, PRISM's echoes do not pass through the Digitakt's master compressor, its master overdrive or the clip
  of the master stage, also when no loop is playing; the master volume still applies to them. Without Overloop they go
  through the master as any send effect's return does.
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
| G | INPT, a switch: LIVE (PRISM hears the tracks by their sends) or LOOP (PRISM hears Overloop's loop). A press or a turn switches, only while there is a loop; the switch crossfades over 5 ms |
| H | XFAD, Overloop's crossfader: the same value as on Overloop's page (`LIVE`, a number, `LOOP`; below it the crossfader as it sounds). Turns only while there is a loop. Press: ARM (`ARMD`, and below it the steps the next change keeps); a second press takes it back |
| FUNC + NO | Drop the loop |

The header shows the zone and the time the engine plays now (it glides after a free RATE move). The values are kept in
digikit's settings (`phrase/digikit/prism`), the same for every kit and project; HOLD is not kept. UP, DOWN and FUNC + FX
move between the FX pages as on any of them.

MIX is this page's and only this page's: the effect row's own MIX of the framework stays at unity, and a kit holds no
PRISM value. INPT, XFAD and ARM are not kept anywhere: after a restart there is no loop, INPT is LIVE and the crossfader at
LIVE; a pattern change does not move them. While PRISM hears the loop the header says `LOOP 16 ST 2.00 S` (its steps and
seconds) in place of the zone and the time.

## Performing a transition

At the Digitakt, with PRISM and Overloop loaded, PRISM's page open (FUNC + FX until it shows) and the tracks you want PRISM
to hear given a PRS amount:

1. The pattern plays; PRISM echoes it. Press **H**: the XFAD cell says `ARMD`.
2. Choose the next pattern the usual way (PTN and a TRIG key). At the end of the playing pattern the loop takes over:
   XFAD goes to `LOOP`, INPT goes to `LOOP` by itself, and the header shows the loop. You hear the old pattern go on with
   its echoes; the new pattern runs underneath, unheard.
3. Play PRISM on the old pattern: ZONE, RATE, RPTS, COLR, HALO, MIX, press C to hold. The new pattern does not reach PRISM.
4. Turn **H** toward LIVE. The new pattern comes in dry; the loop and PRISM's feed fade together, and what PRISM holds rings
   on as RPTS lets it.
5. At LIVE: the new pattern alone, with the last echoes of the old one. Set PRISM up for the new pattern.
6. Press or turn **G**: INPT goes to LIVE and PRISM works on the new pattern by its sends.

The loop stays until the next armed change or **FUNC + NO**; with it dropped INPT is LIVE. If you put INPT at LIVE while the
crossfader still stands at LOOP, PRISM hears the new pattern by its sends and you hear its echoes over the loop: allowed,
but the new pattern then reaches the output through PRISM before you turn H.

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
pnpm nx:pilot run digikit/prism:test-rig          # the engine on the SHARC-only rig, and PRISM coupled with Overloop, about 3 minutes
pnpm nx:pilot run core/phrase-control:test-host   # the control provider: values to control words, the SYNC note table, the transport
pnpm nx:pilot run core/phrase-ui:test-host        # the page: encoders, presses, the cells' texts, what is drawn
pnpm nx:pilot run digikit/prism:test-device       # the emulated Digitakt II: the page, the send path, beside CANOPY and Overloop, the transition
pnpm nx:pilot run digikit/prism:pack              # the package for the Digitakt II
```

`tests/engine.rig.test.ts` runs the assembled engine behind the production effect adapter, with stand-ins for the storage
descriptor and the two mailboxes (`tests/provider.s`, `tests/engine-rig.ts`). Each claim above has a number there: the
click measure at every kind of jump, the pitch during a glide and through a jump, the level from repeat to repeat, the
tilt a repeat at both ends of COLR, HALO at 0 against a build without it, the times of the two sides, the held loop turn
against turn and the bank's words while held. It prints the numbers at its end. Every block of every case is held to the
effect contract (the hook returns, the registers come back, no fault, finite output), the module's stores are held to
its state, its output, its bank and its words of the wet mailbox, and the heaviest block is the `dsp_cost_fx` of
`module.toml` (core/fx included; rig instructions, not a cycle count of the SHARC). The loop as the input and the route
have their cases there too; `tests/transition.rig.test.ts` couples PRISM and Overloop block by block (`tests/pair-rig.ts`):
the handover between the FX return and Overloop, the transition with PRISM sounding, INPT at LIVE over the loop.

The device tests run the patched firmware with the SHARC core. `tests/ui.device.test.ts`: the page opens, each encoder
and LEVEL moves its value, its control word and its cell, the three presses switch, FUNC + RATE is fine, G and H change
nothing. `tests/send.device.test.ts`: a note of a track with a PRS amount comes again after the engine's time at the gain
of the send law times MIX; a track with amount 0 is not heard by PRISM; with every amount at 0 the output is, sample for
sample, the one of the card without PRISM. `tests/coexistence.device.test.ts`: CANOPY, Overloop, PRISM and remote on one
card. `tests/transition.device.test.ts`: the transition of the plan's section 4 from PRISM's page with two patterns of the
test instrument, and the level of PRISM through Overloop against through the FX return.

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

```bash
node modules/fx/prism/tests/transition-audio.ts out/runs/prism-transition-audio   # a whole transition with Overloop: about five minutes
```

writes `transition-in.wav` (what the tracks play), `transition-out.wav` (what is heard) and a `README.txt` with the time of
every step: pattern A with PRISM, ARM, the change, PRISM played on the loop, the crossfader to LIVE over two bars, G. Both
engines run on the rig coupled as the device runs them (`tests/pair-rig.ts`); the master stage is its linear case.

## Limits and what is open

- At the armed change PRISM hears the new pattern for one block (0.7 ms): it runs before Overloop in a block and takes
  the loop from the next one. Its echo of that block is the only trace of the new pattern in PRISM.
- If PRISM is switched off (unloaded) while Overloop adds its output, that output stops without the framework's one-block
  fade.
- Nothing ran on hardware, and nobody has listened.
