# Phrase FX page

`core/phrase-ui` draws the one page of the loaded phrase effect in the native FX view and takes its encoders and presses.
It does not identify a content module by name or effect ID: `core/phrase-control` says which native FX page is the
owner's and holds every value.

## The page

The page is laid out as the stock send effects' pages. The stock FX view draws the header (pattern, kit and tempo, with
the title bubble `PRISM (4/5)` for a moment on arrival), the effect's tab (`PRS`) and the page indicator; the page paints
x 16 to 123 below the header and the level strip under the tab: VOL, the effect's MIX (the stock strip there would show
the effect row's own MIX, which stays at unity). Eight cells in two rows at the stock centres (x 33, 59, 85, 111) have a
graphic above their caption. As on the stock pages a turned cell's caption shows its value while the view's own focus
countdown runs (`fxv_focus_touch`, `fxv_focus_active`), and holding FUNC + FX shows every value (`fxv_values_shown`);
release FX to return to the captions. Tap FUNC + FX to advance to the next effect.

| | Turn | Press | Caption | Value (after a turn, FUNC + FX held) |
| --- | --- | --- | --- | --- |
| LEVEL | MIX | | `VOL` under the strip | its number |
| A | ZONE, four ticks a zone | FLIP | `ZONE`, `FLIP` inverted while FLIP is on | the zone's name |
| B | RATE: a coarse step a tick, a fine step with FUNC held; with SYNC a ratio of the zone's 1/1, four ticks each, `/2` to `X2` | SYNC | `RATE`; the graphic is the time (`387 MS`, `1.25 S`) or the ratio (`1/1`, `X1.5`, `/1.33`), `NO LINK` without a status | the time SYNC gives, or `FREE` |
| C | RPTS, dashes to ∞ | HOLD | `RPTS`, `HOLD` inverted while HOLD is on | the number, or `INF` |
| D | COLR | | `COLR` | signed distance from the uncoloured point (96, three quarters of the way up); negative darkens, positive thins and brightens |
| E | HALO | | `HALO` | the number |
| F | SKEW | Cycle SKEW, PONG, SWAP | `SKEW`, or `PONG` / `SWAP` | `L 12` or `R 12` for the longer side and distance from centre, `0` in PONG |
| G | INPT, a two-position switch: four ticks to the right LOOP, four to the left LIVE, at any time (also without a loop). A tick back toward where it is clears the count, so the tick a knob can make as it is let go of switches nothing | nothing. Held: the TRIG keys turn tracks on and off, their LEDs show them | `INPT`; `NO TRK` while no track is chosen, `SILENT` at LOOP while there is no loop; while G is held `TRIGS`, inverted | `LIVE` or `LOOP`; while G is held the tracks the effect hears (`5 TRK`) |
| H | XFAD, Overloop's crossfader, a step a tick, only while there is a loop | ARM, while Overloop reports | `XFAD`, `ARMD` inverted while armed; the slider is the applied crossfader, without a knob before a loop exists | `LIVE`, a number, or `LOOP`; before a loop exists `--`, or while armed `16 ST`, `CUT` or `WAIT` |
| FUNC + NO | | drop the loop | | |

### The tracks

While G is held on the page that is on top, the 16 TRIG keys choose the tracks the effect hears, and their LEDs show them:
white (`0x71`) for a track it hears, the dim level (`0x02`) for the others. A TRIG key turns its track on or off at its
press; the key plays no note, enters no step and selects no track or pattern, and its release is the page's too, also
when G or the page went first. A key down before G was is the firmware's. When G is released, when another view covers
the page or when the FX view shows another page, every TRIG key and LED is the firmware's again. A track edit while LOOP
is empty returns INPT to LIVE. With a held loop, choosing tracks prepares the LIVE input and keeps the loop routed.
The tracks are `core/phrase-control`'s value `PH_P_TRKS` (bit t for track t + 1), saved with the other values. The provider gives
the fx service a full send amount (127) for each of them and 0 for the others (`phrase_sends`), and the effect row's
amount words carry them to the send route.

