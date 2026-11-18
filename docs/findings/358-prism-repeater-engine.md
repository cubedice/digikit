# 358. PRISM as a repeater: the engine

Step 1 of the redesign in [the plan](../plans/prism-overloop-redesign.md): PRISM's DSP engine is rebuilt as a stereo
repeater on one tape. The grains, the phrase capture and the two-bank swap of findings 352 and 354 are gone. This finding
is about the engine alone, on the SHARC-only rig. Nothing ran on an emulated or a real Digitakt II, no page drives the
engine yet, and nobody has listened to it. Marks as in `docs/FINDINGS.md`; every number is **[D]**: measured once, by
`modules/fx/prism/tests/engine.rig.test.ts` unless a line says otherwise.

## The engine

Sources in `modules/fx/prism/src/dsp`; the control words, the constants that can be tuned and the state layout are in
`src/prism.h`.

```
 input ──────────────────────────────────────────────┐
                                                     ▼
 tape ──► two heads a side ──► DC blocker ──► COLR ──► HALO ──┬──► x MIX ──► output (wet only)
  ▲       (heads.s)            (loop.s)      (loop.s) (halo.s)│
  │                                                           ▼
  └──────────── + ◄──── limiter ◄──── x gain of RPTS ◄────────┘        (loop.s; the write stops while HOLD stands)
```

- **Tape**: the first bank of the lease, two planes of float frames (1,032,192 frames on the device, 21.5 s). A block's
  heads read the tape as the last block left it, then the block is written. The shortest time is 32 frames.
- **Time**: free, `low * (high / low) ^ RATE` of the zone, rounded to whole frames; with SYNC the frames the control
  provider gives (the note-length table and the frames-per-tick conversion stay on the control side, as before). The base
  time is at most half the bank (10.75 s). SKEW gives one side `time / r` and the other `time * r`, `r` = 1 to 1.5.
- **Heads** (`heads.s`): head A plays; when the read position has to jump, head B goes on with what A read and the two
  crossfade with equal power over `PRISM_XFADE` samples. A head is a delay from a reference frame and a direction;
  per sample the delay moves by the reference's advance (1 running, 0 standing) minus the direction (1 or -1). That one
  rule gives the four cases: forward on the running tape (the delay stays; a free RATE or SKEW move glides it), reverse on
  the running tape (a segment of TIME samples read backwards from 32 frames behind the write), and the held loop forwards
  and backwards.
- **Colour** (`loop.s`): a DC blocker at 2 Hz, then on the dark side a shelf toward a one-pole low pass at 800 Hz with a
  cubic saturation, on the bright side a shelf toward a one-pole high pass at 1 kHz. At the centre only the DC blocker.
- **HALO** (`halo.s`): the four lines of 71, 113, 151 and 211 samples as lattice all-pass filters, sum and difference of
  the sides through two of them, then the sides through the other two.
- **Loop** (`loop.s`): output = wet times MIX; feedback = limiter(wet times gain); the tape takes input plus feedback.

The wet output is taken behind COLR and HALO, so the first echo has passed them once and every later one once more.

## Controls

The engine reads version 3 of the control words from the same mailbox as before (`_phrase_control_info`; table in
`src/prism.h`): ZONE 1..8, RATE 0..1, the flags SYNC, FLIP and HOLD, the SYNC time in frames, RPTS 0..1, COLR -1..1,
HALO 0..1, SKEW -1..1, MIX 0..1. A mailbox of another version, a control that is not finite and a bank that does not fit
are faults: the block plays dry. The status mailbox carries the heartbeat, the two sides' times in frames, the hold state
and the frames of written tape.

| Zone | Free range | RATE 0, 0.5, 1 as measured |
| --- | --- | --- |
| 1 | 1 to 10 ms | 1.0, 3.2, 10.0 ms |
| 2 | 5 to 40 ms | 5.0, 14.1, 40.0 ms |
| 3 | 20 to 120 ms | 20.0, 49.0, 120.0 ms |
| 4 | 60 to 350 ms | 60.0, 144.9, 350.0 ms |
| 5 | 150 ms to 1 s | 150.0, 387.3, 1000.0 ms |
| 6 | 0.5 to 2.7 s | 500.0, 1161.9, 2700.0 ms |
| 7 | 1.3 to 5.4 s | 1300.0, 2649.5, 5400.0 ms |
| 8 | 2.7 to 10.7 s | 2700.0, 5374.9, 10700.0 ms |

