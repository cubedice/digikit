#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../../../runtime/platform/instrument-play/src/cf/shared-notes.h"
#include "../../../runtime/platform/instrument-play/src/cf/src-native.c"
static CsState state;
static uint32_t test_held, test_marked, seq_object[0xa0];
static int32_t test_locks[128][ILP_LANES];
static int lock_index(uint32_t code)
{
    static const uint8_t codes[ILP_LANES] = {0xcb, 0xcc, 0xce, 0xcf, 0xd0};
    for (int i = 0; i < ILP_LANES; ++i)
        if (code == codes[i])
            return i;
    assert(0);
    return 0;
}
const uint8_t ip_seq_step[6] = {0};
uint8_t ip_panel_trig_held(uint32_t seq)
{
    (void)seq;
    return (uint8_t)test_held;
}
uint8_t ip_panel_live_record(uint32_t seq)
{
    (void)seq;
    return 0;
}
void ip_lock_mark_edited(uint32_t seq, uint32_t edited)
{
    assert(seq == (uint32_t)(uintptr_t)seq_object && edited == 1);
    ++test_marked;
}
int32_t ip_lock_get(uint32_t a, uint32_t b, uint32_t c)
{
    (void)a;
    assert(c < 128);
    return test_locks[c][lock_index(b)];
}
uint8_t ip_lock_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    (void)a;
    assert(d < 128);
    test_locks[d][lock_index(b)] = (int32_t)c;
    return 1;
}
uint8_t ip_lock_clear(uint32_t a, uint32_t b, uint32_t c)
{
    (void)a;
    assert(c < 128);
    test_locks[c][lock_index(b)] = -1;
    return 1;
}
int instrument_src_lock_target(uint32_t e, const IlpSnapshot *s, uint32_t *c, uint32_t *id,
                               uint32_t *b)
{
    (void)s;
    assert(e == 1);
    *c = CP_COLOR;
    *id = 2;
    *b = state.value[CP_COLOR];
    return CS_EDIT;
}
int instrument_src_lock_step(uint32_t t, uint32_t e, int32_t d, uint32_t now, const IlpSnapshot *s,
                             uint32_t v, uint32_t *n)
{
    (void)s;
    return cs_lock_step(&state, t, e, d, now, v, 0, n);
}
void instrument_src_lock_feedback(char out[32], uint32_t e, const IlpSnapshot *s, uint32_t c,
                                  uint32_t v)
{
    (void)e;
    (void)s;
    (void)c;
    (void)v;
    out[0] = 0;
}
void instrument_src_display(CvDisplay *, uint32_t, const char *, const char *);
void instrument_src_display_locked(CvDisplay *d, uint32_t t, const char *tune, const char *level,
                                   const IlpSnapshot *s)
{
    instrument_src_display(d, t, tune, level);
    d->locked = cv_lock_mask(&state, s);
}
uint8_t *mf_src_alloc(uint32_t bytes)
{
    (void)bytes;
    assert(0);
    return 0;
}
void mf_src_free(uint8_t *p)
{
    (void)p;
    assert(0);
}
static uint32_t selected, alternate, now = 100, type_id = 9, generation = 1, stock_turns,
                                     stock_keys, invalidations, polls;
