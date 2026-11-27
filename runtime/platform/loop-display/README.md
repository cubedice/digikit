# Overloop's page (`core/loop-display`)

The page of the loop effect in the firmware's FX view on the Digitakt II (OS 1.17). It is a page adapter of
[`core/fxview`](../fxview/README.md) and a client of the control home [`core/loop-control`](../loop-control/README.md): it
holds no value of its own. The engine is `digikit/overloop`; the behaviour is described in
[its README](../../../modules/fx/digikit/overloop/README.md) and in [finding 360](../../../docs/findings/360-overloop-automatic-loop.md).

## The page

```
 A01 ▌KIT NAME ≡ 120.0  ▐                 the stock header (the title bubble `Overloop (5/5)` on arrival)
 ╭───╮ ¦──────────────────────────────¦   the stock OVL tab; the waveform in the stock style: dotted end markers,
 │OVL│ ¦  ▂█▆      ▅▂      ▃█▅          ¦   highest positive peak on either channel
 ╰───╯ ¦──▀█▀──────█▀──────▀█▀──────────¦   centre line with negative peaks below
              LOOP 16 ST 2.00 S           what the loop does: length, capture limit, NO LOOP, NO LINK
         ◯      ⁘⁘⁘     ⁘⁘⁘    ▮─────┤    E: ARM's ring (filled while armed), F and G: the stock empty cells, H: XFAD
        ARM                    XFAD       captions at the stock places; their values after a turn or with FUNC + FX
```

| State | Waveform | Line under it |
| --- | --- | --- |
| IDLE | live peaks written at a moving cursor | `CYCLE 16 ST 2.00 S` once the cycle is known, otherwise `NO LOOP` |
| ARMED | live peaks written at a moving cursor | expected cycle, `LAST 21.5 S ONLY` at capacity, or `NOTHING YET` |
| LOOP | held audio and playhead | retained cycle or `CUT TO ITS LAST PART` |
| LOOP+ARM | currently held audio and playhead | the cycle to be captured on the next change |

The header, the effect's tab and the page indicator are the stock FX view's: the page paints x 16 to 123 below the header
and leaves the level strip's place under the tab empty (LEVEL turns XFAD here). The waveform has the stock waveform's
place: its end markers at x 25 and 121, the engine's 120 columns merged into the 95 between them (the largest peak of each
side), rows 12 to 32 about the centre line on row 22. Each column extends from the lowest sample on either channel to the
highest, with silence at the centre line. Display gain follows the strongest peak in the visible window down to about one
eighth of full scale. A quieter phrase uses proportionally less height. This applies only to the display.
Opposite-polarity stereo peaks stay visible. The ticks at the edges follow the held loop's phase. The crossfader follows
the level the engine applies (it moves by at most 1/256 a frame, and stands at LOOP right after a capture). The cells are
the stock ones, 26 pixels wide, centred at x 33, 59, 85 and 111; ARM's caption is `ARMD`, inverted, while ARM is on. A
caption shows its value (`ON`/`OFF`, `LIVE`/`64`/`LOOP`) while the view's own focus countdown runs after a turn of E, H or
LEVEL, and while FUNC + FX is held (`fxv_focus_active`, `fxv_values_shown`). Times have two places below ten seconds and one
from there; below a second they are in milliseconds.

| Control | Does |
| --- | --- |
| press E | ARM, or take the arm back |
| turn E | right: ARM; left: take the arm back. A turn toward the state that is already asked for does nothing |
| press H | ARM, or take the arm back (as on PRISM's page) |
| H, or LEVEL | XFAD, a step a tick, 0 (LIVE) to 127 (LOOP) |
| FUNC + NO | drop the loop |
| A to D, F, G, their presses | taken by the page, without effect |

ARM is a counter in the home: each change of it toggles the engine. So that a turn of several ticks moves it once, the page
counts the state it asked for until the status shows it (`ld_observe`, each time the page reads the status), the status is
not fresh, or 750 blocks of the engine's heartbeat have passed (`LD_ARM_SETTLE`). Without a fresh status every press moves
the counter, as before.

Everything else (the page keys, PLAY, STOP, the track keys, FUNC + FX to the next page) goes to the firmware.

## How it is built

- `src/model.c`, `model.h`: the keys and turns, the texts and the drawing, against a canvas of two functions. No firmware
  type; tested on the host.
- `src/native.c`: the adapter functions `loop_fxv_draw`, `loop_fxv_key`, `loop_fxv_turn` that `core/fxview` calls, and
  `loop_view_update` in the view's update slot (`cf.fx.site.fx_update`), which asks for a repaint when the status has
  moved on by 48 blocks (about 31 times a second at most), at once after an edit, and when the link comes or goes.
- The page is the loop effect's when the view's page code is the one the home reports (`dkrt_loop_page`, which the fx
  service tells the home with the effect's values). Without a loop effect loaded no page is taken.
- Diagnostics a device test reads: `loop_ui_debug` (0 paints, 3 keys taken, 4 turns taken, 6 the page is on top, 7 the
  view's page code), `loop_ui_values` (the home's values at the last callback), `loop_ui_snapshot` (the status), and
  `loop_ui_page` (the page as last painted: the texts, the mark and the fader).

## Checks

```bash
pnpm nx:pilot run core/loop-display:test-host     # keys, turns, the texts of every state, the drawing's bounds
pnpm nx:pilot run digikit/overloop:test-device    # the page on the emulated Digitakt II, in every state
```

The module is in the patch sets `overloop` and `canopy-overloop-prism`. Nothing ran on hardware.
