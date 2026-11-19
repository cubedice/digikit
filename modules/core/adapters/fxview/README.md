# FX view dispatcher (`core/fxview`)

The one owner of the table slots of the firmware's FX page view on the Digitakt II (OS 1.17). An effect that wants a page
of its own, with its own drawing and keys, is a **page adapter**: it provides up to three functions, and this module calls
them from the view's callbacks. Two adapters exist: `core/phrase-ui` (PRISM's page) and `core/loop-display` (Overloop's).
Either, both or neither may be in an image.

## Why it exists

The FX view has one draw slot, one key slot and one encoder slot. Before, `core/phrase-ui` wrote its functions into them,
so a second effect could not have a page with keys. Now this module writes the slots once and asks each adapter in turn.

| Slot (anchor) | Function here | What it does |
| --- | --- | --- |
| `cf.page.site.phrase_fx_draw` | `fxv_view_draw` | the stock draw, then every adapter's draw |
| `cf.page.site.phrase_help_draw` | `fxv_help_draw` | the stock help bubble, then every adapter's header (the bubble paints over the page's title) |
| `cf.page.site.phrase_fx_key` | `fxv_view_key` | the first adapter that takes the key; else the stock function |
| `cf.page.site.phrase_fx_turn` | `fxv_view_turn` | the first adapter that takes the turn; else the stock function |
| `cf.page.site.phrase_fx_encoder` | `fxv_encoder_interface` | the view's second encoder entry, same as the turn |

The anchors keep their names from the time `core/phrase-ui` owned the slots (`devices/fw/phrase-ui.toml`); their user is
this module.

## The contract of an adapter

`sdk/include/dk/fxview.h`. For a prefix `p` (today `phrase` and `loop`, named in `src/native.c` and as `optional` symbols
in `module.toml`):

```c
int p_fxv_draw(uint8_t *view, uint8_t *bitmap, int header_only); /* 1: this is my page and I painted */
int p_fxv_key(uint8_t *view, uint8_t *event);                    /* 1: taken, the firmware does not see it */
int p_fxv_turn(uint8_t *view, uint8_t *event);                   /* 1: taken */
```

An adapter decides by itself whether the view shows its page (`fxv_page(view)` is the page code; `fxv_visible_view()` is the
FX view when no opaque view covers it) and returns 0 at once when it does not. The functions are weak references: an
adapter that is not in the image costs a test for zero. A third adapter is added by naming its prefix in `src/native.c`
and in the `optional` list.

Helpers for adapters: `fxv_key_flags` (the firmware's key flags as `FXV_PRESS`, `FXV_REPEAT`, `FXV_CHORD`, `FXV_RELEASE`),
`fxv_box` and `fxv_text` (top-left coordinates on the 128 x 64 screen, the label font).

The view and the bitmap are borrowed for the callback only. Nothing here keeps a pointer to a firmware view.

## Checks

The dispatcher has no logic of its own to test on the host: the adapters' models have host tests
(`core/phrase-ui:test-host`, `core/loop-display:test-host`). On the emulated device both pages run through it:
`digikit/prism:test-device--ui` and `digikit/overloop:test-device`; `digikit/prism:test-device--coexistence` has both
adapters in one image.
