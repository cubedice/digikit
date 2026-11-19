# 364. PRISM's SYNC after the Mimeophon: ZONE an octave, RATE a ratio

The owner's decision of 2026-11-19: PRISM's SYNC mode follows the Make Noise Mimeophon's Tempo input. Its manual's
"Understanding" page: the repeats are synced "with divisions and multiples of this tempo chosen by the Rate and Zone
parameters"; the 1/1 lands on the zone that best fits the tempo, and the other zones are divided or multiplied by powers
of 2; Rate has 13 positions from x2 to /2. Digitakt II OS 1.17, on the host, on the SHARC-only rig and on the emulator with
the SHARC core. Nothing ran on hardware and nobody has listened. Marks as in `docs/FINDINGS.md`; every result here is
**[D]** unless it is marked **[O]**.

## What SYNC computes

`core/phrase-control` computes the engine's `SYNC_TIME` once a block in the block interrupt, in 32-bit integers
(`sync_time_of` in `src/control.c`); the numbers are in `sdk/dsp/contract/dk_phrase.inc`.

- **The beat** is a quarter note at the pattern's tempo: `345,600,000 / tempo` frames, the tempo in 1/120 BPM from word 5 of
  the position suffix (wire v4 of `core/pattern-position`). The quotient and the remainder are both kept, so the frames are
  right to one frame at every tempo. Without a valid tempo the last one stays (120 BPM before any).
- **Z0**, the zone whose 1/1 the beat is: the zone whose centre, the geometric mean `sqrt(low * high)` of its free range,
  lies nearest the beat in log terms. That is the zone between whose edges the beat lies, the edges being the geometric
  means of neighbouring centres: `PH_SYNC_EDGE_1..7` = 321, 1263, 4044, 11372, 32199, 84219, 181139 frames, from the
  engine's zone table (the rig test reads `_prism_zones` and checks each edge to the frame). In tempo: SLAP above 253.3 BPM,
  ECHO from 89.4 to 253.3 BPM, LONG from 34.2 to 89.4 BPM, PHRS below 34.2 BPM (the position suffix gives 30 to 300 BPM).
  Examples: 96 BPM, a beat of 625 ms: ECHO; SLAP's 1/1 is then an eighth note and LONG's a half note. 60 BPM: LONG. 120 and
  180 BPM: ECHO.
- **The zone** doubles the 1/1 for each zone above Z0 and halves it for each below.
- **RATIO** (the raw value `PH_P_RATIO`, 0 to 12) multiplies it by 1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 1, 5/4, 4/3, 3/2, 5/3, 7/4
  or 2: the Mimeophon's /2, /1 3/4, /1 2/3, /1 1/2, /1 1/3, /1 1/4, 1/1, x1 1/4, x1 1/3, x1 1/2, x1 2/3, x1 3/4, x2.
- `SYNC_TIME = beat * 2 ^ (zone - Z0) * ratio` frames, to the nearest, at most `PH_SYNC_MAX` = 513,600 frames (10.7 s, the
  top zone's high end, which is also about the engine's longest time from one bank of the device: half its 1,032,192
  frames). The engine then bounds it by its own tape as before.

At 96 BPM, ECHO, the 13 ratios from /2 to x2 are 15000, 17143, 18000, 20000, 22500, 24000, 30000, 37500, 40000, 45000,
50000, 52500 and 60000 frames; SLAP has half of each (7500 to 30000, 8571 for 4/7).

**At slow tempos the top zone reaches the bound.** While the beat is LONG's 1/1, LOOP's 1/1 is four beats: its x2 is cut to
10.7 s below 44.9 BPM, x1 3/4 below 39.3 BPM and x1 2/3 below 37.4 BPM, so at 35 BPM the three longest ratios of LOOP are
the same 10.7 s (the host test checks it). Just above 89.4 BPM, where the beat is ECHO's 1/1, LOOP's x2 is 16 beats and
is cut by up to 40 ms. No other zone or tempo reaches the bound; the shortest time, KARP's /2 at 253 BPM, is 7.4 ms.

## The direction of RATE, and the detent

