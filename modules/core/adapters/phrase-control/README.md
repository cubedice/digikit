# Phrase FX controls

DTII 1.17 provider for one dynamically loaded phrase effect. The fx service recognizes the `_phrase_control_info`
import, validates one effect and no voices, and reserves the capability before it publishes any DSP row; a second
phrase effect is refused. The effect is otherwise an ordinary effect of the framework: its route is the one its record
allows (PRISM: a send effect). Its level is the control `PH_C_MIX` here, so the framework keeps the row's own MIX at
unity and reads no kit value for it.

## The layout

`sdk/dsp/contract/dk_phrase.inc` is the one home of the layout, version 3: the provider and the page include it through
`<dk/phrase.h>`, the engine through `dk_phrase_imports.inc`. The control mailbox is 64 words; status is 32 words. All
offsets in that header are bytes.

| Raw value (`PH_P_*`) | Range | Control word (`PH_C_*`) |
| --- | --- | --- |
| ZONE | 0..7 | `ZONE`, 1..8 |
| RATE | 0..1016 | `RATE`, float 0..1 |
| SYNC, FLIP, HOLD | 0, 1 | bits of `FLAGS` |
| RATIO | 0..12, 6 the 1/1 | `SYNC_TIME`, with ZONE: frames of the ratio of the zone's 1/1 at the tempo |
| RPTS | 0..127 | `RPTS`, float: 0..96 is 0..0.75 (a repeat at its level), 96..127 is 0.75..1 |
| COLR, SKEW | 0..127, 64 the centre | float -1..1 |
| HALO, MIX | 0..127 | float 0..1 |
| INPT | 0 LIVE, 1 LOOP, also without a loop (the engine then hears silence); not saved | `INPUT` |
| XFAD | unused: the crossfader is Overloop's (`core/loop-control`) | `XFADE` and `ARM_SEQ` stay zero; a set of XFAD is refused (-2) |

The status words `PH_S_INPUT` (the input as the engine takes it), `PH_S_LOOP` (the loop feeds it) and `PH_S_ROUTE` (Overloop
takes the output) are step 4's ([finding 361](../../../../docs/findings/361-prism-loop-input-and-return.md)); `PH_S_ARMED`
stays 0.

SYNC follows the Mimeophon ([finding 364](../../../../docs/findings/364-prism-sync-zones.md)): the beat, `345,600,000 / tempo`
frames with the tempo in 1/120 BPM from word 5 of the position suffix (`core/pattern-position`), is the 1/1 of the zone Z0
whose centre (the geometric mean of its free range) is nearest it in log terms, which is the zone between whose edges
`PH_SYNC_EDGE_*` (`sdk/dsp/contract/dk_phrase.inc`) it lies. Each zone above doubles the 1/1 and each below halves it;
RATIO multiplies it by 1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 1, 5/4, 4/3, 3/2, 5/3, 7/4 or 2. `SYNC_TIME` is
`beat * 2 ^ (ZONE - Z0) * ratio` frames to the nearest, in 32-bit integers with the beat's remainder kept, at most
`PH_SYNC_MAX` (513,600 frames, 10.7 s, the top zone's high end). Without a valid tempo the last tempo stays (120 BPM
before any).

The values are global: they are kept in the runtime settings under `phrase/<module id>` (a header with the format
number 3 and the count, then the values). A record of another format or size is not read and the defaults stand: ZONE
ECHO, RATE in the middle, RPTS 48, COLR and SKEW at the centre, HALO 0, MIX 100, SYNC off at the 1/1. RATIO is the
value that was the note length before: a stored value above 12 is read as 12. HOLD is not kept. No data is written into Elektron kits, sounds or projects.

## Transport

CF writes the 64-word control packet into DTII's frame suffix at 0x8b4, once a block in the block interrupt
(`phrase_write_frame`, called by `core/dsp-link`). The position, instrument and note-lock suffixes retain their
positions. A per-frame sequence, the same sequence at the end and a checksum gate the DSP side's copy into
`_phrase_control_info` (`src/dsp/control.s`, called from the position hook); an invalid or repeated frame leaves the last
good mailbox, and only `PH_RX_GRACE` (750) refusals in a row clear the magic and the enable word, which an engine checks
before it uses the words.

The existing loop telemetry carrier (rows 17-20 and 29-31, words +0x44/+0x48) is multiplexed: one of four frames keeps
the legacy loop status; three carry a latched 32-word snapshot of `_phrase_status` tagged with a page and a serial. CF
assembles it in the audio interrupt, so UI-task scheduling cannot lose a page. The UI copies the completed snapshot with
interrupts masked briefly (`dkrt_phrase_snapshot`). No allocation, drawing or settings write occurs in the interrupt.

## Tests

- `tests/control.test.ts` compiles `src/control.c` with stand-ins for the settings: values to control words, the ranges,
  SYNC's zone of the beat at every tempo and its frames for every zone and ratio against the formula, the bound at the top,
  the checksum, persistence (HOLD not kept, another format ignored, values clamped) and
  the status assembly.
- `tests/transport.test.ts` runs the assembled receiver and reply on the concrete SHARC rig
  (`tools/patchkit/pk_phrase_transport_probe.py`): a valid frame is copied, each kind of invalid frame is refused and the
  next valid one accepted, the reply pages are coherent, and the register context is preserved.

```bash
pnpm nx:pilot run core/phrase-control:test-host
```
