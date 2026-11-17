/* DTII 1.17 FxPageView integration. The page and bitmap are borrowed for the
 * current callback only; no firmware View survives in our state. */
#include <stdint.h>
#include "model.h"
#include "render.h"
#include "dk_fw_draw.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"
#include "dk_fw_input.h"
extern void phrase_stock_draw(uint8_t *, uint8_t *);
extern void phrase_stock_help_draw(uint8_t *, uint8_t *);
extern uint32_t phrase_stock_key(uint8_t *, uint8_t *);
extern uint32_t phrase_stock_key_dispatch(uint8_t *, uint8_t *);
extern uint32_t phrase_stock_turn(uint8_t *, uint8_t *);
extern void phrase_stock_led(uint8_t *, uint32_t, uint32_t, uint32_t);
extern void phrase_focus_touch(uint8_t *, uint32_t);
extern uint8_t phrase_focus_active(uint8_t *, uint32_t);
extern const uint32_t phrase_label_font;
extern const uint32_t fxpages_info[];
void phrase_view_draw(uint8_t *, uint8_t *);
static PhraseUi ui;
uint32_t phrase_ui_debug[8];
/* Native acceptance diagnostics: owner page, receiver, eligible FX view,
 * generation, freshness, callback count, eligible vtable and callback page. */
uint32_t phrase_ui_binding[8];
/* Read-only diagnostics for the native acceptance test, refreshed on the UI
 * task from the provider. No debug write is accepted as a parameter edit. */
