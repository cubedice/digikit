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
- **[O]** The tape restarts on a change of the input selection, on a timeline change (for example START) and at a take: the
  echo stops and returns after TIME. Keeping the tape across a START needs the capture code's history rules changed.
- **[O]** TIME follows the tempo only; there is no free time in milliseconds.
- **[O]** `dsp_cost_fx` is still not declared; no hardware cycle count.
- **[O]** The grains' gain on the tape uses the same overlap rule as on a phrase; with BAL in the middle head and grains sum.
