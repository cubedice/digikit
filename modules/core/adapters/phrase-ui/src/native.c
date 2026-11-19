/* The phrase page in the firmware's FX view (DTII 1.17), as a page adapter of core/fxview (<dk/fxview.h>). The view and
 * bitmap are borrowed for the current callback only; no firmware View survives in our state.
 *
 * The page is the phrase effect's page in the FX view, whichever way the effect is routed: the callbacks act only
 * while the page the view shows is the owner's (dkrt_phrase_active).
 *
 * The TRIG keys and their LEDs are not the FX view's: the firmware's track and grid handlers take the keys before any view
 * sees them. Two hooks before firmware functions give them to the page while G holds the selection of the tracks: the key
 * dispatcher (phrase_key_dispatch) and the LED setter (phrase_led_set). At any other time both go straight on to the
 * firmware. */
#include <stdint.h>
#include <dk/fxview.h>
#include "model.h"
#include "render.h"
#include "dk_fw_view.h"
#include "dk_fw_input.h"
extern uint32_t phrase_stock_key_dispatch(uint8_t *, uint8_t *);
extern void phrase_stock_led(uint8_t *, uint32_t, uint32_t, uint32_t);
/* The loop effect's home (core/loop-control) is optional: in an image without it these are 0 and the page has no loop. */
#pragma weak dkrt_loop_get
#pragma weak dkrt_loop_xfad
#pragma weak dkrt_loop_arm
#pragma weak dkrt_loop_drop
#pragma weak dkrt_loop_status
static PhraseUi ui;
static PhraseLoop home;
static void reach_home(void)
{
    if (!dkrt_loop_status || !dkrt_loop_get || !dkrt_loop_xfad || !dkrt_loop_arm ||
        !dkrt_loop_drop) {
        ui.loop = 0;
        return;
    }
    home.get = dkrt_loop_get;
    home.xfad = dkrt_loop_xfad;
    home.arm = dkrt_loop_arm;
    home.drop = dkrt_loop_drop;
    home.status = dkrt_loop_status;
    ui.loop = &home;
}
/* Native acceptance diagnostics: 0 draws, 1 and 2 the encoder and the ticks of the last turn the view got, 3 keys taken,
 * 4 turns taken, 5 turns the view got, 6 the page is on top, 7 the view's page, 8 G holds the selection of the tracks,
 * 9 TRIG events the selection took at the key dispatcher, 10 TRIG LED values it gave. */
uint32_t phrase_ui_debug[11];
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
    reach_home();
    pu_loop_status(&ui);
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
static uint32_t model_key(uint8_t *event)
{
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
    return pu_key(&ui, key, pu_native_flags(flags));
}
int phrase_fxv_key(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    if (model_key(event) == PU_PASS)
        return 0;
    ++phrase_ui_debug[3];
    phrase_ui_debug[8] = pu_selecting(&ui);
    dk_fw_view_invalidate(view);
    return 1;
}
/* Before the firmware's key dispatcher (cf.page.hook.phrase_key_dispatch, cdecl(manager, event)): a TRIG key goes to the
 * page while G holds the selection on the page that is on top, so that it neither plays a note, nor enters a step, nor
 * selects a track or a pattern. The release of a TRIG key the page took is the page's, also after G or the view went.
 * Every other event goes on to the firmware unchanged. */
uint32_t phrase_key_dispatch(uint8_t *manager, uint8_t *event)
{
    uint32_t key = word(event, 12);
    if (key >= PU_TRIG && key < PU_TRIG + PH_TRACKS) {
        uint32_t bit = 1u << (key & 31),
                 taken = (ui.owned[key >> 5] | ui.cancelled[key >> 5]) & bit;
        if (ui.select || taken) {
            uint8_t *view = fxv_visible_view();
            if (view)
                (void)context(view);
            else
                pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
            if ((pu_selecting(&ui) || taken) && model_key(event) != PU_PASS) {
                ++phrase_ui_debug[9];
                if (view)
                    dk_fw_view_invalidate(view);
                return 1;
            }
        }
    }
    return phrase_stock_key_dispatch(manager, event);
}
/* Before the firmware's LED setter (cf.page.hook.phrase_led_set, cdecl(manager, logical, layer, value)): while G holds the
 * selection on the page that is on top, the TRIG LEDs show the tracks the effect hears. Every other LED, and every LED at
 * any other time, is the firmware's. */
void phrase_led_set(uint8_t *manager, uint32_t logical, uint32_t layer, uint32_t value)
{
    if (ui.select && logical >= PU_LED_TRIG && logical < PU_LED_TRIG + PH_TRACKS) {
        uint8_t *view = fxv_visible_view();
        if (!view || fxv_page(view) != dkrt_phrase_active())
            pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
        if (pu_selecting(&ui)) {
            value = pu_led(&ui, logical, value);
            /* layer 2: a claimed value (layer 1 is the firmware's forced off); the next LED frame clears every claim */
            layer = 2;
            ++phrase_ui_debug[10];
        }
    }
    phrase_stock_led(manager, logical, layer, value);
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
    /* INPT follows the loop also while the page is not shown */
    reach_home();
    pu_loop(&ui);
    uint8_t *view = fxv_visible_view();
    if (view) {
        if (context(view)) {
            /* A repaint for an edit, for a link that came or went, and every 60 blocks for the time. */
            uint32_t seq = ui.status[PH_S_HEARTBEAT / 4];
            if (ui.revision != paint_revision || seq - painted >= 60 || ui.fresh != paint_fresh)
                dk_fw_view_invalidate(view);
        }
        phrase_ui_debug[8] = pu_selecting(&ui);
        return;
    }
    if (ui.active || dkrt_phrase_active() != last_owner)
        pu_context(&ui, 0, dkrt_phrase_active() ? ui.generation : 0);
    phrase_ui_debug[6] = 0;
    phrase_ui_debug[8] = 0;
}