## Measurements [D]

The click measure is that of finding 354: the largest second difference of the wet output from an event to the next, as a
multiple of the largest in a steady stretch of the same take. Input a 220 Hz sine of 0.08 on both sides, RPTS 0.5. Two
heads that read the same tone at two delays add with their phases, so a crossfade can raise the level, and the measure, to
1.41 without any discontinuity.

| Claim | Measured |
| --- | --- |
| Time is exact | an impulse of 0.5 comes back after exactly 48, 2352, 48000 and (SYNC) 9000 frames on both sides, at 0.500 |
| The longest time plays from one bank | zone 8 at RATE 1: 513,600 frames (10.700 s), echo / burst 1.0000 |
| Zone change | 1.40, 1.11, 0.67, 0.98 x steady (zone 4, 5, 3, 1, 4) |
| SYNC step, SYNC on and off | 1.08, 0.56, 0.40, 0.33 x steady |
| FLIP on and off, every segment's seam | 1.09, 1.01, 1.23, 0.48 x steady |
| HOLD on, off, and ZONE, RATE, FLIP while held | 1.49, 1.01, 1.50, 1.09, 1.50, 1.65, 1.33, 1.50, 1.72 x steady |
| SKEW moves, free RATE moves (one echo) | 2.48, 2.50 x steady; a clean tone bent up by 1.5 has 2.25 |
| COLR, HALO, RPTS, MIX in steps | 1.00, 1.01, 0.97, 0.87 x steady |
| COLR -1, 1, 0, HALO 1, 0, MIX 0, 1, RPTS 0, 0.5, each at once | 0.92, 1.02, 0.63, 0.76, 0.95, 0.95, 0.96, 1.00, 0.52 x steady |
| First echo after the start | the head comes in at sample 2497 (TIME 2400), 0.56 x steady |
| A crossfade is equal power | noise through 24 crossfades: 1.001 of the steady level at their middle |
| No dip per reverse segment or loop turn | lowest 5 ms window of noise against the median: forward 0.919, reverse 0.897, held 0.940 |
| A free RATE move bends the pitch | 1 kHz tone, 4888 to 8297 frames: 500.0 Hz going longer, 1500.0 Hz going shorter, 1000.00 Hz at rest; the delay moves at most 16 frames a block and rests on a whole frame |
| A jump does not | 1 kHz tone through two zone changes and two SYNC steps: 991.0, 983.1, 1016.6, 1000.0 Hz during the crossfade (the two heads' phases), 1000.00 Hz after it; nothing at 500 or 1500 Hz |
| RPTS, level of a repeat against the one before | 0: 1.8e-4 (one echo); 0.25: 0.3333; 0.5: 0.6667; 0.75: 1.0000 (16th against 1st 0.9997); 1: 1.1000 until the limiter |
| RPTS 1 for 30 s (20 s of a 0.5 tone, 10 s without) | peak 1.000 with the input, 0.498 after it; rms 0.4736 at 5 s and at 10 s after |
| RPTS 0.75 with a burst of 0.5 | peaks of repeats 1, 2, 4, 16: 0.501, 0.464, 0.426, 0.361 (the limiter pulls it toward its knee, 0.25) |
| COLR 0 | 0.000 dB a repeat at 200 Hz and at 4 kHz |
| COLR dark, dB a repeat at 200 Hz / 4 kHz | -0.25: -0.03 / -0.60; -0.5: -0.11 / -2.47; -0.75: -0.21 / -6.29; -1: -0.28 / -14.30 |
| COLR bright, dB a repeat at 200 Hz / 4 kHz | 0.25: -0.54 / -0.03; 0.5: -2.37 / -0.11; 0.75: -6.53 / -0.20; 1: -14.14 / -0.25 |
| Dark saturates and drifts, the centre does not | 1 kHz tone of 0.8, one echo: third harmonic -43.2 dB at full dark, -151 dB at the centre; pitch +-0.150 % at full dark, none at the centre |
| HALO 0 is the engine without it | output and tape bit for bit equal to a build whose `_prism_halo` is a return (`tests/halo-off.s`) |
| HALO smears every repeat more | share of a repeat's energy outside the burst's own 2 ms, repeats 1 to 6: HALO 0.25: 0.163, 0.296, 0.406, 0.495, 0.590, 0.645; 0.5: 0.509, 0.752, 0.871, 0.932, 0.961, 0.979; 1: 0.936, 0.994, 0.999, 1.000, 1.000, 1.000 |
| HALO keeps the loop's gain | energy of the 8th repeat against the 1st at RPTS 0.75: 1.000, 0.993, 0.985, 1.000 at HALO 0, 0.25, 0.5, 1 |
| SKEW, left / right frames at 2400 | 0: 2400 / 2400; 0.5: 1920 / 3000; 1: 1600 / 3600; -1: 3600 / 1600 |
| FLIP plays backwards | a burst that swells: its echo's energy sits at 0.749 of its length forwards, at 0.251 with FLIP; level 1.000 of forwards |
| Taking HOLD is not heard | the output of the first 3839 samples (TIME 4800) is bit for bit that of the engine without HOLD |
| Held: no loss | turn 55 against turn 1: rms 1.000001, 220 Hz 1.000000; turn 20 to 55: no sample differs |
| Held: the tape is not written | 0 stores to the bank in 60 s, the bank's words unchanged; a 1 kHz input after HOLD: below 1e-4 in the loop |
| Held with COLR -0.5 and HALO 0.5 | turn 38 against turn 8: rms 1.000000, 3 kHz 1.000000 (against turn 1: 1.0015, HALO's lines settling) |
| Held: ZONE, RATE, FLIP pick a part | the output repeats after exactly 1200, 836 (zone 2), 836 reversed and 9600 frames (largest difference 2.5e-6); bank unchanged; back at the first time the first loop returns to 1.8e-7 |
| Release | the write steps back 480 frames and crossfades for 480; an impulse after it comes back after TIME |
| Tape that was never written is never read | three random walks through all controls on a bank of NaN words, from an empty tape: no NaN in the output, no store outside the module's memory and its bank |

Cost: the heaviest hook call (core/fx and the effect) is **17,379 instructions**, against 42,928 for the engine before.
It is a constructed case: HOLD taken while both sides' heads drift between frames and released before the loop turns, so
both sides crossfade from a head between frames to a head that glides, with HALO, the dark filter, the limiter and the
write's crossfade. `dsp_cost_fx = 17379` in `module.toml` is that number; the test fails on a heavier block. A steady block
with everything at rest and HALO off costs about 4,800; HALO adds about 3,400. Instructions of the rig, not a cycle count
of the SHARC.

## Decisions the plan did not settle

- **Crossfade length: 960 samples (20 ms).** Measured by a SYNC step from 2400 to 3601 frames and back with one echo, the
  click measure and the share of the energy further than 150 Hz from the tone in a window around the crossfade:

  | Length | 55 Hz | 220 Hz | 1 kHz |
  | --- | --- | --- | --- |
  | 32 (0.7 ms) | 148 x, -21 dB | 11.0 x, -19 dB | 1.9 x, -31 dB |
  | 96 (2 ms) | 18.3 x, -25 dB | 2.4 x, -20 dB | 1.5 x, -29 dB |
  | 240 (5 ms) | 5.9 x, -29 dB | 1.3 x, -29 dB | 1.4 x, -30 dB |
  | 480 (10 ms) | 3.7 x, -49 dB | 1.1 x, -50 dB | 1.4 x, -43 dB |
  | 720 (15 ms) | 2.6 x, -55 dB | 1.1 x, -57 dB | 1.4 x, -52 dB |
  | 960 (20 ms) | 2.3 x, -60 dB | 1.0 x, -60 dB | 1.4 x, -59 dB |
  | 1440 (30 ms) | 1.8 x, -68 dB | 1.0 x, -66 dB | 1.4 x, -66 dB |

  At 20 ms what the crossfade adds beyond 150 Hz of the tone is 59 dB or more below it at all three frequencies; each
  5 ms less costs 5 to 7 dB. Longer was not taken because for the length of the crossfade both times sound together. The
  curve is `sin(pi/2 * s)` with `s` a smoothstep: equal power, and no slope at either end (a plain sine quarter starts with
  a slope; computed, not measured, that is a kink of about 4 times a clean 220 Hz tone's measure at 10 ms). The seam of a reverse segment or of a held loop uses the same
  curve over at most half the turn. `PRISM_XFADE` in `prism.h`; a test may change the state word `PS_XFADE`.
- **The limiter is exact below 0.25.** The limiter before (`0.75 (t - t^3/3)`) bends from zero, so a gain of 1 lost 3.7 %
  a repeat at a level of 0.25 and "unity" was not endless. Now it is the identity up to the knee 0.25 and the same cubic
  shape from there to the same ceiling 0.5. Below the knee RPTS 0.75 holds a repeat for ever (16th against 1st 0.9997,
  the rest is the DC blocker at 440 Hz); above it a repeat comes down toward the knee.
- **RPTS curve**: linear, 0 to 0.75 gives a gain of 0 to 1, 0.75 to 1 a gain of 1 to 1.1.
- **HALO is the four lines as all-pass filters, not the SPACE network as it was.** The lines, their lengths and their state
  are the same. The network before fed the lines back through a Hadamard matrix times a decay and mixed the result with
  the input; computed from its equations, its largest gain over frequency is +21.6 dB with the default decay (0.979) and
  DIFF at 1, +8.4 dB with decay 0.9 (not measured on the engine). Inside a loop at unity that grows at the resonances
  until the limiter holds it. As all-pass filters with an orthogonal mix between them the network keeps the energy at
  every frequency, so HALO never changes the loop's gain: measured, the 8th repeat has 0.985 to 1.000 of the energy of
  the 1st at unity. HALO sets the all-pass angle: 0 is the signal itself, 1 is a coefficient of 0.5. At low settings the
  direct sound keeps most of its level and a long, faint tail follows; at high settings most of the energy is in a short,
  dense tail.
- **COLR**: both sides are shelves whose amount is the square of COLR, so the control is fine near the centre (COLR 0.5
  takes 2.4 dB a repeat off its side, COLR 1 is the full filter) and the amount has no slope where the filter changes
  sides. With a linear amount a jump across the centre measured 2.1 to 2.3 x steady; with the square 1.02. The dark
  low pass is at 800 Hz, the bright high pass at 1 kHz, both one pole.
- **Drift**: only on the dark side, by the dark amount: the delay moves by at most 30 frames and at most 2 % of the time
  at 0.6 Hz, the left side against the right. Measured +-0.15 % of pitch at full dark with a long time. None while the
  tape stands or plays backwards.
- **Glide**: `PRISM_GLIDE_MAX` 0.5 frames a sample as before (an octave down, a fifth up), now with the velocity eased
  over 256 samples, so the pitch bends in instead of jumping (finding 354 had that open). A SKEW move glides too. At rest
  the delay sits on a whole frame, so a repeat is a copy and loses no highs to interpolation.
- **Time law**: exponential inside a zone, so equal turns of RATE are equal ratios of time.
- **HOLD**: when HOLD is taken the tape takes 480 more frames and stops. The loop ends where HOLD was taken; those 480
  frames are what followed it. At the release the write steps back to the loop's end and crossfades linearly from them
  into the new signal, so the tape has no splice (the engine before wrote a decaying step, finding 354). While held the
  loop starts where the head read when HOLD was taken; a shorter time keeps the start and moves the end, a longer one
  moves the start back. The seam crossfades into the tape before the loop's start, so a turn is exactly TIME frames and
  the tape is only read. COLR and HALO still work on the output of a held loop; they do not accumulate because nothing is
  written.
- **FLIP on the running tape** is the reverse delay of the engine before with two heads: each stretch of TIME samples
  plays backwards. A repeat of a repeat is therefore forwards again. A free RATE move does not bend the pitch in reverse:
  the next segment takes the new time.
- **Smoothing**: RPTS, COLR, HALO and MIX follow their controls through two stages of 1/16 a block (half way after about
  18 ms), so neither the value nor its slope jumps.
- **DC blocker**: 2 Hz, with unity gain above it. A first try at 5 Hz without the gain correction raised a 440 Hz repeat
  by 0.02 % a pass at unity.
- **Output**: the wet signal only, times MIX, as the effect contract asks; the adapter's own mix stays at 1.
- **Tests**: `tests/engine.test.ts` and its Python probe are replaced by `tests/engine.rig.test.ts` on the SHARC-only rig
  (`digikit/prism:test-rig`), because 30 s of audio and a 10.7 s time need the native core: the suite runs 45 cases in
  about 130 s. `FxRig` of the kit got `poke()` for a word of a mailbox an effect imports.

## What finding 354 had open

- **[C]** "A slope change of the size of the signal's own slope remains" at a change of mode (22 x a clean signal): FLIP
  and HOLD now crossfade; 1.09 to 1.72 x steady, which is the level of two phases adding.
- **[C]** "The dip is still there, about 10 ms long" per reverse segment and loop turn: two heads crossfade at the seam;
  with noise the lowest 5 ms window is 0.897 (reverse) and 0.940 (held) of the median, the forward head has 0.919.
- **[C]** "The change of pitch at the start of a TIME glide is immediate": eased over 256 samples.
- **[C]** `dsp_cost_fx` is declared.

## Not done, not verified [O]

- **[O]** How it sounds. Nobody has listened. "Sounds like a Mimeophon" is not something these tests show: they show
  that each control does what the plan says, without clicks and within bounds. The zone ranges, the two filter corners,
  the amount of saturation and drift, the HALO range, the glide speed and the 20 ms are first values for the owner's ears.
  Renders for that: `node modules/fx/prism/tests/render-audio.ts OUTDIR` (the module's README).
- **[O]** At the top of RPTS a sustained tone ends as a nearly square wave at about +-0.5 (rms 0.474 for a peak of 0.498):
  a limiter without memory in a loop with a gain above 1 flattens whatever it holds. The engine before did the same
  behind its 3 kHz low pass. A limiter that follows the level instead would keep the wave's shape; not built.
- **[O]** HALO's lines are 1.5 to 4.4 ms long, as SPACE's were. One pass smears over some tens of milliseconds; a wash
  comes from the passes adding up. Longer lines would need more state than the 5,632 bytes of today.
- **[O]** A glide reads between frames with a linear interpolation: during a full-speed bend up every other sample is a
  mean of two frames. Computed from that error, a 1 kHz tone has an image about 53 dB below it; measured is only the click
  measure, 2.48 against the 2.25 of a clean bend. At rest and in every jump the read is on
  whole frames.
- **[O]** A held loop shorter than 64 frames that is released is crossfaded over less than 32 samples.
- **[O]** A SYNC time that changes with every block (a tempo that is being turned) is a row of crossfades, one after the
  other.
- **[O]** The storage descriptor still describes two banks; the engine uses the first. The second bank is free for
  whatever step 3 decides.
- Nothing ran on the emulated device or on hardware, and there is no cycle count of the SHARC.

## What the engine change breaks until step 2

The ColdFire adapters `core/phrase-control`, `core/phrase-ui` and `core/phrase-route` are untouched: they publish and
show version 2 of the control words. The engine refuses that mailbox, so on a device image PRISM faults every block and
the adapter plays dry. Not run, and expected to fail on this branch:

- `digikit/prism:test-device` with its files `ui`, `capture` and `coexistence` (and the `test-device-all` forms): they
  drive the five pages and read the status words of the engine before (state, held frames, grains).
- `digikit/prism:test-conformance` (the load cell on the `phrase` patch set) was not run either; the module still packs
  (`digikit/prism:pack`, 8,970 bytes).

The adapters' own host tests (`core/phrase-control`, `core/phrase-route`, `core/phrase-ui`) do not load the engine and are
not affected. `digikit/prism:test-host` no longer exists: the module's tests are `digikit/prism:test-rig`.
