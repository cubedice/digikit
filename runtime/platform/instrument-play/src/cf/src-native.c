#include "seam.h"
/* DT2 1.17 native SRC adapter. Vtable entries and event dispatch are pinned
 * by image hashes. Untouched interfaces remain stock; no View is retained. */
#include <stdint.h>
#include <dk/platform/ui.h>
#include "src-control.h"
#include "../../../keyboard-panel/src/cf/src-clock.h"
#include "src-pages.h"
#include "dk_fw_draw.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"
#include "instances.h"
#include "src-waves.h"
#include "src-envelope.h"
extern uint32_t dkrt_instrument_type(void), dkrt_instrument_generation(void);
extern uint32_t ip_src_type(uint8_t *);
extern uint32_t ip_src_track(uint32_t);
extern uint8_t ip_src_alternate(uint32_t);
extern void ip_src_stock_draw(uint8_t *, uint8_t *);
extern void ip_src_stock_update(uint8_t *);
extern uint32_t ip_src_stock_key(uint8_t *, uint8_t *);
extern uint32_t ip_src_stock_turn(uint8_t *, uint8_t *);
extern uint32_t ip_src_stock_parameter(uint8_t *, uint32_t);
extern uint32_t ip_src_stock_event(uint8_t *, uint8_t *);
extern void ip_src_stock_attach(uint8_t *, uint8_t *), ip_src_stock_detach(uint8_t *, uint8_t *);
extern int32_t ip_src_value(uint8_t *, uint32_t, uint8_t *);
extern void ip_src_tune_format(int32_t, char *), ip_src_level_format(int32_t, char *);
extern void ip_src_amp_adsr(const uint8_t *, uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t);
extern void ip_src_focus_touch(uint8_t *, uint32_t);
extern uint8_t ip_src_focus_active(uint8_t *, uint32_t);
extern const uint32_t ip_src_label_font;
extern dk_ui *dk_ui_core_handle(void);
/* A toast on the core UI (the handle core/ui owns, as control.c takes it). */
static void toast(const char *text)
{
    dk_ui_toast(dk_ui_core_handle(), text);
}
/* Sequencer UI state (finding 347): SRC view +0x6c, as KeyboardView +0x98. */
extern uint8_t ip_panel_trig_held(uint32_t), ip_panel_live_record(uint32_t);
/* Stock held-trig editor marks the gesture as an edit before touching locks.
 * Without this, releasing a short-held trig may toggle it and erase its locks. */
extern void ip_lock_mark_edited(uint32_t, uint32_t);
/* SoundParameterSet lock calls on the current track ("this" is unused). */
extern int32_t ip_lock_get(uint32_t, uint32_t, uint32_t);
extern uint8_t ip_lock_set(uint32_t, uint32_t, uint32_t, uint32_t),
    ip_lock_clear(uint32_t, uint32_t, uint32_t);
