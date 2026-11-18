# 354. PRISM as a tape delay and granular effect that works without a phrase

PRISM (finding 352) played grains only from a recorded phrase. It is now an always-on effect on the selected sources: a tape
delay and grains on the rolling bank, with the recorded phrase as an option. Digitakt II 1.17: emulator results, and one
install on a Digitakt II without a listening test (below). Marks as in `docs/FINDINGS.md`.

## What changed

- **Engine** (`modules/fx/prism/src/dsp`). New `tape.s`: one stereo head on the rolling bank (the 21.5 s the engine already
  kept for PAST takes). It runs before the capture write of each block, reads behind the block about to be written and
  leaves the block's delay output and its feedback; `capture.s` adds the feedback to the input it writes. FORWARD slews the
  delay by at most half a frame per sample; REVERSE plays segments of TIME backwards, a block late so that no read falls
  into the block being written; HOLD skips the write and loops the last TIME frames. `grain.s`: without a held phrase a
  launch takes a window of the newest tape (two seconds at most) and keeps it for the grain's life (`PG_FRAMES`); with a
  phrase nothing changed. `_prism_mix` (in `tape.s`) sums grains and head by BAL and applies DRIV before SPACE.
- **Contract** (`sdk/dsp/contract/dk_phrase.inc`), version 2: the four reserved control words are TIME (frames), FEEDBACK,
  a packed word (FLIP, HOLD, SKEW, WOW, BALANCE) and DRIVE; seven parameters behind the gains (TIME, FDBK, MODE, SKEW, WOW,
  DRIV, BAL); the storage descriptor keeps its own format number 1.
- **Control provider** (`core/phrase-control`): TIME is one of sixteen note lengths, turned into frames from the tempo in
  integers inside the block interrupt; MIX no longer needs a held phrase; CLEAR keeps MIX unless a routing fault is latched;
  the saved settings have format 2 (earlier ones are not read: the controls start from their defaults once).
- **Router** (`core/phrase-route`): the effect's output passes whenever the owner is healthy. Its sources are those of the
  held phrase, or, without one, the sources the tape records. MIX: the wet rises over the first half while the sources stay
  whole, the sources fall over the second half. The fail-open rules are unchanged.
- **Panel** (`core/phrase-ui`): a fifth page DELAY, which a fresh owner shows first; LOOP, GRAIN, MOVE and SPACE keep their
  numbers and contents.

## Evidence [D]

- `digikit/prism:test-host`: 47 cases on the assembled engine through the production FX guard; 12 are new. The head plays
  the tape exactly TIME frames late on both sides; without enough tape it is silent; feedback repeats decay by 0.3 to 0.55
  per generation at 50 % with a 3 kHz tone and stop without feedback; feedback of 110 % with grains stays below a peak of
  4; HOLD freezes write position and history and repeats every TIME; REVERSE runs a ramp down by one frame per sample; a
  change of TIME from 64 to 96 frames arrives within a frame and moves at most 0.5 frames per sample; SKEW makes the right
  side 95 to 96 frames at TIME 64; a history reset runs the head to zero within the block and it resumes; DRIVE stays at or
  below 4/3; tape grains read windows of 256 to 448 frames inside the 512-frame test bank from the rolling bank, fade out
  on a reset and stay on written tape at the extremes of size, pitch, spray and reverse. Every block of every case is
  below the 48 000 instruction budget and writes only its state and its leased banks.
- `core/phrase-control:test-host` 13, `core/phrase-route:test-host` 39 (the curve cases are the new MIX law; "no phrase"
  is now a case of the effect passing), `core/phrase-ui:test-host` (five pages).
- Device tests on the `phrase` and coexistence images: `ui` (five pages, DELAY first), `capture`, `coexistence` (CANOPY,
  Overloop, PRISM and remote), and the conformance load. In `capture`, without a held phrase and with MIX at the top, a
  note of the selected track is heard only as its echo: TIME is 6000 frames (a sixteenth at 120 BPM, read from the control
  word), the note starts at sample 208 of the capture without the effect and at sample 12 278 with it, 6035 frames later
  in a stereo capture; the echo's RMS is 0.0385 against 0.0441 for the note, its peak 0.089 against 0.064 (the grains on
  the tape sound too). The WAV files are kept by the test (`tape-off.wav`, `tape-echo.wav`).

