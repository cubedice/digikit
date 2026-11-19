/* The phrase page in the firmware's FX view (DTII 1.17), as a page adapter of core/fxview (<dk/fxview.h>). The view and
 * bitmap are borrowed for the current callback only; no firmware View survives in our state.
 *
 * The page is the phrase effect's page in the FX view, whichever way the effect is routed: the callbacks act only
 * while the page the view shows is the owner's (dkrt_phrase_active). */
#include <stdint.h>
#include <dk/fxview.h>
#include "model.h"
#include "render.h"
#include "dk_fw_view.h"
#include "dk_fw_input.h"
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
    uint8_t *top = fxv_visible_view();
    uint32_t native_page = fxv_page(view);
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
/* core/fxview calls this after the stock draw of every FX page, and with HEADER_ONLY after the help view has drawn. */
int phrase_fxv_draw(uint8_t *view, uint8_t *bitmap, int header_only)
{
    if (!context(view))
        return 0;
    PhraseCanvas c = {bitmap, fxv_box, fxv_text};
    pu_page(&ui, &phrase_ui_page);
    if (header_only) {
        pu_render_header(&phrase_ui_page, &c);
        return 1;
    }
    pu_render(&phrase_ui_page, &c);
    painted = ui.status[PH_S_HEARTBEAT / 4];
    paint_revision = ui.revision;
    paint_fresh = ui.fresh;
    ++phrase_ui_debug[0];
    return 1;
}
int phrase_fxv_key(uint8_t *view, uint8_t *event)
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
        return 0;
    ++phrase_ui_debug[3];
    dk_fw_view_invalidate(view);
    return 1;
}
int phrase_fxv_turn(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    uint32_t encoder = word(event, 12);
    phrase_ui_debug[1] = encoder;
    phrase_ui_debug[2] = word(event, 16);
    ++phrase_ui_debug[5];
    if (encoder < 1 || encoder > 9 ||
        pu_turn(&ui, encoder - 1, (int32_t)word(event, 16), dk_fw_input_key_down(PU_FUNC)) ==
            PU_PASS)
        return 0;
    ++phrase_ui_debug[4];
    dk_fw_view_invalidate(view);
    return 1;
}
void phrase_ui_tick(void)
{
    reconcile();
    uint8_t *view = fxv_visible_view();
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