/* adda.l #state,a0 before the per-track step store at +0x15b (literal at +2). */
extern const uint8_t ip_seq_step[];
extern int rtm_src(uint8_t *, unsigned, uint8_t *) __attribute__((weak));
static uint32_t owner_type, owner_generation, published, last_draw;
static uint32_t saved_generation, saved_page, saved_valid, saved_track;
static uint32_t restore_pending, restore_page, was_owned;
static uint32_t live_control = CS_NONE, live_value;
static uint32_t live_track = UINT32_MAX, live_token;
static CvDisplay display;
static uint32_t shown_fault;
typedef struct {
    uint8_t *event;
    uint32_t token;
} Captured;
static Captured captured[64];
static uint32_t cursor;
volatile uint32_t ip_src_debug[12];
/* Fault headings shown as a toast by the draw (LINK LOST, VOICE ERR, ...). */
volatile uint32_t ip_src_fault_toasts;
volatile uint32_t ip_src_refresh[8];
static int trig_held(const uint8_t *view);
static uint32_t lock_steps(uint8_t *view, uint32_t steps[4]);
static uint32_t word(const uint8_t *p, uint32_t offset)
{
    return *(const uint32_t *)(p + offset);
}
static uint8_t *pointer(const uint8_t *p, uint32_t offset)
{
    return (uint8_t *)(uintptr_t)word(p, offset);
}
/* The page with the native TUNE and LEV cells. */
static int native_cells(uint32_t page)
{
    return page == CS_DETAIL;
}
static int32_t lock_get(void *ctx, uint32_t code, uint32_t step)
{
    (void)ctx;
    return ip_lock_get(0, code, step);
}
static int lock_set(void *ctx, uint32_t code, uint32_t step, uint32_t value)
{
    (void)ctx;
    return ip_lock_set(0, code, value, step);
}
static int lock_remove(void *ctx, uint32_t code, uint32_t step)
{
    (void)ctx;
    return ip_lock_clear(0, code, step);
}
static const IlpStorage lock_store = {0, lock_get, lock_set, lock_remove};
static void lock_snapshot(uint32_t step, IlpSnapshot *snapshot)
{
    static const uint8_t codes[ILP_LANES] = {0xcb, 0xcc, 0xce, 0xcf, 0xd0};
    snapshot->count = 0;
    for (uint32_t i = 0; i < ILP_LANES; ++i) {
        uint32_t id, value;
        if (ilp_unpack(ip_lock_get(0, codes[i], step), &id, &value)) {
            snapshot->pair[snapshot->count].id = (uint16_t)id;
            snapshot->pair[snapshot->count].value = (uint16_t)value;
            ++snapshot->count;
        }
    }
}
static uint32_t context(uint8_t *view)
{
    uint32_t type = dkrt_instrument_type(), gen = dkrt_instrument_generation();
    if (owner_type && (type != owner_type || gen != owner_generation)) {
        mf_src_unregister(owner_type, owner_generation);
        owner_type = 0;
    }
    uint32_t track = ip_src_track(word(view, 0x74)), page = word(view, 0x90),
             pages = CS_SHARED_PAGES;
    /* The SRC page's track is the UI selection for menus, presets and focus.
     * Sequencer byte +0x268 (ip_src_alternate) is not another context: it is
     * the held-trig "edited" mark that the first lock edit sets until the
     * trig is released. The page stays CANOPY's while a trig is held. */
    if (track < 16)
        instrument_select_track(track);
    int owns = type >= 7 && type < 32 && gen && track < 16 && instrument_track_owned(track) &&
               ip_src_type(view) == type;
    if (owns) {
        if (restore_pending) {
            page = restore_page;
            *(uint32_t *)(view + 0x90) = page;
        } else if (!was_owned && saved_valid && saved_generation == gen) {
            page = saved_page;
            *(uint32_t *)(view + 0x90) = page;
        }
        if (page >= pages) {
            page = 0;
            *(uint32_t *)(view + 0x90) = page;
        }
        MfSrcCapability cap = {MF_SRC_ABI, sizeof(cap), pages, gen};
        if (mf_src_register(type, &cap))
            owns = 0;
        else {
            owner_type = type;
            owner_generation = gen;
        }
        saved_generation = gen;
        saved_page = page;
        saved_valid = 1;
        saved_track = track;
    }
    if (page >= pages)
        page = 0;
    was_owned = (uint32_t)owns;
    published = instrument_src_context(track, page, owns);
    ip_src_debug[0] = track;
    ip_src_debug[1] = page;
    ip_src_debug[2] = published;
    return published;
}
uint32_t ip_src_event(uint8_t *controller, uint8_t *event)
{
    ++ip_src_debug[7];
    ip_src_debug[8] = published;
    for (uint32_t i = 0; i < 64; ++i)
        if (captured[i].event == event)
            captured[i].event = 0;
    /* Capture the copy at dispatch, after the stock constructor/copy path.
     * ViewController closures retain this pointer. Ring exhaustion drops a
     * custom edit instead of assigning an event the destination's context. */
    captured[cursor].event = event;
    captured[cursor].token = published;
    cursor = (cursor + 1) & 63;
    return ip_src_stock_event(controller, event);
}
static uint32_t take(uint8_t *event)
{
    for (uint32_t n = 0; n < 64; ++n) {
        uint32_t i = (cursor - 1 - n) & 63;
        if (captured[i].event == event) {
            uint32_t token = captured[i].token;
            captured[i].event = 0;
            return token;
        }
    }
    return 0;
}
/* Diagrams use top-left display coordinates. The firmware primitive uses a
 * bottom-left origin; every shape remains inside the custom SRC body. */
