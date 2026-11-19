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
| NOTE | 0..15 | `SYNC_TIME`, frames of the note length at the tempo |
| RPTS | 0..127 | `RPTS`, float: 0..96 is 0..0.75 (a repeat at its level), 96..127 is 0.75..1 |
| COLR, SKEW | 0..127, 64 the centre | float -1..1 |
| HALO, MIX | 0..127 | float 0..1 |
| INPT | 0 LIVE, 1 LOOP; not saved | `INPUT` |
| XFAD | unused: the crossfader is Overloop's (`core/loop-control`) | `XFADE` and `ARM_SEQ` stay zero; a set of XFAD is refused (-2) |

The status words `PH_S_INPUT` (the input as the engine takes it), `PH_S_LOOP` (the loop feeds it) and `PH_S_ROUTE` (Overloop
takes the output) are step 4's ([finding 361](../../../../docs/findings/361-prism-loop-input-and-return.md)); `PH_S_ARMED`
stays 0.

The note lengths are 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 128, 144, 192, 288, 384 and 768 ticks of 96 a quarter
(1/32 to two bars, with triplets and dotted values). `SYNC_TIME` is `ticks * 3,600,000 / tempo`, the tempo in 1/120 BPM
from word 5 of the position suffix (`core/pattern-position`); without a valid tempo the last time stays.

The values are global: they are kept in the runtime settings under `phrase/<module id>` (a header with the format
number 3 and the count, then the values). A record of another format or size is not read and the defaults stand: ZONE
ECHO, RATE in the middle, RPTS 48, COLR and SKEW at the centre, HALO 0, MIX 100, SYNC off at a dotted eighth. HOLD is
not kept. No data is written into Elektron kits, sounds or projects.

## Transport

CF writes the 64-word control packet into DTII's frame suffix at 0x8b4, once a block in the block interrupt
(`phrase_write_frame`, called by `core/dsp-link`). The position, instrument and note-lock suffixes retain their
positions. A per-frame sequence, the same sequence at the end and a checksum gate the DSP side's copy into
`_phrase_control_info` (`src/dsp/control.s`, called from the position hook); an invalid or repeated frame clears the
magic and the enable word, which an engine checks before it uses the words.

The existing loop telemetry carrier (rows 17-20 and 29-31, words +0x44/+0x48) is multiplexed: one of four frames keeps
the legacy loop status; three carry a latched 32-word snapshot of `_phrase_status` tagged with a page and a serial. CF
assembles it in the audio interrupt, so UI-task scheduling cannot lose a page. The UI copies the completed snapshot with
interrupts masked briefly (`dkrt_phrase_snapshot`). No allocation, drawing or settings write occurs in the interrupt.

## Tests

- `tests/control.test.ts` compiles `src/control.c` with stand-ins for the settings: values to control words, the ranges,
  the note table at several tempos, the checksum, persistence (HOLD not kept, another format ignored, values clamped) and
  the status assembly.
- `tests/transport.test.ts` runs the assembled receiver and reply on the concrete SHARC rig
  (`tools/patchkit/pk_phrase_transport_probe.py`): a valid frame is copied, each kind of invalid frame is refused and the
  next valid one accepted, the reply pages are coherent, and the register context is preserved.

```bash
pnpm nx:pilot run core/phrase-control:test-host
```
