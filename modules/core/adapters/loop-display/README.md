# Overloop's page (`core/loop-display`)

The page of the loop effect in the firmware's FX view on the Digitakt II (OS 1.17). It is a page adapter of
[`core/fxview`](../fxview/README.md) and a client of the control home [`core/loop-control`](../loop-control/README.md): it
holds no value of its own. The engine is `digikit/overloop`; the behaviour is described in
[its README](../../../fx/overloop/README.md) and in [finding 360](../../../../docs/findings/360-overloop-automatic-loop.md).

## The page

```
 OVERLOOP                      ARMED      title, state: IDLE, ARMED, LOOP, LOOP+ARM, NO LINK
 ──────────────────────────────────
         NEXT CHANGE KEEPS                line 1
           16 ST 2.00 S                   line 2
 |-------|---▮---|-------|-------|        the timeline: one cycle, quarter marks, where it stands
 LIVE ▮────────────────────── LOOP        the crossfader as it sounds
 ──────────────────────────────────
      ARM ON            XFAD LIVE         the two controls
```

| State | Line 1 | Line 2 |
| --- | --- | --- |
| no loop, nothing armed | `NO LOOP` | `CYCLE 16 ST 2.00 S` once a step has been heard; empty while stopped |
| armed | `NEXT CHANGE KEEPS` | `16 ST 2.00 S`; `LAST 21.5 S ONLY` when the cycle is longer than the buffers; `NOTHING YET` while a change would take nothing (stopped) |
| a loop | `LOOP 16 ST 2.00 S` | `CUT TO ITS LAST PART` for a cut loop, else empty |
| a loop, armed | as armed | as armed |

The mark on the timeline is the step of the pattern that plays, or the loop's place once there is a loop. The crossfader
drawing follows the level the engine applies (it moves by at most 1/256 a frame, and stands at LOOP right after a capture); the
`XFAD` text is the value of the control. `ARM ON` is drawn inverted. Times have two places below ten seconds and one from
there; below a second they are in milliseconds.

| Control | Does |
| --- | --- |
| press H | ARM, or take the arm back |
| H, or LEVEL | XFAD, a step a tick, 0 (LIVE) to 127 (LOOP) |
| FUNC + NO | drop the loop |
| A to G, their presses | taken by the page, without effect |

Everything else (the page keys, PLAY, STOP, the track keys, FUNC + FX to the next page) goes to the firmware.

## How it is built

- `src/model.c`, `model.h`: the keys and turns, the texts and the drawing, against a canvas of two functions. No firmware
  type; tested on the host.
- `src/native.c`: the adapter functions `loop_fxv_draw`, `loop_fxv_key`, `loop_fxv_turn` that `core/fxview` calls, and
  `loop_view_update` in the view's update slot (`cf.fx.site.fx_update`), which asks for a repaint when the status has
  moved on by 60 blocks (25 times a second at most), at once after an edit, and when the link comes or goes.
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
