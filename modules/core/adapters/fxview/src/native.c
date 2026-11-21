/* DTII 1.17 FxPageView integration: the one owner of the view's table slots. The view and bitmap are borrowed for the
 * current callback only; no firmware View survives in our state.
 *
 * Every callback runs the stock function and the page adapters of the image (<dk/fxview.h>). An adapter is an optional
 * symbol: without it the call is skipped, and without any adapter the view behaves as the firmware's. */
#include <stdint.h>
#include <dk/fxview.h>
#include "dk_fw_draw.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"
extern void fxv_stock_draw(uint8_t *, uint8_t *);
extern void fxv_stock_help_draw(uint8_t *, uint8_t *);
extern uint32_t fxv_stock_key(uint8_t *, uint8_t *);
extern uint32_t fxv_stock_turn(uint8_t *, uint8_t *);
extern const uint32_t fxv_label_font;
#define OPTIONAL __attribute__((weak))
extern int phrase_fxv_draw(uint8_t *, uint8_t *, int) OPTIONAL;
extern int phrase_fxv_key(uint8_t *, uint8_t *) OPTIONAL;
extern int phrase_fxv_turn(uint8_t *, uint8_t *) OPTIONAL;
extern int loop_fxv_draw(uint8_t *, uint8_t *, int) OPTIONAL;
extern int loop_fxv_key(uint8_t *, uint8_t *) OPTIONAL;
extern int loop_fxv_turn(uint8_t *, uint8_t *) OPTIONAL;
void fxv_view_draw(uint8_t *, uint8_t *);
extern uint32_t fxv_stock_dispatch(uint8_t *, uint8_t *);
extern int phrase_dispatch_trig(uint8_t *) OPTIONAL;
extern int relay_trig(uint8_t *) OPTIONAL;
extern void rtm_stop_key(uint32_t) OPTIONAL;

/* TRIG keys reach the stock sequencer before they reach the FX view. */
uint32_t fxv_key_dispatch(uint8_t *manager, uint8_t *event)
{
    uint32_t key = *(uint32_t *)(event + 12);
    if (key == 21 && rtm_stop_key)
        rtm_stop_key(*(uint32_t *)(event + 16));
    if (key >= 25 && key < 41 &&
        ((phrase_dispatch_trig && phrase_dispatch_trig(event)) ||
         (relay_trig && relay_trig(event))))
        return 1;
    return fxv_stock_dispatch(manager, event);
}

static uint32_t word(const uint8_t *p, uint32_t off)
{
    return *(const uint32_t *)(p + off);
}
static uint8_t *pointer(const uint8_t *p, uint32_t off)
{
    return (uint8_t *)(uintptr_t)word(p, off);
}
uint32_t fxv_page(uint8_t *v)
{
    uint32_t *vt = *(uint32_t **)v;
    return ((uint32_t (*)(uint8_t *))(uintptr_t)vt[34])(v);
}
uint8_t *fxv_visible_view(void)
{
    uint8_t *brain = dk_fw_view_brain_instance();
    uint8_t *manager = brain ? dk_fw_view_brain_view_manager(brain) : 0;
    if (!manager)
        return 0;
    /* The native drawable suffix excludes views covered by an opaque modal.
     * A passive transparent view can sit above FX (including on a normal FX
     * page), so the list's final node is not the input owner. Actual key and
     * encoder dispatch still decides whether our callbacks receive an event. */
    uint8_t *sentinel = manager + 0x14, *node = pointer(manager, 0x1c), *found = 0;
    for (uint32_t n = 0; n < 32 && node && node != sentinel; ++n, node = pointer(node, 0)) {
        uint8_t *view = pointer(node, 8);
        if (!view || view[0x30])
            continue;
        uint8_t *vt = pointer(view, 0);
        if (vt && word(vt, 16) == (uint32_t)(uintptr_t)fxv_view_draw)
            found = view;
    }
    return found;
}
uint32_t fxv_key_flags(uint32_t flags)
{
    /* Native key-up has bit0 clear; bit4 identifies the release/click event.
     * Bit1 notifications may accompany a held FUNC chord, not a release. */
    if (flags & 16)
        return FXV_RELEASE;
    if (flags & 8)
        return FXV_REPEAT;
    if (flags & 1)
        return FXV_PRESS;
    if (flags & 2)
        return FXV_CHORD;
    return FXV_RELEASE;
}
void fxv_box(void *bitmap, int x, int y, int w, int h, int ink)
{
    if (w <= 0 || h <= 0)
        return;
    dk_fw_draw_fill_rect(bitmap, x, 64 - y - h, x + w - 1, 63 - y, ink);
}
void fxv_text(void *bitmap, int x, int y, const char *s, int ink)
{
    uint8_t *font = (uint8_t *)(uintptr_t)fxv_label_font;
    int width = 0;
    if (!ink) {
        /* Native font descriptor: +8 points at the per-character widths.
         * An inverted text is drawn as usual and its box, one pixel larger
         * on every side, is then inverted. */
        const uint8_t *widths = pointer(font, 8);
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
            width += widths[*p] + 1;
        if (width)
            --width;
    }
    dk_fw_text_draw_box(bitmap, font, x, 59 - y, 1, 0, 0, "%s", s);
    if (!ink && width)
        fxv_box(bitmap, x - width / 2 - 1, y - 1, width + 2, 7, -1);
}
void fxv_view_draw(uint8_t *view, uint8_t *bitmap)
{
    fxv_stock_draw(view, bitmap);
    if (phrase_fxv_draw)
        phrase_fxv_draw(view, bitmap, 0);
    if (loop_fxv_draw)
        loop_fxv_draw(view, bitmap, 0);
}
void fxv_help_draw(uint8_t *help, uint8_t *bitmap)
{
    fxv_stock_help_draw(help, bitmap);
    uint8_t *view = fxv_visible_view();
    if (!view)
        return;
    /* The native passive HelpBubble paints after the FX view, including a
     * persistent global page label. A page adapter reclaims only its header,
     * leaving the bubble untouched whenever its page is not visible. */
    if (phrase_fxv_draw)
        phrase_fxv_draw(view, bitmap, 1);
    if (loop_fxv_draw)
        loop_fxv_draw(view, bitmap, 1);
}
uint32_t fxv_view_key(uint8_t *view, uint8_t *event)
{
    if (phrase_fxv_key && phrase_fxv_key(view, event))
        return 1;
    if (loop_fxv_key && loop_fxv_key(view, event))
        return 1;
    return fxv_stock_key(view, event);
}
uint32_t fxv_view_turn(uint8_t *view, uint8_t *event)
{
    if (phrase_fxv_turn && phrase_fxv_turn(view, event))
        return 1;
    if (loop_fxv_turn && loop_fxv_turn(view, event))
        return 1;
    return fxv_stock_turn(view, event);
}
uint32_t fxv_encoder_interface(uint8_t *receiver, uint8_t *event)
{
    return fxv_view_turn(receiver - 4, event);
}