## Hardware [D]

Installed on one Digitakt II on 2026-11-17: image `f48a6656aade4fbf`, byte-identical to the image the coexistence test
boots. Only PRISM's package changed (11 370 bytes); it was uploaded before the flash, so the device needed no restart
after it. CANOPY, Overloop, PRISM and remote load with no faults and the settings record is unchanged. PRISM's entry
opens on DELAY with its defaults (TIME 1/8., MODE FWD, state READY). No audio was captured and nobody has listened.

## Feedback level [C]

The first build limited the feedback at 4/3 of full scale with a low pass near 5 kHz. On the device the feedback
sounded clipped. In the engine harness a 0.3 tone with TIME 96 and 100 % feedback built the tape up to 1.52 and, at
110 %, left a tail that sat at 0.96 with steps of 0.48 per sample: a flat-topped wave. The limit is now half of full
scale (`0.75 * (t - t^3/3)`, `t` = 4/3 of the signal), the low pass is near 3 kHz and the gain is ramped over each
block. The same input now peaks at 0.80, and the tail at 110 % sits at 0.31 [D]. Not yet heard on the device.

## Clicks [C]

On the device (image `5f664d0d90c67436`) PRISM was still "glitchy / clicky" after the feedback change. The engine was run
on the concrete core over 450 to 1000 blocks per case with the control words `core/phrase-control` publishes for a set of
panel values, and the whole output was kept. The measure is the largest second difference of the left output, for a
220 Hz sine of 0.5 on both sides; in brackets the same as a multiple of what a clean signal of that level has. TIME was
2400 frames in every case. [D], emulator only.

| Case | Before | After |
| --- | --- | --- |
| A timeline change (START), one with STOP, a selection change; head only, FDBK 70 | 0.399 (527) | 0.0009 (1.1) |
| The same with the grains on | 0.402 (507) | 0.0012 (1.0) |
| The clock invalid for 40 blocks | 0.383 (506) | 0.0009 (1.1) |
| No source for 150 blocks, then the source again | 0.273 (368) | 0.0006 (0.9) |
| MODE: FWD, REV, HOLD, FWD | 0.671 (1046) | 0.014 (22) |
| HOLD, each turn of the loop | 0.028 (47) | 0.0024 (4) |
| REV, each segment | 0.042 (75) | 0.0020 (3.5) |
| BAL turned in steps of 4 | 0.049 (76) | 0.0021 (3.3) |
| DRIV turned from 0 in steps of 4, at the block starts | 0.119 (107) | none there |
| The first echo after the tape became long enough | 0.0075 (13) | 0.0015 (2.5) |
| The settings read from the device, grains only (SHAP FALL) | 0.0053 (19) | 0.0008 (3.0) |
| The settings read from the device, SPACE off | 0.0046 (5.8) | 0.0011 (1.4) |
| The controls of a fresh device | 0.0033 (3.2) | 0.0010 (1.0) |
| A change of TIME (the start of the glide) | 0.0055 (8.6) | unchanged |

What was wrong, and what it is now:

- **The tape started again with every timeline.** `_pc_reset_history` cleared the write position and the count of valid
  blocks on a new timeline epoch or selection. `core/phrase-control` starts a new epoch at START, at STOP, at a pattern
  change and at a scale change while stopped. The head then had no tape for TIME: its output ran to zero in 32 samples,
  the repeats were gone, and the grains on the tape faded out in 32 samples. Now only the history of a take starts again
  (`PS_VALID_BLOCKS`); the tape has its own count (`PT_VALID`) and keeps its write position. An invalid clock and an
  empty source mask no longer stop the write either: they clear the take history, and the tape takes what the router
  gives it (silence without a source) and the feedback.
- **The feedback filters kept their state while the head was silent**, so the first block the head could read again put
  a step on the tape, heard TIME later (the 0.4 above). The feedback is now always the filtered output, silent or not.
