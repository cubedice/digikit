/* DTII 1.17 FxPageView integration. The page and bitmap are borrowed for the
 * current callback only; no firmware View survives in our state.
 *
 * The page is the phrase effect's page in the FX view, whichever way the effect is routed: these callbacks sit in
 * the view's own table and act only while the page the view shows is the owner's (dkrt_phrase_active). */
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
extern uint32_t phrase_stock_turn(uint8_t *, uint8_t *);
extern const uint32_t phrase_label_font;
void phrase_view_draw(uint8_t *, uint8_t *);
static PhraseUi ui;
/* Native acceptance diagnostics: 0 draws, 1 and 2 the encoder and the ticks of the last turn the view got, 3 keys taken,
 * 4 turns taken, 5 turns the view got, 6 the page is on top, 7 the view's page. */
uint32_t phrase_ui_debug[8];
/* Native acceptance diagnostics: owner page, receiver, eligible FX view,
 * generation, freshness, callback count, eligible vtable and callback page. */
uint32_t phrase_ui_binding[8];
/* Read-only diagnostics for the native acceptance test, refreshed on the UI
 * task from the provider. No debug write is accepted as a parameter edit. */
uint32_t phrase_ui_values[PH_PARAM_COUNT];
/* The page as it was last painted: the texts of its cells, for the same test. */
PhrasePage phrase_ui_page;
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
    phrase_ui_debug[6] = ui.active;
    phrase_ui_debug[7] = native_page;
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
         * An inverted text is drawn as usual and its box, one pixel larger
         * on every side, is then inverted. */
        const uint8_t *widths = pointer(font, 8);
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
            width += widths[*p] + 1;
        if (width)
            --width;
    }
    dk_fw_text_draw_box(n->bitmap, font, x, 59 - y, 1, 0, 0, "%s", s);
    if (!ink && width)
        native_box(ctx, x - width / 2 - 1, y - 1, width + 2, 7, -1);
}
static void paint(uint8_t *view, uint8_t *bitmap, int header_only)
{
    NativeCanvas n = {view, bitmap};
    PhraseCanvas c = {&n, native_box, native_text};
    pu_page(&ui, &phrase_ui_page);
    if (header_only)
        pu_render_header(&phrase_ui_page, &c);
    else
        pu_render(&phrase_ui_page, &c);
}
void phrase_view_draw(uint8_t *view, uint8_t *bitmap)
{
    phrase_stock_draw(view, bitmap);
    if (!context(view))
        return;
    paint(view, bitmap, 0);
    painted = ui.status[PH_S_HEARTBEAT / 4];
    paint_revision = ui.revision;
    paint_fresh = ui.fresh;
    ++phrase_ui_debug[0];
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
    paint(view, bitmap, 1);
}
uint32_t phrase_view_key(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    uint32_t key = word(event, 12), flags = word(event, 16);
    /* FUNC may have been pressed before this view was on top. Native
     * key state supplies modifiers; never manufacture ownership of them. */
    for (uint32_t k = 16; k <= 48; ++k)
        if (k != key && !(ui.owned[k >> 5] & (1u << (k & 31)))) {
            if (dk_fw_input_key_down(k))
                ui.held[k >> 5] |= 1u << (k & 31);
            else
                ui.held[k >> 5] &= ~(1u << (k & 31));
        }
    if (pu_key(&ui, key, pu_native_flags(flags)) == PU_PASS)
        return phrase_stock_key(view, event);
    ++phrase_ui_debug[3];
    dk_fw_view_invalidate(view);
    return 1;
}
uint32_t phrase_view_turn(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    uint32_t encoder = word(event, 12);
    phrase_ui_debug[1] = encoder;
    phrase_ui_debug[2] = word(event, 16);
    ++phrase_ui_debug[5];
    if (encoder < 1 || encoder > 9 ||
        pu_turn(&ui, encoder - 1, (int32_t)word(event, 16), dk_fw_input_key_down(PU_FUNC)) ==
            PU_PASS)
        return phrase_stock_turn(view, event);
    ++phrase_ui_debug[4];
    dk_fw_view_invalidate(view);
    return 1;
}
uint32_t phrase_encoder_interface(uint8_t *receiver, uint8_t *event)
{
    return phrase_view_turn(receiver - 4, event);
}
void phrase_ui_tick(void)
{
    reconcile();
    uint8_t *view = visible_fx_view();
    if (view) {
        if (context(view)) {
            /* A repaint for an edit, for a link that came or went, and every 60 blocks for the time. */
            uint32_t seq = ui.status[PH_S_HEARTBEAT / 4];
            if (ui.revision != paint_revision || seq - painted >= 60 || ui.fresh != paint_fresh)
                dk_fw_view_invalidate(view);
        }
        return;
    }
    if (ui.active || dkrt_phrase_active() != last_owner)
        pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
    phrase_ui_debug[6] = 0;
}