static uint32_t manager[16], node[4], vtable[60], modal[24], modal_node[4];
static uint32_t focus_mask, texts, graphs, toasts;
static char last_toast[32];
/* ip_help_draw: the header's boxes and text, recorded instead of painted */
static int header_mode;
static uint32_t header_boxes;
static int32_t header_box_x, header_box_rows;
static char drawn_header[32];
static int32_t drawn_x, drawn_y, drawn_maxw;
static uint8_t *drawn_font;
static char label_text[8][32];
static char body_text[8][32];
static char arp_caption[32];
static uint32_t real_display, graph_mask, graph_value[8];
static uint32_t amp_draws, env_caption, detail_caption;
static int32_t amp_args[6];
const uint32_t ip_src_label_font = 123;
void ip_src_focus_touch(uint8_t *tracker, uint32_t index)
{
    (void)tracker;
    focus_mask |= 1u << index;
}
uint8_t ip_src_focus_active(uint8_t *tracker, uint32_t index)
{
    (void)tracker;
    return !!(focus_mask & (1u << index));
}
int32_t dk_ui_toast(dk_ui *ui, const char *text)
{
    assert(ui && text[0]);
    ++toasts;
    snprintf(last_toast, sizeof last_toast, "%s", text);
    return 0;
}
struct dk_ui {
    int unused;
};
static struct dk_ui host_ui;
dk_ui *dk_ui_core_handle(void)
{
    return &host_ui;
}
void instrument_src_feedback(char out[32], uint32_t encoder)
{
    cv_feedback(out, &state, encoder);
}
uint8_t *dk_fw_view_brain_instance(void)
{
    return (uint8_t *)manager;
}
uint8_t *dk_fw_view_brain_view_manager(uint8_t *brain)
{
    return brain;
}
volatile uint32_t ip_track;
int instrument_track_owned(uint32_t t)
{
    return t == ip_track;
}
void instrument_select_track(uint32_t t)
{
    (void)t;
}
uint32_t dkrt_instrument_type(void)
{
    return type_id;
}
uint32_t dkrt_instrument_generation(void)
{
    return generation;
}
uint32_t ip_src_type(uint8_t *v)
{
    (void)v;
    return selected == ip_track ? type_id : 0;
}
uint32_t ip_src_track(uint32_t p)
{
    (void)p;
    return selected;
}
uint8_t ip_src_alternate(uint32_t p)
{
    (void)p;
    return (uint8_t)alternate;
}
uint32_t instrument_src_now(void)
{
    return now;
}
void instrument_src_poll(uint32_t time)
{
    assert(time == now);
    ++polls;
}
uint32_t instrument_src_context(uint32_t track, uint32_t page, int visible)
{
    return cs_context(&state, track, generation, visible, page, KB_PLAY, 0, 0, 0);
}
int instrument_src_encoder(uint32_t token, uint32_t encoder, int32_t turns, uint32_t time)
{
    assert(time == now);
    uint32_t id;
    return cs_edit(&state, token, encoder, turns, time, &id);
}
void instrument_src_display(CvDisplay *d, uint32_t time, const char *t, const char *l)
{
    assert(time == now);
    assert(!strcmp(t, "-60.00") && !strcmp(l, "127"));
    memset(d, 0, sizeof(*d));
    if (real_display) {
        IsStatus status = {0};
        status.generation = state.generation;
        status.track = state.track;
        status.mode = state.mode;
        status.fast_valid = 1;
        status.fast_at = time;
        cv_build(d, &state, &status, time, t, l, 0);
        return;
    }
    for (unsigned i = 0; i < 8; ++i) {
        strcpy(d->label[i], "COLR");
        strcpy(d->value[i], "64");
    }
    d->rotary = 1u << 3;
    d->knob[3] = 64;
    /* the source family picks the page's pictures (Velvet's FM ENV has none) */
    d->family = (uint8_t)((state.metadata >> 4) & 3);
}
int32_t ip_src_value(uint8_t *v, uint32_t code, uint8_t *flags)
{
    (void)v;
    *flags = 0;
    return code == 0xca ? 0x400 : 0x7f00;
}
void ip_src_tune_format(int32_t raw, char *out)
{
    assert(raw == 0x400);
    strcpy(out, "-60.00");
}
void ip_src_level_format(int32_t raw, char *out)
{
    assert(raw == 0x7f00);
    strcpy(out, "127");
}
void ip_src_amp_adsr(const uint8_t *view, uint8_t *canvas, int32_t column, int32_t row,
                     int32_t attack, int32_t decay, int32_t sustain, int32_t release)
{
    assert(view && canvas);
    ++amp_draws;
    const int32_t args[] = {column, row, attack, decay, sustain, release};
    memcpy(amp_args, args, sizeof amp_args);
}
void ip_src_stock_attach(uint8_t *v, uint8_t *m)
{
    (void)m;
    *(uint32_t *)(v + 0x90) = 0;
}
void ip_src_stock_detach(uint8_t *v, uint8_t *m)
{
    (void)v;
    (void)m;
}
void ip_src_stock_draw(uint8_t *v, uint8_t *c)
{
    (void)v;
    (void)c;
}
void ip_src_stock_update(uint8_t *v)
{
    (void)v;
}
uint32_t ip_src_stock_key(uint8_t *v, uint8_t *e)
{
    (void)v;
    (void)e;
    ++stock_keys;
    return 7;
}
uint32_t ip_src_stock_turn(uint8_t *v, uint8_t *e)
{
    (void)v;
    (void)e;
    ++stock_turns;
    return 7;
}
uint32_t ip_src_stock_parameter(uint8_t *v, uint32_t e)
{
    (void)v;
    return 1000 + e;
}
uint32_t ip_src_stock_event(uint8_t *controller, uint8_t *event)
{
    (void)controller;
    (void)event;
    return 7;
}
static void event(uint8_t *e, uint32_t encoder, int32_t delta, uint32_t stamp, uint32_t fine)
{
    uint32_t *w = (uint32_t *)e;
    w[2] = stamp;
    w[3] = encoder;
    w[4] = (uint32_t)delta;
    e[20] = (uint8_t)fine;
    assert(ip_src_event(0, e) == 7);
}
void dk_fw_view_invalidate(uint8_t *v)
{
    (void)v;
    ++invalidations;
}
static void graph(uint8_t *a, uint32_t code, int32_t value, uint32_t alt, uint8_t *state_ptr,
                  uint8_t *canvas, int32_t x, int32_t y)
{
    (void)a;
    (void)canvas;
    assert(code == 0xcc && !alt && state_ptr);
    if (real_display) {
        assert(x >= 24 && x <= 102 && (x - 24) % 26 == 0 && (y == 7 || y == 34));
        assert(value >= 0 && value <= (127 << 8));
        uint32_t i = (uint32_t)(x - 24) / 26 + (y == 7 ? 4 : 0);
        graph_mask |= 1u << i;
        graph_value[i] = (uint32_t)value >> 8;
    } else
        assert(value == (64 << 8) && x == 102 && y == 34);
    ++graphs;
}
static uint32_t accessor_vtable[25], accessor[1];
static uint8_t *access(uint8_t *v, uint32_t code, int32_t track)
{
    (void)v;
    assert(code == 0xcc && track == -1);
    return (uint8_t *)accessor;
}
int32_t dk_fw_text_draw_box(uint8_t *p, uint8_t *font, int32_t x, int32_t y, int32_t align,
                            int32_t arg6, const char *prefix, const char *fmt, ...)
{
    (void)p;
    assert((uintptr_t)font == 123 && align == 1 && !arg6 && !prefix && !strcmp(fmt, "%s"));
    assert(x >= 24 && x <= 120 && y >= 0 && y <= 52);
    ++texts;
    va_list ap;
    va_start(ap, fmt);
    const char *s = va_arg(ap, const char *);
    assert(x != 34 || y != 47 || strcmp(s, "VOICE"));
    env_caption += !strcmp(s, "FM ENV") || !strcmp(s, "POS FM");
    detail_caption += !strcmp(s, "DETAIL");
    if (x == 98 && y == 0)
        strcpy(arp_caption, s);
    if (x >= 33 && x <= 111 && (x - 33) % 26 == 0) {
        if (y == 0 || y == 27)
            strcpy(label_text[(x - 33) / 26 + (y == 0 ? 4 : 0)], s);
        /* a text value on the stock text graphic's rows (19 to 23, 46 to 50) */
        if (y == 13 || y == 40)
            strcpy(body_text[(x - 33) / 26 + (y == 13 ? 4 : 0)], s);
    }
    va_end(ap);
    return 0;
}
int32_t dk_fw_text_draw(uint8_t *p, uint8_t *font, int32_t x, int32_t y, int32_t maxw,
                        const char *s)
{
    assert(header_mode && p);
    drawn_font = font;
    drawn_x = x;
    drawn_y = y;
    drawn_maxw = maxw;
    snprintf(drawn_header, sizeof drawn_header, "%s", s);
    return 0;
}
void dk_fw_draw_fill_rect(uint8_t *p, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
{
    if (header_mode) {
        /* the header's bar beside the stock text: rows 0 to the bubble's height, to the right edge */
        assert(x0 > 17 && x1 == 127 && y0 >= 54 && y1 == 63 && (color == 0 || color == -1));
        header_box_x = x0;
        header_box_rows = 64 - y0;
        ++header_boxes;
        return;
    }
    assert(x0 >= 23 && x1 < 124 && y0 >= 0 && y1 < 53 && (color == -1 || color == 0 || color == 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            uint8_t *px = &p[(63 - y) * 128 + x];
            if (color < 0)
                *px ^= 1;
            else
                *px = (uint8_t)(color & 1);
        }
}
static void clear_paint(uint8_t *canvas)
{
    memset(canvas, 0xaa, 8192);
    memset(label_text, 0, sizeof(label_text));
    memset(body_text, 0, sizeof(body_text));
    memset(graph_value, 0, sizeof(graph_value));
    graph_mask = 0;
    amp_draws = env_caption = detail_caption = 0;
}
static void expect_amp(int32_t attack, int32_t decay, int32_t sustain, int32_t release,
                       uint32_t dial_mask)
{
    const int32_t expected[] = {0, 0, attack, decay, sustain, release};
    assert(amp_draws == 1 && !memcmp(amp_args, expected, sizeof expected));
    assert(graph_mask == dial_mask && !env_caption);
}
static void compact_envelope_pixels(void)
{
    static uint32_t plane[256];
    uint32_t before[256];
    uint32_t bitmap[] = {0, 128, 64, 2, (uint32_t)(uintptr_t)plane, 0};
    /* The real framebuffer stores pixels from bottom to top, MSB first. */
    for (unsigned x = 0; x < 128; ++x) {
        plane[2 * x] = 0xa0000000 | x;
        plane[2 * x + 1] = x | (1u << (11 + x % 21));
    }
    memcpy(before, plane, sizeof(before));
    compact_wt_envelope((uint8_t *)bitmap);
    for (unsigned x = 0; x < 128; ++x) {
        assert(plane[2 * x] == before[2 * x]);
        if (x < 23 || x >= 124) {
            assert(plane[2 * x + 1] == before[2 * x + 1]);
            continue;
        }
        uint32_t expected = before[2 * x + 1] & 0x7ffu;
        if (x < 97) {
            unsigned first = 23 + (x - 23) * 101 / 74, end = 23 + (x - 22) * 101 / 74;
            for (unsigned source = first; source < end; ++source)
                expected |= before[2 * source + 1] & 0xfffff800u;
        }
        assert(plane[2 * x + 1] == expected);
    }
}
static void protected_pixels(const uint8_t *canvas, int native_tune)
{
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x)
            if (y < 11 || x < 23 || x >= 124 ||
                (native_tune && ((y < 37 && x < 48) || (y >= 38 && x >= 101))))
                assert(canvas[y * 128 + x] == 0xaa);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    static uint32_t v[160], e[6];
    uint8_t canvas[8192];
    v[0] = (uint32_t)(uintptr_t)vtable;
    vtable[0xc4 / 4] = (uint32_t)(uintptr_t)access;
    accessor[0] = (uint32_t)(uintptr_t)accessor_vtable;
    accessor_vtable[0x60 / 4] = (uint32_t)(uintptr_t)graph;
    cs_init(&state);
    memset(canvas, 0xaa, sizeof(canvas));
    ip_src_draw((uint8_t *)v, canvas);
    if (!strcmp(argv[1], "shared-keyboard-queue")) {
        IsnQueue q = {0};
        MiEvent batch[8] = {0};
        isn_sync(&q, 5, 7, 23);
        for (uint32_t token = 1; token <= 6; ++token) {
            assert(isn_submit(&q, token, 59 + token, 100, 1));
            assert(isn_submit(&q, token, 59 + token, 0, 0));
        }
        assert(isn_submit(&q, 7, 66, 100, 1));
        assert(isn_submit(&q, 8, 67, 100, 1));
        assert(!isn_submit(&q, 9, 68, 100, 1));
        assert(!isn_submit(&q, 7, 67, 0, 0));
        assert(isn_submit(&q, 7, 66, 0, 0));
        assert(isn_submit(&q, 8, 67, 0, 0));
        assert(isn_take(&q, batch) == 8);
        /* USB channel 1: the route of the controllers the track keyboard forwards (shared-notes.h) */
        assert(batch[0].port == 0 && batch[0].channel == 0 && batch[0].track == 5 &&
               batch[0].epoch == 7 && batch[0].generation == 23 && batch[0].sequence == 0);
        assert(batch[0].kind == MI_NOTE_ON && batch[0].token == 1 && batch[0].note == 60);
        assert(batch[1].kind == MI_NOTE_OFF && batch[1].token == 1 && batch[1].note == 60);
        assert(isn_take(&q, batch) == 8);
        assert(batch[7].kind == MI_NOTE_OFF && batch[7].token == 8 && batch[7].note == 67);
        assert(!isn_submit(&q, 6, 68, 100, 1));
        assert(isn_submit(&q, 9, 68, 100, 1));
        isn_sync(&q, 5, 7, 24);
        assert(!isn_take(&q, batch));
        assert(!isn_submit(&q, 9, 68, 0, 0));
        assert(isn_submit(&q, 9, 68, 100, 1));
        isn_sync(&q, 6, 7, 24);
        assert(!isn_take(&q, batch));
        IsnSnapshot locked = {.valid = 1, .count = 1, .lanes = 3, .locks = {(2 << 10) | 84}};
        const IsnSnapshot live = {.valid = 1, .lanes = 1};
        assert(isn_submit_locked(&q, 10, 60, 100, 1, 0, &locked));
        assert(isn_submit_locked(&q, 11, 64, 100, 1, 1, &locked));
        assert(isn_submit_voice(&q, 10, 60, 0, 0, 0));
        assert(isn_submit_locked(&q, 12, 67, 100, 1, 0, &live));
        locked.locks[0] = 0;
        assert(isn_take(&q, batch) == 2);
        assert(batch[0].lock_valid && batch[0].lock_count == 1 && batch[0].shared_lanes == 3 &&
               batch[0].locks[0] == ((2 << 10) | 84));
        assert(batch[1].kind == MI_NOTE_ON && !batch[1].lock_valid &&
               (batch[1].token & ISN_ARP_TOKEN));
        assert(isn_take(&q, batch) == 1 && batch[0].kind == MI_NOTE_OFF && !batch[0].lock_valid);
        assert(isn_take(&q, batch) == 1 && batch[0].lock_valid && !batch[0].lock_count &&
               batch[0].shared_lanes == 1);
    } else if (!strcmp(argv[1], "shared-keyboard")) {
        static const char *const labels[3][3][8] = {
            {{"WAVE", "COLR", "", "", "", "", "", "DRFT"},
             {"", "", "", "", "", "", "", ""},
             {"", "", "", "", "", "", "MIX", ""}},
            {{"WAVE", "POS", "DEP", "HZ", "SHAP", "CLK", "RST", "DRFT"},
             {"ATK", "REL", "", "PENV", "FM", "RAT", "VEL", "FENV"},
             {"", "", "", "", "", "", "MIX", ""}},
            {{"WAVE", "ALG", "BAL", "RTA", "RTB", "FDBK", "", "DRFT"},
             {"ATK", "DEC", "SUS", "REL", "DEP", "ENV", "VEL", ""},
             {"", "", "", "", "", "", "MIX", ""}}};
        compact_envelope_pixels();
        real_display = 1;
        /* a page past the three pages is page 0 */
        v[0x90 / 4] = 3;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(v[0x90 / 4] == 0 && mf_src_page_count(type_id) == 3);
        for (uint32_t source = 0; source < 4; ++source) {
            cs_metadata(&state, source << 4);
            if (source == 2) {
                state.value[CP_WT_CLOCK] = 0;
                state.value[CP_WT_POS] = 233;
                state.value[CP_WT_DEPTH] = 45;
                state.value[CP_WT_RATE] = 132;
                state.value[CP_WT_ATTACK] = 12;
                state.value[CP_WT_RELEASE] = 26;
                state.value[CP_WT_ENV] = 140;
                state.value[CP_WTFM_ENV] = 0;
                state.value[CP_WTFM_RATIO] = 15;
            } else if (source == 3) {
                state.value[CP_FM_ALG] = 3;
                state.value[CP_FM_ATTACK] = 127;
                state.value[CP_FM_DECAY] = 127;
                state.value[CP_FM_SUSTAIN] = 67;
                state.value[CP_FM_RELEASE] = 127;
                state.value[CP_FM_BAL] = 100;
                state.value[CP_COLOR] = 32;
            }
            for (uint32_t page = 0; page < 3; ++page) {
                v[0x90 / 4] = page;
                v[0x190 / 4] = 0;
                focus_mask = 0;
                clear_paint(canvas);
                ip_src_draw((uint8_t *)v, canvas);
                assert(v[0x90 / 4] == page && mf_src_page_count(type_id) == 3);
                for (unsigned i = 0; i < 8; ++i)
                    assert(!strcmp(label_text[i], labels[source < 2 ? 0 : source - 1][page][i]));
                assert(ip_src_parameter((uint8_t *)v, 0) == (page == CS_DETAIL ? 0xca : 0));
                assert(ip_src_parameter((uint8_t *)v, 7) == (page == CS_DETAIL ? 0xd1 : 0));
                assert(ip_src_parameter((uint8_t *)v, 8) == 10);
                if (source == 2 && page == CS_VOICE) {
                    /* The waveform, frame rail, and scan shape use the WT widget bodies. */
                    unsigned waveform = 0;
                    for (unsigned y = 14; y <= 26; ++y)
                        waveform |= canvas[y * 128 + 50];
                    assert(waveform == 1 && canvas[48 * 128 + 25] == 1);
                    /* within the stock graphic rows 13 to 29: the frame rail on 28, the cursor from 14 */
                    for (unsigned x = 49; x <= 121; ++x)
                        assert(canvas[28 * 128 + x] == 1);
                    assert(canvas[14 * 128 + 73] == 1 && canvas[13 * 128 + 73] == 1);
                    for (unsigned x = 48; x <= 122; ++x)
                        assert(canvas[12 * 128 + x] == 0 && canvas[30 * 128 + x] == 0);
                    assert(!(graph_mask & 0x1e));
                }
                if (source < 2 && page == CS_FM_ENV) {
                    /* no FM ENV controls: the stock placeholder in all eight cells, no words */
                    for (unsigned i = 0; i < 4; ++i)
                        assert(canvas[13 * 128 + 26 + 26 * i] == 1 &&
                               canvas[56 * 128 + 26 + 26 * i] == 1);
                    for (unsigned i = 0; i < 8; ++i)
                        assert(!body_text[i][0]);
                    assert(!env_caption);
                }
                if (source == 3 && page == CS_FM_ENV)
                    assert(canvas[40 * 128 + 104] == 1 &&
                           canvas[56 * 128 + 118] == 1); /* H has no control */
                if (source == 3 && page == CS_VOICE)
                    for (unsigned x = 76; x <= 98; ++x)
                        assert(!canvas[57 * 128 + x] &&
                               !canvas[59 * 128 + x]); /* the operators end on row 56 */
                if (source == 2 && page == CS_FM_ENV) {
                    expect_amp(0x0c00, 0, 0x7f00, 0x1a00, 0xd8);
                    assert(graph_value[3] == 88 && graph_value[7] == 0);
                } else if (source == 3 && page == CS_FM_ENV)
                    expect_amp(0x7f00, 0x7eff, 21783, 0x7eff, 0x70);
                else
                    assert(!amp_draws);
                assert(!detail_caption);
                protected_pixels(canvas, page == CS_DETAIL);

                v[0x190 / 4] = 0x01010101;
                clear_paint(canvas);
                ip_src_draw((uint8_t *)v, canvas);
                if (source == 2 && page == CS_VOICE) {
                    assert(!strcmp(label_text[1], "3.33") && !strcmp(label_text[2], "45%") &&
                           !strcmp(label_text[3], "1.32") && !strcmp(label_text[4], "SINE"));
                    /* the text cells keep their captions, as the stock NOTE and PROB: their graphic is the value */
                    assert(!strcmp(label_text[0], "WAVE") && !strcmp(body_text[0], "ANLG"));
                    assert(!strcmp(label_text[5], "CLK") && !strcmp(body_text[5], "FREE"));
                    assert(canvas[28 * 128 + 121] == 1 && canvas[14 * 128 + 73] == 1);
                    v[0x190 / 4] = 0;
                    uint32_t before_toasts = toasts;
                    event((uint8_t *)e, 2, 100, 0, 0);
                    assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
                    assert(state.value[CP_WT_POS] == 333);
                    /* a number: in the caption, no toast */
                    assert(toasts == before_toasts);
                    clear_paint(canvas);
                    ip_src_draw((uint8_t *)v, canvas);
                    assert(!strcmp(label_text[1], "4.33"));
                    assert(canvas[14 * 128 + 73] == 0 && canvas[14 * 128 + 83] == 1);
                }
                if (source == 2 && page == CS_FM_ENV) {
                    assert(!strcmp(label_text[3], "+40%") && !strcmp(label_text[5], "RAT") &&
                           !strcmp(body_text[5], "8.00") && !strcmp(label_text[7], "-100%"));
                    expect_amp(0x0c00, 0, 0x7f00, 0x1a00, 0xd8);
                    /* Stored lock IDs still reach their controls in their new cells. */
                    const IlpSnapshot locks = {2, {{CP_WT_ENV, 96}, {CP_WTFM_ENV, 30}}};
                    assert(cv_lock_mask(&state, &locks) == 0x88);
                    event((uint8_t *)e, 4, 1, 0, 0);
                    assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
                    event((uint8_t *)e, 8, 7, 0, 0);
                    assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
                    assert(state.value[CP_WT_ENV] == 141 && state.value[CP_WTFM_ENV] == 7);
                    event((uint8_t *)e, 1, 10, 0, 0);
                    assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
                    event((uint8_t *)e, 2, 5, 0, 0);
                    assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
                    clear_paint(canvas);
                    ip_src_draw((uint8_t *)v, canvas);
                    expect_amp(0x1600, 0, 0x7f00, 0x1f00, 0xd8);
                }
                if (source == 3 && page == CS_FM_ENV) {
                    assert(!strcmp(label_text[0], "127") && !strcmp(label_text[4], "32"));
                    expect_amp(0x7f00, 0x7eff, 21783, 0x7eff, 0x70);
                    event((uint8_t *)e, 1, -109, 0, 0);
                    assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
                    clear_paint(canvas);
                    ip_src_draw((uint8_t *)v, canvas);
                    assert(!strcmp(label_text[0], "18"));
                    expect_amp(0x1200, 0x7eff, 21783, 0x7eff, 0x70);
                }
                assert(!detail_caption);
                if (source == 3 && page == CS_VOICE)
                    assert(!strcmp(label_text[1], "ALG") && !strcmp(body_text[1], "CHAIN") &&
                           !strcmp(label_text[2], "100%"));
                protected_pixels(canvas, page == CS_DETAIL);
                if (source == 2 && page == CS_VOICE) {
                    v[0x190 / 4] = 0;
                    state.value[CP_WT_CLOCK] = 1;
                    state.value[CP_WT_RATE] = 3;
                    clear_paint(canvas);
                    ip_src_draw((uint8_t *)v, canvas);
                    assert(!strcmp(label_text[3], "RATE"));
                    protected_pixels(canvas, 0);
                }
            }
        }
    } else if (!strcmp(argv[1], "live-lock")) {
        for (unsigned step = 0; step < 128; ++step)
            for (unsigned lane = 0; lane < ILP_LANES; ++lane)
                test_locks[step][lane] = -1;
        v[0x6c / 4] = (uint32_t)(uintptr_t)seq_object;
        v[0x90 / 4] = CS_VOICE;
        uint32_t token = context((uint8_t *)v), steps[4] = {1u << 4, 0, 0, 0};
        e[4] = 20;
        assert(lock_turn((uint8_t *)v, (uint8_t *)e, token, 1, steps, 4) == 1);
        steps[0] = 1u << 5;
        assert(lock_turn((uint8_t *)v, (uint8_t *)e, token, 1, steps, 5) == 1);
        uint32_t id, value;
        assert(ilp_unpack(test_locks[4][0], &id, &value) && id == 2 && value == 84);
        assert(ilp_unpack(test_locks[5][0], &id, &value) && id == 2 && value == 104);
        assert(!test_marked && state.value[CP_COLOR] == 64);
        ip_src_update((uint8_t *)v); /* leaving live record ends the sweep */
        assert(live_control == CS_NONE);
    } else if (!strcmp(argv[1], "held-lock")) {
        for (unsigned step = 0; step < 128; ++step)
            for (unsigned lane = 0; lane < ILP_LANES; ++lane)
                test_locks[step][lane] = -1;
        v[0x6c / 4] = (uint32_t)(uintptr_t)seq_object;
        v[0x90 / 4] = CS_VOICE;
        test_held = 1;
        uint32_t token = context((uint8_t *)v), steps[4] = {1u << 4, 0, 0, 0};
        e[4] = 20;
        assert(lock_turn((uint8_t *)v, (uint8_t *)e, token, 1, steps, 4) == 1);
        uint32_t id, value;
        assert(test_marked == 1 && ilp_unpack(test_locks[4][0], &id, &value));
        assert(id == 2 && value == 84 && state.value[CP_COLOR] == 64);
        test_marked = 0;
        assert(lock_clear((uint8_t *)v, 1, steps) == 1);
        assert(test_marked == 1 && test_locks[4][0] == -1 && state.value[CP_COLOR] == 64);
    } else if (!strcmp(argv[1], "lock-paint")) {
        for (unsigned step = 0; step < 128; ++step)
            for (unsigned lane = 0; lane < ILP_LANES; ++lane)
                test_locks[step][lane] = -1;
        real_display = 1;
        v[0x6c / 4] = (uint32_t)(uintptr_t)seq_object;
        v[0x90 / 4] = CS_VOICE;
        seq_object[0x258 / 4] = 1u << 4;
        test_held = 1;
        context((uint8_t *)v);
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        uint8_t before[8192];
        memcpy(before, canvas, sizeof(before));
        uint16_t packed;
        assert(ilp_pack(2, 84, &packed));
        test_locks[4][0] = packed;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(canvas[20 * 128 + 52] != (before[20 * 128 + 52]));
        assert(canvas[20 * 128 + 80] == before[20 * 128 + 80]);
    } else if (!strcmp(argv[1], "paint")) {
        /* DETAIL keeps the native TUNE and LEV cells unpainted */
        v[0x90 / 4] = CS_DETAIL;
        texts = 0;
        memset(canvas, 0xaa, sizeof(canvas));
        ip_src_draw((uint8_t *)v, canvas);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 128; ++x)
                if (y < 11 || x < 23 || x >= 124 || (y < 37 && x < 48) || (y >= 38 && x >= 101))
                    assert(canvas[y * 128 + x] == 0xaa);
        assert(texts >= 6 && !toasts);
        assert(mf_src_page_count(type_id) == CS_SHARED_PAGES);
        now = 139;
        ip_src_update((uint8_t *)v);
        assert(!invalidations);
        now = 140;
        ip_src_update((uint8_t *)v);
        assert(invalidations == 1);
    } else if (!strcmp(argv[1], "diagrams")) {
        real_display = 1;
        for (unsigned family = 0; family < 4; ++family) {
            cs_metadata(&state, family << 4);
            for (unsigned page = 0; page < CS_SHARED_PAGES; ++page) {
                v[0x90 / 4] = page;
                context((uint8_t *)v);
                for (unsigned values = 0; values < 2; ++values) {
                    v[0x190 / 4] = values ? 0x01010101 : 0;
                    clear_paint(canvas);
                    ip_src_draw((uint8_t *)v, canvas);
                    protected_pixels(canvas, page == CS_DETAIL);
                }
            }
        }
        cs_metadata(&state, 2u << 4);
        v[0x90 / 4] = CS_VOICE;
        v[0x190 / 4] = 0;
        context((uint8_t *)v);
        uint8_t before[8192];
        state.value[CP_WT_POS] = 0;
        state.value[CP_WT_DEPTH] = 0;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        memcpy(before, canvas, sizeof(before));
        state.value[CP_WT_POS] = 700;
        state.value[CP_WT_DEPTH] = 100;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(memcmp(before, canvas, sizeof(before)) && canvas[28 * 128 + 121]);
        protected_pixels(canvas, 0);
    } else if (!strcmp(argv[1], "tone")) {
        real_display = 1;
        cs_metadata(&state, 2u << 4);
        v[0x90 / 4] = CS_VOICE;
        context((uint8_t *)v);
        for (unsigned i = 0; i < 8; ++i)
            assert(ip_src_parameter((uint8_t *)v, i) == 0);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        event((uint8_t *)e, 2, 350, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WT_POS] == 350 && !stock_turns);
        event((uint8_t *)e, 8, 100, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_DRIFT] == 100 && !stock_turns);
        focus_mask = 0;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *labels[] = {"WAVE", "POS", "DEP", "HZ", "SHAP", "CLK", "RST", "DRFT"};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], labels[i]));
        protected_pixels(canvas, 0);
        for (unsigned i = 0; i < 3; ++i) {
            event((uint8_t *)e, 5, 1, i, 0);
            ip_src_turn((uint8_t *)v, (uint8_t *)e);
            assert(state.value[CP_WT_SHAPE] == 0);
        }
        /* numbers (POS, DRFT) and list ticks that change nothing say nothing in the header */
        assert(!toasts);
        event((uint8_t *)e, 5, 1, 1000, 1);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WT_SHAPE] == 1 && !stock_turns);
        /* a changed choice names itself, as the track keyboard's ORDR ("Ascending") */
        assert(toasts == 1 && !strcmp(last_toast, "Triangle"));
        event((uint8_t *)e, 5, 4, 1001, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WT_SHAPE] == 2 && !stock_turns);
        assert(toasts == 2 && !strcmp(last_toast, "Random"));
        for (unsigned key = 41; key <= 48; ++key) {
            e[3] = key;
            assert(ip_src_key((uint8_t *)v, (uint8_t *)e) == 1);
        }
    } else if (!strcmp(argv[1], "fm-envelope")) {
        cs_metadata(&state, 3u << 4);
        v[0x90 / 4] = CS_FM_ENV;
        context((uint8_t *)v);
        assert(mf_src_page_count(type_id) == CS_SHARED_PAGES);
        for (unsigned i = 0; i < 8; ++i)
            assert(ip_src_parameter((uint8_t *)v, i) == 0);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        event((uint8_t *)e, 1, 12, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_FM_ATTACK] == 17 && !stock_turns);
        event((uint8_t *)e, 8, 1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1 && !stock_turns);
        event((uint8_t *)e, 9, 1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 7 && stock_turns == 1);
        v[0x190 / 4] = 0x01010101;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[0], "64"));
        protected_pixels(canvas, 0);
        cs_metadata(&state, 2u << 4);
        context((uint8_t *)v);
        assert(mf_src_page_count(type_id) == CS_SHARED_PAGES && v[0x90 / 4] == CS_FM_ENV);
        assert(ip_src_parameter((uint8_t *)v, 0) == 0);
        cs_metadata(&state, 0);
        context((uint8_t *)v);
        assert(mf_src_page_count(type_id) == CS_SHARED_PAGES && v[0x90 / 4] == CS_FM_ENV);
    } else if (!strcmp(argv[1], "routing")) {
        /* DETAIL: the native TUNE (A) and LEV (H) cells, MIX on G */
        v[0x90 / 4] = CS_DETAIL;
        context((uint8_t *)v);
        assert(ip_src_parameter((uint8_t *)v, 0) == 0xca);
        assert(ip_src_parameter((uint8_t *)v, 7) == 0xd1);
        assert(ip_src_parameter((uint8_t *)v, 2) == 0);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        event((uint8_t *)e, 7, 3, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
        assert(state.value[CP_ARP_MIX] == 3 && !stock_turns);
        event((uint8_t *)e, 1, 3, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 7);
        assert(stock_turns == 1);
        event((uint8_t *)e, 2, 3, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_CHORD] == 0 && stock_turns == 1);
        selected = 1;
        context((uint8_t *)v);
        event((uint8_t *)e, 5, 3, 0, 0);
        assert(ip_src_parameter((uint8_t *)v, 0) == 1000);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 7 && stock_turns == 2);
    } else if (!strcmp(argv[1], "values")) {
        /* DETAIL: MIX on G, the native TUNE and LEV cells, nothing else */
        v[0x90 / 4] = CS_DETAIL;
        context((uint8_t *)v);
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[6], "COLR"));
        v[0x190 / 4] = 0x01010101;
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[6], "64"));
        v[0x190 / 4] = 0;
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[6], "COLR"));
        event((uint8_t *)e, 7, 1, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        ip_src_draw((uint8_t *)v, canvas);
        /* MIX is a number: its caption shows it while the focus runs, and no toast says it again */
        assert(!strcmp(label_text[6], "64") && !toasts);
        focus_mask = 0;
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[6], "COLR"));
        /* Earlier checks intentionally use the all-COLR display stub. Switch
         * to the real model to prove the unused cells are blank, and seed the
         * canvas to verify that no label or graphic is left in them. */
        real_display = 1;
        focus_mask = 0;
        for (unsigned values = 0; values < 2; ++values) {
            v[0x190 / 4] = values ? 0x01010101 : 0;
            clear_paint(canvas);
            ip_src_draw((uint8_t *)v, canvas);
            for (unsigned cell = 1; cell < 6; ++cell)
                assert(!label_text[cell][0] && !body_text[cell][0] && !(graph_mask & (1u << cell)));
            /* ... and the stock placeholder in each: B, C and D at the top, E and F at the bottom */
            for (unsigned cell = 1; cell < 6; ++cell) {
                unsigned x = 33 + 26 * (cell % 4), top = cell < 4 ? 13 : 40;
                assert(canvas[top * 128 + x - 7] == 1 && canvas[top * 128 + x - 6] == 0);
                assert(canvas[(top + 1) * 128 + x - 8] == 1 &&
                       canvas[(top + 16) * 128 + x + 7] == 1);
            }
            protected_pixels(canvas, 1);
        }
    } else if (!strcmp(argv[1], "delayed")) {
        event((uint8_t *)e, 4, 3, 0, 0);
        v[0x90 / 4] = 1;
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_COLOR] == 64 && state.value[CP_GATE] == 50 && !stock_turns);
        event((uint8_t *)e, 4, 3, 0, 0);
        selected = 1;
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(!stock_turns);
    } else if (!strcmp(argv[1], "overflow")) {
        v[0x90 / 4] = CS_DETAIL;
        context((uint8_t *)v);
        uint32_t events[65][6];
        for (unsigned i = 0; i < 65; ++i)
            event((uint8_t *)events[i], 7, 1, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)events[0]);
        assert(state.value[CP_ARP_MIX] == 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)events[64]);
        assert(state.value[CP_ARP_MIX] == 1);
    } else if (!strcmp(argv[1], "tick")) {
        v[0] = (uint32_t)(uintptr_t)vtable;
        vtable[4] = (uint32_t)(uintptr_t)ip_src_draw;
        node[0] = (uint32_t)(uintptr_t)((uint8_t *)manager + 0x14);
        node[2] = (uint32_t)(uintptr_t)v;
        manager[7] = (uint32_t)(uintptr_t)node;
        now = 139;
        instrument_src_tick();
        assert(!invalidations && polls == 1);
        now = 140;
        instrument_src_tick();
        assert(invalidations == 1 && published && polls == 2);
        modal_node[0] = node[0];
        modal_node[2] = (uint32_t)(uintptr_t)modal;
        manager[7] = (uint32_t)(uintptr_t)modal_node;
        instrument_src_tick();
        assert(!published && invalidations == 1 && polls == 2);
        manager[7] = (uint32_t)(uintptr_t)node;
        instrument_src_tick();
        assert(published && invalidations == 2);
        selected = 1;
        instrument_src_tick();
        assert(!published && invalidations == 2);
    } else if (!strcmp(argv[1], "attach")) {
        v[0x90 / 4] = 1;
        ip_src_detach((uint8_t *)v, 0);
        assert(!published);
        ip_src_attach((uint8_t *)v, 0);
        assert(v[0x90 / 4] == 1);
        v[0x90 / 4] = 0;
        ip_src_draw((uint8_t *)v, canvas);
        assert(v[0x90 / 4] == 1);
        generation = 2;
        ip_src_attach((uint8_t *)v, 0);
        assert(v[0x90 / 4] == 0);
    } else if (!strcmp(argv[1], "track")) {
        v[0x90 / 4] = 1;
        context((uint8_t *)v);
        selected = 1;
        context((uint8_t *)v);
        v[0x90 / 4] = 0;
        selected = 0;
        context((uint8_t *)v);
        assert(v[0x90 / 4] == 1);
        /* Ownership follows the instrument's track, not track 1. */
        ip_track = 5;
        selected = 5;
        v[0x90 / 4] = 2;
        assert(context((uint8_t *)v) && ip_src_debug[0] == 5);
        ip_track = 0;
        assert(!context((uint8_t *)v));
        ip_track = 5;
        selected = 0;
        assert(!context((uint8_t *)v));
        ip_track = 0xffffffffu;
        assert(!context((uint8_t *)v));
        ip_track = 0;
        selected = 0;
    } else if (!strcmp(argv[1], "alternate")) {
        /* Sequencer byte +0x268 is the held-trig "edited" mark that the
         * first lock edit sets until release. CANOPY keeps its page then. */
        alternate = 1;
        assert(context((uint8_t *)v));
        event((uint8_t *)e, 4, 3, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1 && !stock_turns);
        for (unsigned step = 0; step < 128; ++step)
            for (unsigned lane = 0; lane < ILP_LANES; ++lane)
                test_locks[step][lane] = -1;
        v[0x6c / 4] = (uint32_t)(uintptr_t)seq_object;
        v[0x90 / 4] = CS_VOICE;
        test_held = 1;
        uint32_t steps[4] = {1u << 4, 0, 0, 0}, token = context((uint8_t *)v);
        assert(token);
        e[4] = 20;
        assert(lock_turn((uint8_t *)v, (uint8_t *)e, token, 1, steps, 4) == 1);
        assert(context((uint8_t *)v) && test_locks[4][0] != -1 && !stock_turns);
        test_held = 0;
        alternate = 0;
        /* A new generation drops the old page registration on a view that
         * no longer owns the instrument (another track selected). */
        selected = 1;
        generation = 2;
        context((uint8_t *)v);
        /* unregistered: 0, and the machines service's hook takes the native page count */
        assert(mf_src_page_count(type_id) == 0);
        selected = 0;
    } else if (!strcmp(argv[1], "header")) {
        /* SRC held: the sound's source in the header where a sample's name would stand, six pixels after the stock text's
         * colon, painted after the help view's draw, in the bubble's font and at its height, once a frame */
        real_display = 1;
        cs_metadata(&state, 2u << 4);
        state.value[CP_WT_TABLE] = 1;
        v[0x90 / 4] = CS_VOICE;
        context((uint8_t *)v);
        static uint32_t plane[256];
        uint32_t bitmap[] = {0, 128, 64, 2, (uint32_t)(uintptr_t)plane, 0};
        /* the stock bar: rows 0 to 9 lit from x 18, its text dark, the colon (rows 4 and 6) at x 50 */
        for (unsigned x = 18; x < 128; ++x)
            plane[2 * x + 1] = 0x3ffu;
        plane[2 * 30 + 1] &= ~0x3cu;
        plane[2 * 50 + 1] &= ~((1u << 4) | (1u << 6));
        static uint8_t help[0xa8], font[4];
        uint8_t *font_pointer = font;
        memcpy(help + 0x9c, &font_pointer, sizeof font_pointer);
        int32_t height = 10;
        memcpy(help + 0xa4, &height, sizeof height);
        /* SRC not held: nothing */
        v[0x190 / 4] = 0;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!drawn_header[0] && !header_boxes);
        /* held: GLASS at x 56, the bubble's text height, black on its white bar */
        v[0x190 / 4] = 0x01010101;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!strcmp(drawn_header, "GLASS") && drawn_x == 56 && drawn_y == 0x42 - 10 &&
               drawn_maxw == 72);
        assert(drawn_font == font && header_boxes == 2 && header_box_x == 56 &&
               header_box_rows == 10);
        /* once: a help draw without the page's draw paints nothing */
        drawn_header[0] = 0;
        header_boxes = 0;
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!drawn_header[0] && !header_boxes);
        /* the bubble sliding in: nothing until it is all in, then the place found stays while it slides */
        height = 4;
        memcpy(help + 0xa4, &height, sizeof height);
        ip_src_draw((uint8_t *)v, canvas);
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!drawn_header[0] && !header_boxes);
        for (int32_t h = 10; h >= 4; h -= 6) {
            memcpy(help + 0xa4, &h, sizeof h);
            ip_src_draw((uint8_t *)v, canvas);
            header_mode = 1;
            ip_help_draw(help, (uint8_t *)bitmap);
            header_mode = 0;
            assert(!strcmp(drawn_header, "GLASS") && drawn_x == 56 && drawn_y == 0x42 - h &&
                   header_box_rows == h);
        }
        /* FM names its algorithm; Velvet and Reed their source */
        cs_metadata(&state, 3u << 4);
        state.value[CP_FM_ALG] = 1;
        context((uint8_t *)v);
        ip_src_draw((uint8_t *)v, canvas);
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!strcmp(drawn_header, "FM STACK"));
        cs_metadata(&state, 1u << 4);
        context((uint8_t *)v);
        ip_src_draw((uint8_t *)v, canvas);
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!strcmp(drawn_header, "REED"));
        /* another track's view (not CANOPY's): its draw sets nothing, the help draw paints nothing */
        drawn_header[0] = 0;
        selected = 1;
        ip_src_draw((uint8_t *)v, canvas);
        header_mode = 1;
        ip_help_draw(help, (uint8_t *)bitmap);
        header_mode = 0;
        assert(!drawn_header[0]);
        selected = 0;
    } else
        return 2;
    puts("ok");
    return 0;
}