static void diagram_box(uint8_t *b, int x, int y, int w, int h, int ink)
{
    if (w > 0 && h > 0)
        dk_fw_draw_fill_rect(b, x, 64 - y - h, x + w - 1, 63 - y, ink);
}
static void diagram_line(uint8_t *b, int x0, int y0, int x1, int y1)
{
    if (y0 == y1) {
        int lo = x0 < x1 ? x0 : x1, hi = x0 > x1 ? x0 : x1;
        diagram_box(b, lo, y0, hi - lo + 1, 1, 1);
        return;
    }
    if (x0 == x1) {
        int lo = y0 < y1 ? y0 : y1, hi = y0 > y1 ? y0 : y1;
        diagram_box(b, x0, lo, 1, hi - lo + 1, 1);
        return;
    }
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        diagram_box(b, x0, y0, 1, 1, 1);
        if (x0 == x1 && y0 == y1)
            break;
        int e = 2 * err;
        if (e > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e < dx) {
            err += dx;
            y0 += sy;
        }
    }
}
static void diagram_rect(uint8_t *b, int x, int y, int w, int h)
{
    diagram_box(b, x, y, w, 1, 1);
    diagram_box(b, x, y + h - 1, w, 1, 1);
    diagram_box(b, x, y, 1, h, 1);
    diagram_box(b, x + w - 1, y, 1, h, 1);
}
static void diagram_text(uint8_t *b, int x, int y, const char *text)
{
    dk_fw_text_draw_box(b, (uint8_t *)(uintptr_t)ip_src_label_font, x, 59 - y, 1, 0, 0, "%s", text);
}
static int native_index(const CvDisplay *d, uint32_t id)
{
    return d->raw[id] < 128 ? d->raw[id] : 127;
}
static int32_t finite_env_time(const CvDisplay *d, uint32_t control)
{
    int32_t value = native_index(d, control);
    /* The AMP graph draws maximum DEC and REL as infinite. CANOPY's
     * timbre timing is finite, including at the longest setting. */
    return value == 127 ? 0x7eff : value << 8;
}
static void shared_envelope(const uint8_t *view, uint8_t *b, const CvDisplay *d)
{
    int fm = d->family == 3;
    int32_t attack = native_index(d, fm ? CP_FM_ATTACK : CP_WT_ATTACK) << 8;
    int32_t decay = fm ? finite_env_time(d, CP_FM_DECAY) : 0;
    int32_t sustain = fm ? (d->raw[CP_FM_SUSTAIN] * 0x7f00 + 50) / 100 : 0x7f00;
    int32_t release = finite_env_time(d, fm ? CP_FM_RELEASE : CP_WT_RELEASE);
    ip_src_amp_adsr(view, b, 0, 0, attack, decay, sustain, release);
}
static void compact_wt_envelope(uint8_t *b)
{
    if (word(b, 4) != 128 || word(b, 8) != 64 || word(b, 12) != 2 || !word(b, 16))
        return;
    uint32_t *upper = (uint32_t *)pointer(b, 16) + 1;
    /* Bitmap y runs from bottom to top. The second word in each column stores screen rows 0 through 31, with the top row in bit 0.
     * Combine source columns so a thin vertical envelope edge always survives the narrower graph. */
    const uint32_t graph = UINT32_MAX << 11;
    enum { LEFT = 23, FULL_END = 124, COMPACT_END = 97, WIDTH = COMPACT_END - LEFT };
    unsigned source = LEFT, extra = 0;
    for (unsigned x = LEFT; x < COMPACT_END; ++x) {
        uint32_t column = upper[2 * source++];
        extra += FULL_END - COMPACT_END;
        if (extra >= WIDTH) {
            column |= upper[2 * source++];
            extra -= WIDTH;
        }
        upper[2 * x] = (upper[2 * x] & ~graph) | (column & graph);
    }
    for (unsigned x = COMPACT_END; x < FULL_END; ++x)
        upper[2 * x] &= ~graph;
}
static void diagram_wavetable(uint8_t *b, const CvDisplay *d)
{
    uint32_t table = d->raw[CP_WT_TABLE], position = d->raw[CP_WT_POS], depth = d->raw[CP_WT_DEPTH];
    if (table > 4)
        table = 4;
    if (position > 700)
        position = 700;
    if (depth > 100)
        depth = 100;
    uint32_t frame = position / 100, fraction = position % 100,
             next = frame < 7 ? frame + 1 : frame;
    int samples[74], peak = 1;
    for (int i = 0; i < 74; ++i) {
        samples[i] = (canopy_preview_value(table, frame, (unsigned)i) * (int)(100 - fraction) +
                      canopy_preview_value(table, next, (unsigned)i) * (int)fraction) /
                     100;
        int abs = samples[i] < 0 ? -samples[i] : samples[i];
        if (abs > peak)
            peak = abs;
    }
    /* In the stock graphic rows of the top cells, 13 to 29: the scan span on 13, the cycle on 15 to 25, the frame rail on 28. */
    int magnitude = (5 * (samples[0] < 0 ? -samples[0] : samples[0]) + peak / 2) / peak;
    int last = 20 + (samples[0] < 0 ? magnitude : -magnitude);
    for (int i = 1; i < 74; ++i) {
        magnitude = (5 * (samples[i] < 0 ? -samples[i] : samples[i]) + peak / 2) / peak;
        int y = 20 + (samples[i] < 0 ? magnitude : -magnitude);
        diagram_line(b, 48 + i, last, 49 + i, y);
        last = y;
    }
    diagram_line(b, 49, 28, 121, 28);
    for (int i = 0; i < 8; ++i) {
        int x = 49 + (72 * i + 3) / 7;
        diagram_line(b, x, 27, x, 29);
    }
    int x = 49 + (int)((72 * position + 350) / 700), radius = (int)((72 * depth + 100) / 200);
    int lo = x - radius, hi = x + radius;
    if (lo < 49)
        lo = 49;
    if (hi > 121)
        hi = 121;
    diagram_line(b, lo, 13, hi, 13);
    diagram_line(b, lo, 13, lo, 15);
    diagram_line(b, hi, 13, hi, 15);
    diagram_line(b, x, 14, x, 28);
    diagram_box(b, x - 1, 27, 3, 3, 1);
    /* The scan-shape glyph is qualitative; RAND intentionally promises no
     * future random trajectory. */
    if (d->raw[CP_WT_SHAPE] == 2)
        diagram_text(b, 33, 46, "RAND");
    else {
        static const int8_t sine[] = {0, 2, 4, 5, 5, 4, 2, 0, -2, -4, -5, -5, -4, -2, 0, 1, 0, 0};
        for (int i = 1; i < 18; ++i) {
            int a = d->raw[CP_WT_SHAPE] ? (i - 1 < 9 ? (i - 1) * 5 / 4 : (17 - (i - 1)) * 5 / 4) - 5
                                        : sine[i - 1];
            int c = d->raw[CP_WT_SHAPE] ? (i < 9 ? i * 5 / 4 : (17 - i) * 5 / 4) - 5 : sine[i];
            diagram_line(b, 24 + i, 48 - a, 25 + i, 48 - c);
        }
    }
}
static void diagram_legacy(uint8_t *b, const CvDisplay *d, int width)
{
    static const int8_t wave[] = {0,  2,  4,  6,  7,  7,  6,  5,  4,  3,  3,  3,  3,  2, 0,
                                  -2, -4, -6, -7, -7, -6, -5, -4, -3, -3, -3, -3, -2, 0};
    for (int i = 1; i < width; ++i) {
        int pa = (i - 1) * 28 / (width - 1), pc = i * 28 / (width - 1);
        int a = wave[pa], c = wave[pc];
        if (d->family) {
            a = pa < 14 ? 7 - pa : pa - 21;
            c = pc < 14 ? 7 - pc : pc - 21;
        }
        diagram_line(b, 26 + i, 48 - a, 27 + i, 48 - c);
    }
    /* No word of its own on the caption row: WAVE (A) names the source. */
}
static void compact_mix(uint8_t *b, const CvDisplay *d)
{
    diagram_text(b, 78, 40, "P");
    diagram_text(b, 93, 40, "A");
    diagram_line(b, 77, 50, 95, 50);
    diagram_line(b, 77, 48, 77, 52);
    diagram_line(b, 95, 48, 95, 52);
    int x = 77 + (18 * d->raw[CP_ARP_MIX] + 63) / 127;
    diagram_box(b, x - 1, 47, 3, 7, 1);
}
/* VOICE G shows the operator lanes selected by ALG: four 8 by 7 boxes, A B over X Y, in the stock graphic rows 40 to 56. */
static void compact_fm(uint8_t *b, const CvDisplay *d)
{
    static const char *const names[] = {"A", "B", "X", "Y"};
    for (unsigned i = 0; i < 4; ++i) {
        int x = 77 + 13 * (int)(i % 2), y = 40 + 10 * (int)(i / 2);
        diagram_rect(b, x, y, 8, 7);
        diagram_text(b, x + 3, y + 1, names[i]);
    }
    uint32_t alg = d->raw[CP_FM_ALG];
    diagram_line(b, 80, 47, 80, 49);
    if (alg == 0)
        diagram_line(b, 93, 47, 93, 49);
    else if (alg == 2)
        diagram_line(b, 93, 47, 84, 49);
    else
        diagram_line(b, 85, 43, 89, 43);
    if (alg == 3)
        diagram_line(b, 85, 53, 89, 53);
}
/* The stock placeholder of a cell without a control: a dotted square, 17 pixels a side, on the stock graphic rows (13 to 29
 * at the top, 40 to 56 at the bottom), centred on the cell. */