PRISM keeps its own direction: in free mode a higher RATE is a longer time, so with SYNC a turn to the right goes toward x2
(the longest) and to the left toward /2 (the shortest), with 1/1 in the middle (index 6, `PH_SYNC_UNITY`). This is the
reverse of the Mimeophon's Rate knob (the owner's choice; the README says so). A ratio takes four ticks, as the note
lengths did; 13 choices need no other count (the whole range is 52 ticks, 64 before).

## The engine

Unchanged. With SYNC it reads only `SYNC_TIME`: its program key is (zone 0, SYNC 1, the time), so a zone change in SYNC
reaches it as a new `SYNC_TIME` in the same mailbox. A new time is a new key and, when the base time changes, one new
program: the heads crossfade (`PRISM_XFADE`, 960 samples) and the pitch does not bend. The rig test counts the program
changes (`PS_KEYGEN`) and measures a 1 kHz tone through each change. A tempo that moves changes the time too and is a
crossfade each time, as before. **[O]** Under an external MIDI clock whose tempo wanders, each new tempo is a crossfade.

## The page and the settings

`core/phrase-ui`: with SYNC, RATE's cell shows the ratio (`/2`, `/1.75`, `/1.67`, `/1.5`, `/1.33`, `/1.25`, `1/1`,
`X1.25`, `X1.33`, `X1.5`, `X1.67`, `X1.75`, `X2`; five characters at most) over `SYNC`. The header keeps the time the
engine plays. ZONE's name and its eight marks stay as they are: in SYNC the zone is the octave.

The ratio is the saved value that was the note length (`PH_P_NOTE` before, renamed `PH_P_RATIO`; `PH_TIME_CHOICES` is now
`PH_SYNC_RATIOS`, 13). The record's format is unchanged (3): a stored value above 12 is read as 12, the other values and the
tracks are kept; without a stored value the ratio is the 1/1. The owner's device (format 3, its note value whatever it is)
keeps everything; a note value of 13 to 15 becomes x2.

## Proofs

| Layer | Check | Result |
| --- | --- | --- |
| Host, C | `core/phrase-control:test-host`: Z0 at 60 BPM LONG, 96, 120 and 180 BPM ECHO, 300 BPM SLAP, 30 BPM PHRS; the provider's zone equals the definition (nearest centre in log terms, computed in doubles from the zones' ranges) at every tempo from 30 to 300 BPM in steps of 1/120 BPM but within a frame of an edge; at 96 BPM every ratio in ECHO and SLAP against the formula (30000 and 15000 at the 1/1, 17143 at 4/7); LONG at 96 and 60 BPM; every zone, ratio and tempo (in steps of 37) within a frame of the formula; at 35 BPM LOOP's 3/2 under the bound and 5/3, 7/4, 2 at 513600; no tempo and a tempo out of range keep the last one, and a ratio change then is taken at it; a stored note of 15 is 12 with the other values and the tracks kept, 8 stays 8, nothing stored is the 1/1. `core/phrase-ui:test-host`: the 13 ratio texts, four ticks a ratio up to 12 and down to 0, RATE kept, the RATE cell `X1.5` and `1/1` over `SYNC`, the header's time | pass |
| Rig | `digikit/prism:test-rig` (55 cases): the edges `PH_SYNC_EDGE_1..7` and `PH_SYNC_MAX` against `_prism_zones`, to the frame; with SYNC, SLAP's 1/1 to ECHO's 1/1 (a zone change with its new time), ECHO's 1/1 to 3/4, and back to SLAP: one program change each, a 1 kHz tone at 1000.0, 1000.0 and 973.0 Hz during the crossfade and 1000.00 Hz after it, nothing at 500 or 1500 Hz | pass |
| Device | PRISM `ui`: SYNC on, the 1/1 by default, `SYNC_TIME` the formula's at the tempo the position suffix gives and the engine's time equal to it; B four ticks right: `X1.25` and its frames; A four ticks up: LONG, twice the frames (to a frame), the engine plays them; back, and SYNC off | pass |

## Open

- **[O]** Nobody has listened; whether the ratios are musically the right ones for PRISM is for the owner's ears.
- **[O]** Hardware: not run.
