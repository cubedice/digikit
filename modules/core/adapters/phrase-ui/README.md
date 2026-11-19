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
| G | INPT: a tick up LOOP, a tick down LIVE, only while there is a loop | a short press: INPT, only while there is a loop. Held: the TRIG keys turn tracks on and off, their LEDs show them | `INPT`, `LIVE` or `LOOP`, and under it the tracks the effect hears (`5 TRK`, `NO TRK`); while G is held the tracks and `TRIGS` inverted |
| H | XFAD, Overloop's crossfader, a step a tick, only while there is a loop | ARM, while Overloop reports | `XFAD`: `--` and `NO LOOP`; `ARMD` and the steps the next change keeps (`16 ST`, `CUT`, `WAIT`); with a loop `LIVE`, a number or `LOOP`, and the crossfader as it sounds drawn below, or `ARMD` inverted |
| FUNC + NO | | drop the loop | |

### The tracks

While G is held on the page that is on top, the 16 TRIG keys choose the tracks the effect hears, and their LEDs show them:
white (`0x71`) for a track it hears, the dim level (`0x02`) for the others. A TRIG key turns its track on or off at its
press; the key plays no note, enters no step and selects no track or pattern, and its release is the page's too, also
when G or the page went first. A key down before G was is the firmware's. When G is released, when another view covers
the page or when the FX view shows another page, every TRIG key and LED is the firmware's again. The release of G is
INPT's switch only when it ends a short press: no TRIG key and no turn of G during the hold, and less than 0.4 s
(`PU_SHORT_BLOCKS`, 600 blocks of the engine's heartbeat; without a fresh heartbeat any hold counts as short). The tracks
are `core/phrase-control`'s value `PH_P_TRKS` (bit t for track t + 1), saved with the other values; the provider gives
the fx service a full send amount (127) for each of them and 0 for the others (`phrase_sends`), and the effect row's
amount words carry them to the send route.

INPT is `core/phrase-control`'s value (not saved); XFAD, ARM and the drop are Overloop's, through its home
`core/loop-control` (`<dk/loop.h>`), which is optional here: in an image without it (the patch set `phrase`) the page has no
loop, INPT stays LIVE and H shows `--`. `phrase_ui_tick`, once a UI pass also when the page is not shown, puts INPT at LOOP
when the loop count in Overloop's status moves (a new loop) and at LIVE when there is no loop (a status without one, or
no status for 60 more passes). While INPT is LOOP and there is a loop the header says `LOOP 16 ST 2.00 S` in place of the
zone and the time. The presses of D to F are taken and do nothing. UP, DOWN, FX and every other key go to the stock FX
view: the page is one entry among the FX pages.

The time in the header and in the RATE cell is the engine's (`PH_S_TIME_L`, `PH_S_TIME_R`): the geometric mean of the
two sides, since SKEW makes one shorter and one longer by the same ratio. Without a status the header says `NO LINK`.

## How it is bound

This module is a page adapter of [`core/fxview`](../fxview/README.md), which owns the five table slots of the FX view
(its draw, key and encoder slots, its encoder-interface thunk, and the draw slot of the passive help view; anchors in
`devices/fw/phrase-ui.toml`) and calls `phrase_fxv_draw`, `phrase_fxv_key` and `phrase_fxv_turn` here. No instruction
of the FX view is replaced (the two hooks of the tracks, below, are before the key dispatcher and the LED setter). An adapter function returns 0, and the firmware goes on as it would, unless the page the view shows is the owner's (`dkrt_phrase_active`) and the view is
the visible FX view; so the binding does not depend on how the effect is routed, and every other FX page is the stock
one. The view's page getter is vtable slot 34. The secondary encoder receiver is `view+4`, so that thunk adjusts the
receiver before entering the shared handler. Eligibility uses the firmware's resolved drawable suffix, which removes FX
under an opaque modal while permitting a passive transparent view above it. A post-HelpBubble paint wrapper restores only
the ten-pixel header while that FX view remains eligible. View layout background is in finding 71; the declarations and
local 1.17 instruction windows are the version-specific binding evidence.

The TRIG keys and LEDs are not the FX view's: the firmware's track and grid handlers take the keys before a view sees
them. Two hooks before firmware functions (`devices/fw/phrase-ui.toml`, the input overlay's from before step 2 of the
redesign) give them to the page: `cf.page.hook.phrase_key_dispatch` before the primary ViewController key dispatcher
(`phrase_key_dispatch`: a TRIG event goes to the model while G holds the selection, or when it is the release of a TRIG
key the model took; every other event goes on to the dispatcher unchanged) and `cf.page.hook.phrase_led_set` before the
LedManager setter (`phrase_led_set`: while G holds the selection, the values of the logical LEDs 26 to 41 at layer 2; the
next LED frame clears every claim). Neither touches audio code.

The model owns complete key gestures: a press it took keeps its release, also when another view covers the page
meanwhile, and a release that was lost under a cover does not hold the next press back.

## Tests

`tests/ui.test.ts` compiles the model and the renderer the firmware uses (`src/model.c`, `src/render.c`) with a stand-in
provider: every encoder to its value and step, the three presses, the keys that pass, FUNC + RATE, the note steps with
SYNC, every text, the cells for the states the plan draws, that everything drawn stays on the screen, and the tracks: G
held and the TRIG keys, their LEDs, the texts of INPT held and not held, the release of a TRIG key after G, a key down
before G, the page losing the top while G is held, and which holds of G switch INPT. The native glue (`src/native.c`,
the two hooks) is covered by PRISM's device tests (`modules/fx/prism/tests/ui.device.test.ts`: the TRIG LEDs read from
the panel, the amount words of the DSP row; `send.device.test.ts`: no note and no step from a TRIG key while G is held),
which read the painted page from `phrase_ui_page`.