static void placeholder(uint8_t *b, uint32_t cell)
{
    int x = 33 + 26 * (int)(cell % 4), top = cell < 4 ? 13 : 40;
    for (int i = -7; i <= 7; i += 2) {
        diagram_box(b, x + i, top, 1, 1, 1);
        diagram_box(b, x + i, top + 16, 1, 1, 1);
    }
    for (int y = top + 1; y < top + 16; y += 2) {
        diagram_box(b, x - 8, y, 1, 1, 1);
        diagram_box(b, x + 8, y, 1, 1, 1);
    }
}
/* The cells whose body is part of a picture. Velvet and Reed have no FM ENV controls: that page is eight stock placeholders. */
static uint32_t diagram_mask(uint32_t page, const CvDisplay *d)
{
    uint32_t family = d->family;
    if (page == CS_VOICE)
        return family < 2 ? 0x70 : family == 2 ? 0x1e : 0x40;
    if (page == CS_FM_ENV)
        return family < 2 ? 0 : family == 2 ? 0x07 : 0x0f;
    return 0x40;
}
static void shared_diagram(const uint8_t *view, uint8_t *b, const CvDisplay *d)
{
    if (d->page == CS_VOICE) {
        if (d->family == 3)
            compact_fm(b, d);
        else if (d->family == 2)
            diagram_wavetable(b, d);
        else
            diagram_legacy(b, d, 72);
    } else if (d->page == CS_DETAIL) {
        compact_mix(b, d);
    } else if (d->family >= 2) {
        shared_envelope(view, b, d);
    }
}
/* SRC held: the stock help bubble names the machine in the header, by its short name, and then the sample ("ONE: 1-sine220").
 * CANOPY has no sample: its sound's source stands where the sample's name would ("GLASS", "FM STACK"), or a fault's heading
 * ("LINK LOST"). The bubble (HelpBubbleView) writes its text in its own font (+0x9c) at y 0x42 minus its height (+0xa4: it
 * slides in, 10 when it is all in) and inverts its bar; the source is written the same way beside it, as RELAY's tape
 * (core/relay-track). It starts six pixels after the colon, the stock gap ("ONE:" ends at x 34, the name starts at 40). The
 * colon is the last dark pixel of the bar's text rows (2 to 7), found once the bubble is all in. */