uint32_t phrase_ui_values[PH_PARAM_COUNT];
static uint32_t painted, paint_revision, last_owner, paint_fresh;
static void reconcile(void)
{
    uint32_t physical[2] = {0, 0};
    for (uint32_t key = 1; key <= 48; ++key)
        if ((ui.cancelled[key >> 5] & (1u << (key & 31))) && dk_fw_input_key_down(key))
            physical[key >> 5] |= 1u << (key & 31);
    pu_reconcile(&ui, physical);
}
static uint32_t word(const uint8_t *p, uint32_t off)
{
    return *(const uint32_t *)(p + off);
}
static uint8_t *pointer(const uint8_t *p, uint32_t off)
{
    return (uint8_t *)(uintptr_t)word(p, off);
}
static uint32_t page(uint8_t *v)
{
    uint32_t *vt = *(uint32_t **)v;
    return ((uint32_t (*)(uint8_t *))(uintptr_t)vt[34])(v);
}
static uint8_t *visible_fx_view(void)
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
        if (vt && word(vt, 16) == (uint32_t)(uintptr_t)phrase_view_draw)
            found = view;
    }
    return found;
}
static int context(uint8_t *view)
{
    uint32_t owner = dkrt_phrase_active(), status[PH_STATUS_WORDS];
    uint32_t fresh = dkrt_phrase_snapshot(status, PH_STATUS_WORDS);
    pu_status(&ui, status, fresh);
    ui.title = dkrt_phrase_title();
    uint32_t generation = status[PH_S_OWNER_GENERATION / 4];
    /* Keep the last known generation during initial status assembly. */
    if (owner && !generation)
        generation = ui.generation;
    if (!owner)
        generation = 0;
    uint8_t *top = visible_fx_view();
    uint32_t native_page = page(view);
    phrase_ui_binding[0] = owner;
    phrase_ui_binding[1] = (uint32_t)(uintptr_t)view;
    phrase_ui_binding[2] = (uint32_t)(uintptr_t)top;
    phrase_ui_binding[3] = generation;
    phrase_ui_binding[4] = fresh;
    ++phrase_ui_binding[5];
    phrase_ui_binding[6] = top ? word(top, 0) : 0;
    phrase_ui_binding[7] = native_page;
    pu_context(&ui, owner && owner == native_page && view == top, generation);
    last_owner = owner;
    for (uint32_t i = 0; i < PH_PARAM_COUNT; ++i)
        phrase_ui_values[i] = dkrt_phrase_get(i);
    phrase_ui_debug[1] = ui.page;
    phrase_ui_debug[2] = ui.overlay;
    phrase_ui_debug[5] = pu_effective_mask();
    phrase_ui_debug[6] = ui.active;
    phrase_ui_debug[7] = page(view);
    return ui.active != 0;
}
typedef struct {
    uint8_t *view, *bitmap;
} NativeCanvas;
static void native_box(void *ctx, int x, int y, int w, int h, int ink)
{
    NativeCanvas *n = ctx;
    dk_fw_draw_fill_rect(n->bitmap, x, 64 - y - h, x + w - 1, 63 - y, ink);
}
static void native_text(void *ctx, int x, int y, const char *s, int ink)
{
    NativeCanvas *n = ctx;
    uint8_t *font = (uint8_t *)(uintptr_t)phrase_label_font;
    int width = 0;
    if (!ink) {
        /* Native font descriptor: +8 points at the per-character widths.
         * Clear before drawing so inverse labels also work with transparent
         * native text rendering on an already-solid held-source tile. */
        const uint8_t *widths = pointer(font, 8);
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
            width += widths[*p] + 1;
        if (width)
            --width;
        if (width)
            native_box(ctx, x - width / 2, y, width, 5, 0);
    }
    dk_fw_text_draw_box(n->bitmap, font, x, 59 - y, 1, 0, 0, "%s", s);
    if (!ink && width)
        native_box(ctx, x - width / 2, y, width, 5, -1);
}
static void native_knob(void *ctx, uint32_t cell, uint32_t value)
{
    NativeCanvas *n = ctx;
    uint32_t *vt = *(uint32_t **)n->view;
    uint8_t *(*access)(uint8_t *, uint32_t, int32_t) = (void *)(uintptr_t)vt[49];
    uint32_t code = fxpages_info[16];
    uint8_t *a = access(n->view, code, -1);
    if (!a)
        return;
    uint32_t *av = *(uint32_t **)a;
    void (*draw)(uint8_t *, uint32_t, int32_t, uint32_t, uint8_t *, uint8_t *, int32_t, int32_t) =
        (void *)(uintptr_t)av[24];
    draw(a, code, (int32_t)value << 8, 0, n->view + 0xb4 + 20 * cell, n->bitmap,
         24 + 26 * (int32_t)(cell % 4), cell < 4 ? 34 : 7);
}
void phrase_view_draw(uint8_t *view, uint8_t *bitmap)
{
    phrase_stock_draw(view, bitmap);
    if (!context(view))
        return;
    NativeCanvas n = {view, bitmap};
    PhraseCanvas c = {&n, native_box, native_text, native_knob};
    ui.show_values = view[0x190] != 0;
    for (uint32_t i = 0; i < 8; ++i)
        if (phrase_focus_active(view + 0x94, i)) {
            uint32_t p = pu_parameter(ui.page, i);
            if (p < PH_PARAM_COUNT) {
                ui.focus = p;
                ui.focus_ticks = 1;
            }
        }
    pu_render(&ui, &c);
    painted = ui.status[PH_S_HEARTBEAT / 4];
    paint_revision = ui.revision;
    paint_fresh = ui.fresh;
    ++phrase_ui_debug[0];
    phrase_ui_debug[1] = ui.page;
    phrase_ui_debug[2] = ui.overlay;
}
void phrase_help_draw(uint8_t *help, uint8_t *bitmap)
{
    phrase_stock_help_draw(help, bitmap);
    uint8_t *view = visible_fx_view();
    if (!view || !context(view))
        return;
    /* The native passive HelpBubble paints after the FX view, including a
     * persistent global page label. Reclaim only our header, leaving its
     * normal rendering untouched whenever the phrase page is not visible. */
    NativeCanvas n = {view, bitmap};
    PhraseCanvas c = {&n, native_box, native_text, native_knob};
    pu_render_header(&ui, &c);
}
static uint32_t model_key(uint8_t *event)
{
    uint32_t key = word(event, 12), flags = word(event, 16);
    /* FUNC/TRIG may have been pressed before this view was on top. Native
     * key state supplies modifiers; never manufacture ownership of them. */
    for (uint32_t k = 16; k <= 48; ++k)
        if (k != key && !(ui.owned[k >> 5] & (1u << (k & 31)))) {
            if (dk_fw_input_key_down(k))
                ui.held[k >> 5] |= 1u << (k & 31);
            else
                ui.held[k >> 5] &= ~(1u << (k & 31));
        }
    uint32_t result = pu_key(&ui, key, pu_native_flags(flags));
    return result;
}
uint32_t phrase_view_key(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    uint32_t key = word(event, 12), result = model_key(event);
    if (result == PU_PASS)
        return phrase_stock_key(view, event);
    ++phrase_ui_debug[3];
    dk_fw_view_invalidate(view);
    /* The native FX-key gesture owns global advance and held show-values.
     * Its complete event stream continues after the overlay closes; the
     * model only keeps its release separate from the overlay's NO/H exit. */
    if (key == PU_FX)
        return phrase_stock_key(view, event);
    return 1;
}
uint32_t phrase_key_dispatch(uint8_t *manager, uint8_t *event)
{
    uint32_t key = word(event, 12);
    if (key >= PU_TRIG && key < PU_TRIG + 16) {
        uint8_t *view = visible_fx_view();
        if (view)
            (void)context(view);
        else
            pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
        uint32_t bit = 1u << (key & 31),
                 owned = (ui.owned[key >> 5] | ui.cancelled[key >> 5]) & bit;
        /* Native track/grid handlers consume TRIG before FxPageView. Own
         * input selection at the manager boundary so it cannot also audition,
         * select a track, mute, or enter a sequencer lock. An owned release
         * stays ours even after an opaque modal has replaced the FX view. */
        if (((ui.active && ui.overlay) || owned) && model_key(event) != PU_PASS) {
            ++phrase_ui_debug[3];
            if (view)
                dk_fw_view_invalidate(view);
            return 1;
        }
    }
    return phrase_stock_key_dispatch(manager, event);
}
uint32_t phrase_view_turn(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    uint32_t encoder = word(event, 12);
    if (encoder < 1 || encoder > 9 ||
        pu_turn(&ui, encoder - 1, (int32_t)word(event, 16)) == PU_PASS)
        return phrase_stock_turn(view, event);
    if (encoder <= 8)
        phrase_focus_touch(view + 0x94, encoder - 1);
    ++phrase_ui_debug[4];
    dk_fw_view_invalidate(view);
    return 1;
}
uint32_t phrase_encoder_interface(uint8_t *receiver, uint8_t *event)
{
    return phrase_view_turn(receiver - 4, event);
}
void phrase_led_set(uint8_t *manager, uint32_t logical, uint32_t layer, uint32_t value)
{
    if (ui.active && ui.overlay) {
        uint8_t *view = visible_fx_view();
        if (!view || page(view) != dkrt_phrase_active())
            pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
    }
    if (ui.active && ui.overlay && dkrt_phrase_active() == last_owner && logical >= 26 &&
        logical <= 41) {
        value = pu_led(&ui, logical, value);
        /* Layer 1 is the native forced-off layer. Layer 2 is a normal
         * claimed value; the next LED frame clears all priorities. */
        layer = 2;
    }
    phrase_stock_led(manager, logical, layer, value);
}
void phrase_ui_tick(void)
{
    reconcile();
    uint8_t *view = visible_fx_view();
    if (view) {
        if (context(view)) {
            uint32_t seq = ui.status[PH_S_HEARTBEAT / 4];
            if (ui.revision != paint_revision || seq - painted >= 60 || ui.fresh != paint_fresh)
                dk_fw_view_invalidate(view);
        }
        return;
    }
    if (ui.active || dkrt_phrase_active() != last_owner) {
        pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
    }
    phrase_ui_debug[1] = ui.page;
    phrase_ui_debug[2] = ui.overlay;
    phrase_ui_debug[6] = 0;
}