INPT is `core/phrase-control`'s value (not saved); XFAD, ARM and the drop are Overloop's, through its home
`core/loop-control` (`<dk/loop.h>`), which is optional here: in an image without it (the patch set `phrase`) the page has no
loop and H shows `--`; G still turns INPT, and at LOOP PRISM hears silence. `phrase_ui_tick`, once a UI pass also when the
page is not shown, puts INPT at LOOP when the loop count in Overloop's status moves (a new loop), and at LIVE when the
loop goes away (a status with a loop, then one without: a drop, or a loop lost with its lease) or when Overloop, having
reported, stops reporting (no status for 60 more passes; once, so that G may put INPT at LOOP again). The loop's own line
(`LOOP 16 ST 2.00 S`) is Overloop's page's; the page keeps it in `phrase_ui_page` for the device tests only. The presses of
D to F are taken and do nothing. UP, DOWN, FX and every other key go to the stock FX view: the page is one entry among the
FX pages.

The time in RATE's graphic when free, or in its caption when synced and shown, is the engine's (`PH_S_TIME_L`,
`PH_S_TIME_R`). It is the geometric mean of the two sides, since SKEW makes one shorter and one longer by the same
ratio. Without a status RATE's graphic says `NO LINK`.

## How it is bound

This module is a page adapter of [`core/fxview`](../fxview/README.md), which owns the five table slots of the FX view
(its draw, key and encoder slots, its encoder-interface thunk, and the draw slot of the passive help view; anchors in
`ports/fw/phrase-ui.toml`) and calls `phrase_fxv_draw`, `phrase_fxv_key` and `phrase_fxv_turn` here. No instruction
of the FX view is replaced (the two hooks of the tracks, below, are before the key dispatcher and the LED setter). An adapter function returns 0, and the firmware goes on as it would, unless the page the view shows is the owner's (`dkrt_phrase_active`) and the view is
the visible FX view; so the binding does not depend on how the effect is routed, and every other FX page is the stock
one. The view's page getter is vtable slot 34. The secondary encoder receiver is `view+4`, so that thunk adjusts the
receiver before entering the shared handler. Eligibility uses the firmware's resolved drawable suffix, which removes FX
under an opaque modal while permitting a passive transparent view above it. A post-HelpBubble paint wrapper restores only
the ten-pixel header while that FX view remains eligible. View layout background is in finding 71; the declarations and
local 1.17 instruction windows are the version-specific binding evidence.

The TRIG keys and LEDs are not the FX view's: the firmware's track and grid handlers take the keys before a view sees
them. Two hooks before firmware functions (`ports/fw/phrase-ui.toml`, the input overlay's from before step 2 of the
redesign) give them to the page: `cf.page.hook.phrase_key_dispatch` before the primary ViewController key dispatcher
(`phrase_key_dispatch`: a TRIG event goes to the model while G holds the selection, or when it is the release of a TRIG
key the model took; every other event goes on to the dispatcher unchanged) and `cf.page.hook.phrase_led_set` before the
LedManager setter (`phrase_led_set`: while G holds the selection, the values of the logical LEDs 26 to 41 at layer 2; the
next LED frame clears every claim). Neither touches audio code.

The model owns complete key gestures: a press it took keeps its release, also when another view covers the page
meanwhile, and a release that was lost under a cover does not hold the next press back.

## Tests

`tests/ui.test.ts` compiles the model and the renderer the firmware uses (`src/model.c`, `src/render.c`) with a stand-in
provider: every encoder to its value and step, the three presses, the keys that pass, FUNC + RATE, the ratio steps with
SYNC, every text, G's turn both ways with and without a loop, no switch on its press, INPT staying at LOOP without a loop,
LIVE at a drop and LOOP at an armed change, the cells for the states the plan draws, that everything drawn stays on the screen, and the tracks: G
held and the TRIG keys, their LEDs, the texts of INPT held and not held, the release of a TRIG key after G, a key down
before G, the page losing the top while G is held, and that no hold of G switches INPT. The native glue (`src/native.c`,
the two hooks) is covered by PRISM's device tests (`modules/fx/digikit/prism/tests/ui.device.test.ts`: the TRIG LEDs read from
the panel, the amount words of the DSP row; `send.device.test.ts`: no note and no step from a TRIG key while G is held),
which read the painted page from `phrase_ui_page`.
