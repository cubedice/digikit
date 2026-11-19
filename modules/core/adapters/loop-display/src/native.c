/* The loop effect's page in the firmware's FX view (DTII 1.17), as a page adapter of core/fxview (<dk/fxview.h>). The
 * view and bitmap are borrowed for the current callback only; no firmware View survives in our state.
 *
 * The page is the loop effect's: the home (core/loop-control) knows its page code from the fx service. Its values are
 * the home's, not the kit's. */
#include <stdint.h>
#include <dk/fxview.h>
#include "model.h"
#include "dk_fw_view.h"
#include "dk_fw_input.h"
extern void loop_stock_update(uint8_t *);

static LoopUi ui;
/* Diagnostics for the device test: 0 paints, 3 keys taken, 4 turns taken, 6 the page is on top, 7 the view's page. */
uint32_t loop_ui_debug[8];
/* The home's raw values and the engine's status as the page last read them, and the page as it was last painted. */
uint32_t loop_ui_values[LP_COUNT];
uint32_t loop_ui_snapshot[LS_WORDS];
LoopPage loop_ui_page;
static uint32_t drawn_seq, drawn_revision;
static int drawn_fresh;

static uint32_t word(const uint8_t *p, uint32_t off)
{
    return *(const uint32_t *)(p + off);
}
/* Is `page` the loop effect's page? */
static int mine(uint32_t page)
{
    uint32_t own = dkrt_loop_page();
    return own && page == own;
}
static int context(uint8_t *view)
{
    uint32_t page = fxv_page(view);
    ui.active = mine(page) && view == fxv_visible_view();
    for (uint32_t i = 0; i < LP_COUNT; ++i)
        loop_ui_values[i] = dkrt_loop_get(i);
    loop_ui_debug[6] = ui.active;
    loop_ui_debug[7] = page;
    return ui.active != 0;
}
int loop_fxv_draw(uint8_t *view, uint8_t *bitmap, int header_only)
{
    if (!context(view))
        return 0;
    int fresh = (int)dkrt_loop_status(loop_ui_snapshot);
    LoopCanvas c = {bitmap, fxv_box, fxv_text};
    ld_page(loop_ui_snapshot, (uint32_t)fresh, dkrt_loop_get(LP_XFAD), &loop_ui_page);
    if (header_only) {
        ld_render_header(&loop_ui_page, &c);
        return 1;
    }
    ld_render(&loop_ui_page, &c);
    drawn_seq = loop_ui_snapshot[LS_HEARTBEAT];
    drawn_fresh = fresh;
    drawn_revision = ui.revision;
    ++loop_ui_debug[0];
    return 1;
}
int loop_fxv_key(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    if (ld_key(&ui, word(event, 12), fxv_key_flags(word(event, 16)),
               dk_fw_input_key_down(FXV_FUNC)) == LD_PASS)
        return 0;
    ++loop_ui_debug[3];
    dk_fw_view_invalidate(view);
    return 1;
}
int loop_fxv_turn(uint8_t *view, uint8_t *event)
{
    (void)context(view);
    uint32_t encoder = word(event, 12);
    if (encoder < 1 || encoder > 9 ||
        ld_turn(&ui, encoder - 1, (int32_t)word(event, 16)) == LD_PASS)
        return 0;
    ++loop_ui_debug[4];
    dk_fw_view_invalidate(view);
    return 1;
}
/* The FX view's update slot. Invoked by ViewController while it owns its live-view list lock. Walk only the drawable
 * suffix, bounded to 64 nodes; never retain a view or manager. A modal's opaque view hides the page and suppresses our
 * animation requests. */
void loop_view_update(uint8_t *view)
{
    loop_stock_update(view);
    if (!mine(fxv_page(view)))
        return;
    uint8_t *mgr = *(uint8_t **)(view + 0x2c);
    if (!mgr)
        return;
    uint8_t *node = *(uint8_t **)(mgr + 0x1c);
    for (unsigned i = 0; node && node != mgr + 0x14 && i < 64; ++i) {
        if (*(uint8_t **)(node + 8) == view) {
            uint32_t now[LS_WORDS];
            int fresh = (int)dkrt_loop_status(now);
            /* Audio runs at 1500 blocks a second: 60 blocks is a 25 Hz ceiling on animation requests. An edit and
             * a link that came or went repaint at once. */
            if (fresh != drawn_fresh || ui.revision != drawn_revision ||
                (fresh && (uint32_t)(now[LS_HEARTBEAT] - drawn_seq) >= 60))
                dk_fw_view_invalidate(view);
            return;
        }
        node = *(uint8_t **)node;
    }
}
