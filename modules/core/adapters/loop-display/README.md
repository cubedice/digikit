# Overloop's page (`core/loop-display`)

The page of the loop effect in the firmware's FX view on the Digitakt II (OS 1.17). It is a page adapter of
[`core/fxview`](../fxview/README.md) and a client of the control home [`core/loop-control`](../loop-control/README.md): it
holds no value of its own. The engine is `digikit/overloop`; the behaviour is described in
[its README](../../../fx/overloop/README.md) and in [finding 360](../../../../docs/findings/360-overloop-automatic-loop.md).

## The page

```
 OVERLOOP                      ARMED      title, state: IDLE, ARMED, LOOP, LOOP+ARM, NO LINK
 ──────────────────────────────────
 ╭────────────────────────────────╮
 │──╼╾──╼╾──────╼╾──╼╾────────────│        left channel
 │────╼╾────╼╾──────╼╾────╼╾──────│        right channel
 ╰────────────────────────────────╯
           16 ST 2.00 S                   length or capture limit
    ARM                         XFAD      the bottom row as PRISM's cells: each label above its encoder (E, H)
    ON        ▮─────            LIVE      ARM's state, effective crossfade and XFAD's value
```

| State | Waveform | Caption |
| --- | --- | --- |
| IDLE | rolling stereo record, newest at right | `CYCLE 16 ST 2.00 S` once the cycle is known, otherwise `NO LOOP` |
| ARMED | rolling record | expected cycle, `LAST 21.5 S ONLY` at capacity, or `NOTHING YET` |
| LOOP | held audio and playhead | retained cycle or `CUT TO ITS LAST PART` |
| LOOP+ARM | currently held audio and playhead | the cycle to be captured on the next change |

The marker on the frame follows the held loop's phase. The crossfader
between the controls follows the level the engine applies (it moves by at most 1/256 a frame, and stands at LOOP right after
a capture); the text above it is the value of the control. `ON` is drawn inverted. The cells are 26 pixels wide, E's
centred at x 32 and H's at x 110, as on PRISM's page; x 124 to 127 below the header are left to the FX view's page
indicator. Times have two places below ten seconds and one from there; below a second they are in milliseconds.

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
