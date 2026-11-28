# 377. PRISM rests while nothing sounds

Work of 2026-11-27 on PRISM's engine (`modules/fx/digikit/prism/src/dsp`), Digitakt II OS 1.17. The engine ran on the
SHARC-only rig and on the emulated Digitakt II; nothing here ran on hardware. Marks as in `docs/FINDINGS.md`: every
result is **[D]** (measured once in this work) unless it is marked **[O]**. Costs are rig instructions of the effect's
hook call, core/fx included, not cycles of the SHARC.

## Why

The owner's Digitakt II, transport stopped and nothing sounding, as its DSP statistics showed it (**[D]**, the owner's
report; not measured again here): the costliest block took about 73 % of the 666,666 cycles a block of 32 samples has,
where the stock firmware idles at about 66 %. The effect row that is PRISM cost about 29,500 cycles a call, every block,
with or without a signal. Above about 91 to 95 % the stock engine loses note triggers, so cycles that are spent always
count.

The engine had one bypass: HALO at 0 skips the diffuser and the late network. A send of 0 or MIX at 0 still ran the
whole block, and has to: the tape goes on recording and the repeats play out.

## The rule

A block rests when the block before it found all of this (`_prism_settle` in `prism.s`):

1. **The whole written tape is silence.** The write to the tape (`loop.s`) keeps the largest magnitude of the 64 words
   it wrote, input plus feedback. `PS_TAPE_QUIET` counts the frames written in a row whose block stayed under
   `PRISM_IDLE_LEVEL` (2^-40, -241 dB), up to the tape's length; the tape is silence when the count is at least `PS_VALID`,
   the frames of written tape. An exact zero is not asked for: the state of a one-pole filter decays for ever, and a send
   bus of the stock engine may carry such a tail.
2. **After that, the wet signal was silence for `PRISM_IDLE_QUIET` frames in a row** (4096; `PS_QUIET`), so the colour
   filter and HALO's lines (251 frames at most) have played out. While HALO's late network runs, `PRISM_IDLE_LATE` frames
   (229,376, 4.8 s): its two rings turn in at most 1906 frames with a loop gain of 0.75, so 120 turns take a network that
   was full to the output's bound under the level, also when MIX is 0 and its tail cannot be seen at the output. The wet
   signal is looked at (64 words) only while 1 holds, so a block that plays does not pay for it.
3. **Nothing is left to finish:** no crossfade of the heads, no frames to take after HOLD (`PS_POST`), no write crossfade
   after a release (`PS_WFADE`), both sides' heads playing (a side that still fills changes by itself), the input's switch
   at one end.

What follows from 1: HOLD on a tape that sounds never rests (a standing tape is not written, the count stands, and the
loop keeps the wet signal up); repeats at or above unity never rest while anything circulates (every turn writes it
again). HOLD on a silent tape rests.

The brief's rule counted frames with an input of exactly zero. This one has no test of the input while the engine plays:
what is written to the tape is the input plus the feedback, and that is what has to be silence. The resting block does
test the input, against the same level.

## A resting block

After the checks of the control mailbox and the lease, and after `_pi_head` (whether the loop effect runs, the tap's flag,
INPT's target), `_prism_rest`:

- runs the block in full instead (`_rs_wake`) when one of the ten control words from ZONE to INPUT differs from the
  block that let the engine rest, when INPT's switch has to move (a new loop, the end of the half second after one), or
  when a sample of the input reaches the level: the send bus, or at INPT LOOP the loop's tap (silence while it carries no
  loop);
- else writes 32 frames of exact silence to the tape and moves the write position and `PS_VALID` on, as the engine would
  have (a standing tape is left alone); clears the output block; sets the share of the output the loop effect takes to
  where it would stand (1 while it runs, 0 when it does not); publishes the status.

It does not write the wet mailbox and does not move `LW_SEQ`, so Overloop takes nothing from it: its block is 762
instructions shorter while PRISM rests (2207 to 1445 on the coupled rig).

The status mailbox of a resting engine is, word for word and block for block, the one of the same engine kept awake: the
heartbeat moves, the state is READY or HELD, the two times, the hold state, the tape's frames (they go on counting), INPUT,
LOOP and ROUTE. The page reads the times, the error and the owner; the provider the heartbeat.