enum { HELP_FONT = 0x9c, HELP_HEIGHT = 0xa4, HEADER_GAP = 6, HEADER_ROWS = 0xfc };
static char header_text[16];
static uint32_t header_frame;
static int header_x;
static void header_source(char out[16], const CvDisplay *d)
{
    static const char *const sources[] = {"VELVET", "REED",  "ANALOG", "GLASS",
                                          "VOCAL",  "ORGAN", "METAL"};
    static const char *const algorithms[] = {"FM TWIN", "FM STACK", "FM SUM", "FM CHAIN"};
    const char *s = d->fault                  ? d->heading
                    : d->family == 3          ? algorithms[d->raw[CP_FM_ALG] & 3]
                    : d->family < 2           ? sources[d->family]
                    : d->raw[CP_WT_TABLE] < 5 ? sources[2 + d->raw[CP_WT_TABLE]]
                                              : "";
    uint32_t n = 0;
    while (s[n] && n + 1 < 16) {
        out[n] = s[n];
        ++n;
    }
    out[n] = 0;
}
static int header_colon(const uint8_t *b)
{
    if (word(b, 4) != 128 || word(b, 8) != 64 || word(b, 12) != 2 || !word(b, 16))
        return 0;
    /* the second word of a column holds screen rows 0 to 31, the top row in bit 0; the bar is lit, its text dark */
    const uint32_t *upper = (const uint32_t *)pointer(b, 16) + 1;
    for (int x = 127; x > 17; --x)
        if (~upper[2 * x] & HEADER_ROWS)
            return x;
    return 0;
}
/* core/fxview, after the stock help view HELP has drawn: the source of the frame the SRC page drew with SRC held, once. */
void ip_help_draw(uint8_t *help, uint8_t *bitmap)
{
    if (!header_frame) {
        /* SRC released (or another page): the next hold finds the colon again */
        header_x = 0;
        return;
    }
    header_frame = 0;
    int height = *(int32_t *)(help + HELP_HEIGHT);
    if (height <= 0)
        return;
    if (height >= 10) {
        height = 10;
        if (!header_x) {
            int colon = header_colon(bitmap);
            header_x = colon ? colon + HEADER_GAP : 0;
        }
    }
    if (!header_x || header_x > 100 || !header_text[0])
        return;
    uint8_t *font;
    __builtin_memcpy(&font, help + HELP_FONT, sizeof font);
    diagram_box(bitmap, header_x, 0, 128 - header_x, height, 0);
    dk_fw_text_draw(bitmap, font, header_x, 0x42 - height, 128 - header_x, header_text);
    diagram_box(bitmap, header_x, 0, 128 - header_x, height, -1);
}
void ip_src_draw_rotary(uint8_t *view, uint8_t *canvas, uint32_t cell, uint32_t value)
{
    /* Paint a native encoder glyph with a supplied value and the cell's native focus state. */
    uint32_t *vt = (uint32_t *)pointer(view, 0);
    uint8_t *(*access)(uint8_t *, uint32_t, int32_t) = (void *)(uintptr_t)vt[0xc4 / 4];
    uint8_t *a = access(view, 0xcc, -1);
    uint32_t *av = (uint32_t *)pointer(a, 0);
    void (*draw)(uint8_t *, uint32_t, int32_t, uint32_t, uint8_t *, uint8_t *, int32_t, int32_t) =
        (void *)(uintptr_t)av[0x60 / 4];
    draw(a, 0xcc, (int32_t)value << 8, 0, view + 0xb4 + 20 * cell, canvas,
         24 + 26 * (int32_t)(cell % 4), cell < 4 ? 34 : 7);
}
void ip_src_draw(uint8_t *view, uint8_t *canvas)
{
    uint32_t token = context(view);
    restore_pending = 0;
    int relay = IP_HAVE(rtm_src) && rtm_src(view, 0, 0);
    ip_src_stock_draw(view, canvas);
    if (relay) {
        rtm_src(view, 1, canvas);
        last_draw = instrument_src_now();
        return;
    }
    if (!token)
        return;
    char tune[32], level[32];
    uint8_t flags = 0;
    tune[0] = 0;
    level[0] = 0;
    int32_t raw = ip_src_value(view, 0xca, &flags);
    if (raw >= 0x400 && raw <= 0x7c00)
        ip_src_tune_format(raw, tune);
    raw = ip_src_value(view, 0xd1, &flags);
    if (raw >= 0 && raw <= 0x7f00)
        ip_src_level_format(raw, level);
    uint32_t steps[4], first = trig_held(view) ? lock_steps(view, steps) : 128;
    IlpSnapshot locked;
    if (first < 128) {
        /* A held trig shows what it plays: its locks, else the Sound. */
        lock_snapshot(first, &locked);
        instrument_src_display_locked(&display, instrument_src_now(), tune, level, &locked);
    } else
        instrument_src_display(&display, instrument_src_now(), tune, level);
    uint32_t page = word(view, 0x90);
    int native = native_cells(page);
    uint32_t mask = diagram_mask(page, &display);
    if (native) {
        dk_fw_draw_fill_rect(canvas, 49, 27, 123, 52, 0);
        dk_fw_draw_fill_rect(canvas, 23, 0, 100, 25, 0);
    } else
        dk_fw_draw_fill_rect(canvas, 23, 0, 123, 52, 0);
    int compact = page == CS_FM_ENV && display.family == 2;
    if (compact) {
        shared_envelope(view, canvas, &display);
        compact_wt_envelope(canvas);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        /* The firmware already painted SOUND A/H and the track column.
         * Custom cells never clear or redraw any part of those controls. */
        if (native && (i == 0 || i == 7))
            continue;
        int32_t x = 24 + 26 * (int32_t)(i % 4), y = i < 4 ? 27 : 0;
        /* A text cell's graphic is its value, as the stock NOTE and PROB: its caption stays (below). */
        int text_cell = 0;
        if (mask & (1u << i)) {
            /* This control's body is part of the diagram. */
        } else if (display.rotary & (1u << i)) {
            ip_src_draw_rotary(view, canvas, i, display.knob[i]);
        } else if (!display.label[i][0]) {
            placeholder(canvas, i);
        } else {
            /* the stock text graphic's rows: 19 to 23 at the top, 46 to 50 at the bottom */
            dk_fw_text_draw_box(canvas, (uint8_t *)(uintptr_t)ip_src_label_font, x + 9, y + 13, 1,
                                0, 0, "%s", display.value[i]);
            text_cell = 1;
        }
        /* 0x190 is the firmware's show-values state; native edit focus uses
         * its own countdown. Do not implement another SRC key/timer policy. */
        if (display.label[i][0]) {
            int values = !text_cell && (view[0x190] || ip_src_focus_active(view + 0x94, i));
            dk_fw_text_draw_box(canvas, (uint8_t *)(uintptr_t)ip_src_label_font, x + 9, y, 1, 0, 0,
                                "%s", values ? display.value[i] : display.label[i]);
        }
        if (display.locked & (1u << i)) {
            /* Stock parameter locks invert the control's value box. Shared
             * diagrams occupy that space, so invert their label instead. */
            if (mask & (1u << i))
                dk_fw_draw_fill_rect(canvas, x, y, x + 20, y + 5, -1);
            else
                dk_fw_draw_fill_rect(canvas, x - 1, y + 6, x + 21, y + 24, -1);
        }
    }
    if (!compact)
        shared_diagram(view, canvas, &display);
    header_frame = view[0x190] != 0;
    if (header_frame)
        header_source(header_text, &display);
    if (display.fault && display.fault != shown_fault) {
        ++ip_src_fault_toasts;
        toast(display.heading);
    }
    shown_fault = display.fault;
    last_draw = instrument_src_now();
    ++ip_src_debug[3];
}
void ip_src_attach(uint8_t *view, uint8_t *manager)
{
    uint32_t gen = dkrt_instrument_generation();
    ip_src_stock_attach(view, manager);
    if (saved_valid && saved_generation == gen &&
        instrument_track_owned(ip_src_track(word(view, 0x74))) &&
        ip_src_type(view) == dkrt_instrument_type()) {
        restore_page = saved_page;
        restore_pending = 1;
        *(uint32_t *)(view + 0x90) = saved_page;
    } else
        restore_pending = 0;
}
void ip_src_detach(uint8_t *view, uint8_t *manager)
{
    (void)context(view);
    if (IP_HAVE(rtm_src))
        rtm_src(view, 4, 0);
    ip_src_stock_detach(view, manager);
    was_owned = 0;
    published = 0;
    (void)instrument_src_context(ip_src_debug[0], ip_src_debug[1], 0);
}
void ip_src_update(uint8_t *view)
{
    ip_src_stock_update(view);
    uint32_t seq = word(view, 0x6c);
    if (!seq || !ip_panel_live_record(seq))
        live_control = CS_NONE;
    if (context(view) && instrument_src_now() - last_draw >= 40)
        dk_fw_view_invalidate(view);
}
void instrument_src_tick(void)
{
    ++ip_src_refresh[0];
    /* UI-task ownership: resolve the current visible view afresh. The stock
     * SRC update callback alone does not schedule idle animation. Never keep
     * a view pointer across ticks or animate underneath an opaque modal. */
    uint8_t *brain = dk_fw_view_brain_instance();
    uint8_t *manager = brain ? dk_fw_view_brain_view_manager(brain) : 0;
    ip_src_refresh[1] = (uint32_t)(uintptr_t)brain;
    ip_src_refresh[2] = (uint32_t)(uintptr_t)manager;
    if (manager) {
        /* Use the firmware's resolved first drawable node. The per-view
         * visibility flag is a virtual policy, not a raw opaque boolean. */
        uint8_t *sentinel = manager + 0x14, *node = pointer(manager, 0x1c);
        ip_src_refresh[3] = (uint32_t)(uintptr_t)node;
        for (uint32_t n = 0; n < 32 && node && node != sentinel; ++n, node = pointer(node, 0)) {
            uint8_t *view = pointer(node, 8);
            if (!view || view[0x30])
                continue;
            uint8_t *vtable = pointer(view, 0);
            /* the vtable holds the entry of the image: in the service build the forwarder of seam.s */
            if (vtable && word(vtable, 16) == (uint32_t)IP_DATA(ip_src_draw)) {
                ++ip_src_refresh[4];
                uint32_t now = instrument_src_now();
                ip_src_refresh[5] = now;
                ip_src_refresh[6] = last_draw;
                int relay = IP_HAVE(rtm_src) ? rtm_src(view, 5, 0) : 0;
                if (relay) {
                    if (relay == 2 && now - last_draw >= 24) {
                        last_draw = now;
                        dk_fw_view_invalidate(view);
                    }
                    return;
                }
                if (context(view)) {
                    instrument_src_poll(now);
                    if (now - last_draw >= 40) {
                        ++ip_src_refresh[7];
                        dk_fw_view_invalidate(view);
                    }
                }
                return;
            }
        }
    }
    was_owned = 0;
    published = 0;
    (void)instrument_src_context(ip_src_debug[0], ip_src_debug[1], 0);
}
/* Packed trig locks. Grid recording: the held trigs (128-bit mask at
 * +0x258, below the pattern length). Live recording: the track's current
 * step. Returns the first step, or 128 when neither applies. */