- **A change of MODE, and a head losing its tape, jumped.** The difference to the last sample is now carried on and
  run out over 256 samples (`PT_HEAL_*`). A slope change of the size of the signal's own slope remains (the 0.014).
- **The head came back in 32 samples.** Now 256 (`PT_FADE`).
- **The edge of a REV segment and of the HOLD loop was 16 frames**, a dip to silence in 0.7 ms. It is a quarter of the
  segment in powers of two, 16 to 256 frames. The dip is still there, about 10 ms long at 5 ms a side: a crossfade of two
  readers would remove it and is not built.
- **A tape that stood still in HOLD went on with a splice**, heard TIME after the release (0.25 in the MODE case). The
  write now starts from the tape's last frame and the difference falls by 1/256 a sample (`PS_WRITE_HEAL_*`).
- **BAL and DRIV stepped once a block.** Both gains and the drive move over the block's 32 samples. Where the drive goes
  on or off the driven signal is blended in over that block, because the curve bends even at no drive (`x - x^3/12`).
- **The steep side of a RISE or FALL grain was 32 samples.** With the device's settings (SHAP FALL, SIZE 243 ms, RATE
  1/24) that is an attack of 0.7 ms about thirty times a second. It is an eighth of the grain now, 32 to 256 samples.
  SOFT and TRI are unchanged.

What changes in the sound beyond the clicks: repeats and tape grains continue through START, STOP and a pattern change;
RISE and FALL grains have a softer steep side (up to 5 ms); REV and HOLD fade in and out over up to 5 ms a side instead
of 0.3 ms; the head comes in over 5 ms after it had no tape; a turn of DRIV away from 0 fades the drive in over a block.
Steady DRIV, BAL, FWD and the SOFT and TRI grains are unchanged.

Evidence: `digikit/prism:test-host` has 55 tests; seven are new (mode change, lost tape, BALANCE, DRIVE, the reverse
edge, the HOLD splice, the FALL edge) and four were changed for the tape that goes on. Those eleven fail on the engine
of main 9f2fba5e8. The heaviest block seen is 42 928 instructions of the 48 000 the descriptor reserves (eight grains of
243 ms, the head in reverse, full DRIV and FDBK, SPACE on); the device's settings in FWD reach 41 616.

Not shown by the harness: what the device's sources do (the input was a sine and a tone burst), and the load on the
SHARC. HEALTH read DSP 71 %, at most 78 %, which is no overrun; a block that ran over would also sound like a glitch.
The grains are computed even when BAL turns them off (34 320 instructions with the device's settings at BAL head only,
six grains running).

## Where this differs from the proposal

- **[C]** The proposal said that while a phrase is held "the heads read the loop". The delay head stays on the tape (the
  live selected sources) and only the grains read the phrase: a delay on a fixed loop is a second playback of the loop,
  and the tape keeps running for the next take anyway.
- FLIP and HOLD are one control, MODE (FWD, REV, HOLD); HOLD loops forward.
- MOVE stays a page of its own (POS, SCAN, REVR, WDTH) instead of moving into GRAIN, so there are five pages.

## Open

- **[O]** The fader beside the pages is still labelled FADE and LIVE; it is MIX now.
- **[O]** How it sounds: no listening on hardware; levels, the feedback tone, the glide rate and the grain window are first
  values. The owner judges.
- **[C]** The tape no longer restarts on a change of the input selection or of the timeline (section "Clicks"). **[O]** It
  still does at a take (the rolling bank changes) and at CLEAR: the head runs out and returns after TIME.
- **[O]** The change of pitch at the start of a TIME glide is immediate (half a frame per sample at once).
- **[O]** Whether the clicks heard on the device are among those found: nobody has listened to the new build.
- **[O]** TIME follows the tempo only; there is no free time in milliseconds.
- **[O]** `dsp_cost_fx` is still not declared; no hardware cycle count.
- **[O]** The grains' gain on the tape uses the same overlap rule as on a phrase; with BAL in the middle head and grains sum.