The first resting block empties what the engine no longer runs: the two colour filters, HALO's four lines (1024 words)
and the late network's rings (2048 words) and filters. The late network's count of valid samples is kept: a reset would
fade the next sound's excitation in over 256 samples. It also lands a head that still glides (a free RATE or SKEW move
made in silence) on its target plus the drift; the tape is silence, so nothing is heard, and the next note is not bent.

## Waking

The block that wakes runs in full from a state that is silence except the tape's dust under the level, with `PS_FIRST`
set after `_pi_head`: RPTS, COLR, HALO, MIX and the ping-pong blend take their values at once instead of moving there
(the owner agreed to this). The same block lets the engine rest again if its controls were all that changed, so a knob
turned in silence costs one block a change.

What stands still while the engine rests: the phase of the drift below the uncoloured point of COLR, and the place in a
reverse segment or a held loop. A forward head at rest does not move against the tape in any case.

## Numbers

| | Before | Now |
| --- | ---: | ---: |
| A resting block | 6127 (it ran in full) | 1436 |
| The first resting block: HALO off, HALO on | | 1461, 4547 |
| A block that plays a steady tone, HALO off | 6127 | 6304 |
| The same with HALO 0.5 | 11449 | 11628 |
| The same, into the loop effect: HALO off, HALO 0.5 | 6808, 12130 | 6985, 12309 |
| The heaviest block of the suite (`dsp_cost_fx`) | 22113 | 22138 |

- A block that plays costs 177 to 179 instructions more: the largest magnitude of the 64 written words (128), the two
  counts and `_prism_settle`. Its output, its wet mailbox and its tape are the same words as before:
  `tests/active-pins.rig.test.ts` pins five runs through every part of the block to hashes taken from the engine of
  commit ee79d7018, and no pin moved.
- A note and the 16,384-frame tape of the rig: the last output sample at or above the level at frame 85,153, resting from
  frame 103,488. On the device the tape is 1,032,192 frames, so the engine rests 21.5 s after the last sound it wrote, plus
  0.1 s, or plus 4.8 s with HALO up. An engine that has not heard anything since it started rests after 4096 frames.
- After a rest of several seconds an impulse and a note play as on the engine that never rested: the largest difference
  2.3e-26 of a peak of 0.26, with HALO 0.7 and its late network. With the drift (COLR -1, SKEW 0.3) the heads stood still
  where the other engine's went on: the echoes' rms 0.06149 against 0.06152, and no stretch less smooth.
- RATE, MIX and HALO turned while resting: one block runs, the next is the first resting block again, the status shows the
  new time, and the next note differs from the engine that never rested by 1.4e-16 of a peak of 0.105.

## What a player can notice

- Nothing while PRISM plays.
- A control turned while nothing sounds is there at once. Before, RPTS, COLR, HALO and MIX took about 0.1 s and a free
  time glided; neither could be heard in silence.
- After a silence the slow drift of the dark colours and the turn of a reverse segment go on from where they stood, not
  from where they would have been.

## Tests

- `tests/rest.rig.test.ts` (16 cases): resting after the rule's silence and not before; HALO's longer wait and the emptied
  lines and rings; an input under the level and one at it; no rest with HOLD on a sounding tape, with unity repeats, during
  a crossfade, the frames after HOLD and a release, or while a side fills; waking against the engine kept awake, with HALO
  and with the drift; controls, FLIP and HOLD while resting; the status word for word; the wet mailbox left alone, the loop
  effect starting and stopping, INPT at LOOP, a new loop; Overloop's block on the coupled rig.
- `tests/active-pins.rig.test.ts`: the pins, and the cost of a steady block for the record.
- `tests/send.device.test.ts` on the emulated Digitakt II: before the first note the engine rests (`PS_IDLE` is 2), its
  status beats and counts the tape; the notes after it are echoed as before.

## Open

- **[O]** Cycles on the device: the rig counts instructions. The owner's statistics row should fall from about 29,500
  cycles to a few thousand while PRISM rests; to be read on hardware.
- **[O]** The wait of one whole tape (21.5 s on the device) is the simple, safe bound. Resting sooner needs the deepest
  frame the heads can reach with the controls as they are, and a wake when a control reaches deeper.