static int trig_held(const uint8_t *view)
{
    uint32_t seq = word(view, 0x6c);
    return seq && ip_panel_trig_held(seq);
}
static uint32_t lock_steps(uint8_t *view, uint32_t steps[4])
{
    uint32_t seq = word(view, 0x6c);
    steps[0] = steps[1] = steps[2] = steps[3] = 0;
    if (!seq)
        return 128;
    if (trig_held(view)) {
        for (uint32_t i = 0; i < 4; ++i)
            steps[i] = word((const uint8_t *)(uintptr_t)seq, 0x258 + 4 * i);
        for (uint32_t s = 0; s < 128; ++s)
            if (steps[s >> 5] >> (s & 31) & 1u)
                return s;
        return 128;
    }
    uint32_t track = ip_src_debug[0];
    if (track >= 16 || !ip_panel_live_record(seq))
        return 128;
    const uint8_t *state = (const uint8_t *)(uintptr_t)*(const uint32_t *)(ip_seq_step + 2);
    uint32_t step = state[0x15b + track];
    if (step >= 128)
        return 128;
    steps[step >> 5] = 1u << (step & 31);
    return step;
}
/* A live-recording sweep continues from its last value across steps. */
static uint32_t lock_turn(uint8_t *view, uint8_t *event, uint32_t token, uint32_t encoder,
                          const uint32_t steps[4], uint32_t first)
{
    uint32_t control = 0, id = 0, base = 0, next = 0, current = 0;
    IlpSnapshot snapshot;
    lock_snapshot(first, &snapshot);
    int result = instrument_src_lock_target(encoder, &snapshot, &control, &id, &base);
    if (result == CS_NATIVE)
        return ip_src_stock_turn(view, event);
    int held = trig_held(view);
    if (result == CS_EDIT) {
        if (held)
            ip_lock_mark_edited(word(view, 0x6c), 1);
        int found = ilp_store_read(&lock_store, first, id, &current);
        if (found < 0) {
            toast("LOCK SLOT IN USE");
            return 1;
        }
        if (!found)
            current = base;
        if (!held && live_control == id && live_track == ip_src_debug[0] && live_token == token)
            current = live_value;
        result = instrument_src_lock_step(token, encoder, (int32_t)word(event, 16),
                                          instrument_src_now(), &snapshot, current, &next);
    } else if (result == CS_READONLY)
        toast("NOT LOCKABLE");
    if (result == CS_EDIT) {
        int written = ilp_store_edit_steps(&lock_store, steps, id, next, 0);
        if (written != 1)
            toast(written == ILP_ROLLBACK_FAILED                       ? "LOCK RESTORE FAILED"
                  : written == ILP_FULL || written == ILP_STORAGE_FULL ? "LOCKS FULL"
                                                                       : "LOCK SLOT IN USE");
        else {
            live_control = held ? CS_NONE : id;
            live_value = next;
            live_track = ip_src_debug[0];
            live_token = token;
            char feedback[32];
            instrument_src_lock_feedback(feedback, encoder, &snapshot, control, next);
            if (feedback[0])
                toast(feedback);
        }
        ip_src_focus_touch(view + 0x94, encoder);
        ++ip_src_debug[5];
    }
    dk_fw_view_invalidate(view);
    return 1;
}
/* Holding a trig and pressing a CANOPY encoder clears that control's lock. */
static int lock_clear(uint8_t *view, uint32_t encoder, const uint32_t steps[4])
{
    uint32_t control, id, base;
    uint32_t first = 128;
    for (uint32_t s = 0; s < 128; ++s)
        if (steps[s >> 5] >> (s & 31) & 1u) {
            first = s;
            break;
        }
    if (first == 128)
        return 0;
    IlpSnapshot snapshot;
    lock_snapshot(first, &snapshot);
    if (instrument_src_lock_target(encoder, &snapshot, &control, &id, &base) != CS_EDIT)
        return 0;
    ip_lock_mark_edited(word(view, 0x6c), 1);
    int result = ilp_store_edit_steps(&lock_store, steps, id, 0, 1);
    if (result < 0)
        toast(result == ILP_ROLLBACK_FAILED ? "LOCK RESTORE FAILED" : "LOCK SLOT IN USE");
    return 1;
}
uint32_t ip_src_key(uint8_t *view, uint8_t *event)
{
    if (IP_HAVE(rtm_src) && rtm_src(view, 2, event))
        return 1;
    uint32_t token = context(view), key = word(event, 12), page = word(view, 0x90), steps[4];
    int held = token && key >= 41 && key <= 48 && trig_held(view);
    if (held && lock_steps(view, steps) < 128 && lock_clear(view, key - 41, steps)) {
        dk_fw_view_invalidate(view);
        return 1;
    }
    published = 0;
    (void)instrument_src_context(ip_src_debug[0], page, 0);
    if (token && key >= 41 && key <= 48 && (!native_cells(page) || (key != 48 && key != 41)))
        return 1;
    return ip_src_stock_key(view, event);
}
uint32_t ip_src_turn(uint8_t *view, uint8_t *event)
{
    uint32_t captured_token = take(event), token = context(view), encoder = word(event, 12);
    if (IP_HAVE(rtm_src) && rtm_src(view, 3, event))
        return 1;
    ++ip_src_debug[4];
    ip_src_debug[9] = captured_token;
    ip_src_debug[10] = encoder;
    if (!token) {
        if (captured_token) {
            ++ip_src_debug[6];
            return 1;
        }
        return ip_src_stock_turn(view, event);
    }
    if (encoder < 1 || encoder > 8)
        return ip_src_stock_turn(view, event);
    uint32_t steps[4], first = lock_steps(view, steps);
    if (first < 128)
        return lock_turn(view, event, captured_token, encoder - 1, steps, first);
    live_control = CS_NONE;
    int result = instrument_src_encoder(captured_token, encoder - 1, (int32_t)word(event, 16),
                                        instrument_src_now());
    ip_src_debug[11] = (uint32_t)result;
    if (result == CS_NATIVE)
        return ip_src_stock_turn(view, event);
    if (result != CS_STALE) {
        /* The turned cell shows its value in its caption while the focus countdown runs, as the stock cells do; the
         * header names a choice that changed (cv_feedback), as the track keyboard's ARP and KEYS pages do. */
        ip_src_focus_touch(view + 0x94, encoder - 1);
        char feedback[32];
        instrument_src_feedback(feedback, encoder - 1);
        if (display.fault)
            toast(display.heading);
        else if (result == CS_EDIT && feedback[0])
            toast(feedback);
    }
    if (result == CS_EDIT)
        ++ip_src_debug[5];
    if (result == CS_STALE)
        ++ip_src_debug[6];
    dk_fw_view_invalidate(view);
    return 1;
}
uint32_t ip_src_encoder_interface(uint8_t *receiver, uint8_t *event)
{
    return ip_src_turn(receiver - 4, event);
}
uint32_t ip_src_parameter(uint8_t *view, uint32_t encoder)
{
    /* V2 has no legacy DK_PARAM descriptor. Keep the native editing path,
     * including modifier handling and Sound persistence, for SOUND A/H. */
    if (context(view)) {
        if (encoder == 8)
            return 10;
        if (native_cells(word(view, 0x90))) {
            if (encoder == 0)
                return 0xca;
            if (encoder == 7)
                return 0xd1;
        }
        if (encoder < 8)
            return 0; /* custom cell: no hidden Sound parameter */
    }
    return ip_src_stock_parameter(view, encoder);
}
