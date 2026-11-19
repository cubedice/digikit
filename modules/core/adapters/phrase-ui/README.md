# Phrase FX page

`core/phrase-ui` draws the one page of the loaded phrase effect in the native FX view and takes its encoders and presses.
It does not identify a content module by name or effect ID: `core/phrase-control` says which native FX page is the
owner's and holds every value.

## The page

128 x 64, native text and filled rectangles only: a header of ten pixels (the effect's title, the zone's name, the time
the engine plays), the MIX column at the left, and eight cells of 26 pixels in two rows. A cell has three lines: its
label, its value, and a state word.

| | Turn | Press | Cell |
| --- | --- | --- | --- |
| LEVEL | MIX | | the column and its number |
| A | ZONE, four ticks a zone | FLIP | the zone's name, eight marks under it with the chosen one tall, `FLIP` inverted |
| B | RATE: a coarse step a tick, a fine step with FUNC held; with SYNC a note length, four ticks each | SYNC | the time (`387 MS`, `1.25 S`) or the note (`1/8.`), `FREE` or `SYNC` |
| C | RPTS | HOLD | the number, or `INF` with `HOLD` inverted |
| D | COLR | | the distance from the centre, `DARK` or `BRIT` |
| E | HALO | | the number |
| F | SKEW | | `L 12` or `R 12`: the side with the longer time and the distance from the centre |
| G | nothing yet | nothing yet | `INPT`, `LIVE` |
| H | nothing yet | nothing yet | `XFAD`, `--`, `NO LOOP` |

G and H are kept for the loop (the input switch and Overloop's crossfader, steps 3 and 4 of
[the redesign](../../../../docs/plans/prism-overloop-redesign.md)). Their encoders and presses, the presses of D to F and
FUNC + NO are taken by the page, so that the stock page under it does nothing with them, and do nothing. UP, DOWN, FX and
every other key go to the stock FX view: the page is one entry among the FX pages.

The time in the header and in the RATE cell is the engine's (`PH_S_TIME_L`, `PH_S_TIME_R`): the geometric mean of the
two sides, since SKEW makes one shorter and one longer by the same ratio. Without a status the header says `NO LINK`.

## How it is bound

This module is a page adapter of [`core/fxview`](../fxview/README.md), which owns the five table slots of the FX view
(its draw, key and encoder slots, its encoder-interface thunk, and the draw slot of the passive help view; anchors in
`devices/fw/phrase-ui.toml`) and calls `phrase_fxv_draw`, `phrase_fxv_key` and `phrase_fxv_turn` here. No instruction
is replaced. An adapter function returns 0, and the firmware goes on as it would, unless the page the view shows is the owner's (`dkrt_phrase_active`) and the view is
the visible FX view; so the binding does not depend on how the effect is routed, and every other FX page is the stock
one. The view's page getter is vtable slot 34. The secondary encoder receiver is `view+4`, so that thunk adjusts the
receiver before entering the shared handler. Eligibility uses the firmware's resolved drawable suffix, which removes FX
under an opaque modal while permitting a passive transparent view above it. A post-HelpBubble paint wrapper restores only
the ten-pixel header while that FX view remains eligible. View layout background is in finding 71; the declarations and
local 1.17 instruction windows are the version-specific binding evidence.

The model owns complete key gestures: a press it took keeps its release, also when another view covers the page
meanwhile, and a release that was lost under a cover does not hold the next press back.

## Tests

`tests/ui.test.ts` compiles the model and the renderer the firmware uses (`src/model.c`, `src/render.c`) with a stand-in
provider: every encoder to its value and step, the three presses, the keys that pass, FUNC + RATE, the note steps with
SYNC, every text, the cells for the states the plan draws, and that everything drawn stays on the screen. The native
glue (`src/native.c`) is covered by PRISM's device test (`modules/fx/prism/tests/ui.device.test.ts`), which reads the
painted page from `phrase_ui_page`.
