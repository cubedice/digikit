#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/src-native.c"
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
int32_t ip_panel_step_note(uint32_t track, uint32_t step)
{
    (void)track;
    (void)step;
    return -1;
}
void instrument_src_preview_step(CvDisplay *d, const IlpSnapshot *s, int32_t note)
{
    (void)d;
    (void)s;
    (void)note;
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
uint32_t mf_native_page_count(uint32_t type, uint32_t ordinary)
{
    (void)type;
    return ordinary;
}
uint32_t instrument_src_pages(void)
{
    return cs_pages(&state);
}
uint32_t instrument_src_tune_page(uint32_t page)
{
    return page == 0;
}
static uint32_t selected, alternate, now = 100, type_id = 9, generation = 1, stock_turns,
                                     stock_keys, invalidations, polls;
static uint32_t manager[16], node[4], vtable[60], modal[24], modal_node[4];
static uint32_t focus_mask, texts, graphs, toasts;
static char label_text[8][32];
static char body_text[8][32];
static char arp_caption[32];
static uint32_t real_display, graph_mask, graph_value[8];
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
    if (x == 98 && y == 0)
        strcpy(arp_caption, s);
    if (x >= 33 && x <= 111 && (x - 33) % 26 == 0) {
        if (y == 0 || y == 27)
            strcpy(label_text[(x - 33) / 26 + (y == 0 ? 4 : 0)], s);
        if (y == 14 || y == 41)
            strcpy(body_text[(x - 33) / 26 + (y == 14 ? 4 : 0)], s);
    }
    va_end(ap);
    return 0;
}
void dk_fw_draw_fill_rect(uint8_t *p, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
{
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
    if (!strcmp(argv[1], "live-lock")) {
        for (unsigned step = 0; step < 128; ++step)
            for (unsigned lane = 0; lane < ILP_LANES; ++lane)
                test_locks[step][lane] = -1;
        v[0x6c / 4] = (uint32_t)(uintptr_t)seq_object;
        v[0x90 / 4] = 2;
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
        v[0x90 / 4] = 2;
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
        v[0x90 / 4] = 2;
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
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 128; ++x)
                if (y < 11 || x < 23 || x >= 124 || (y < 37 && x < 48) || (y >= 38 && x >= 101))
                    assert(canvas[y * 128 + x] == 0xaa);
        assert(texts >= 6 && !toasts);
        assert(mf_src_page_count(type_id) == 4);
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
            for (unsigned page = 0; page < 4; ++page) {
                v[0x90 / 4] = page;
                context((uint8_t *)v);
                for (unsigned values = 0; values < 2; ++values) {
                    v[0x190 / 4] = values ? 0x01010101 : 0;
                    clear_paint(canvas);
                    ip_src_draw((uint8_t *)v, canvas);
                    protected_pixels(canvas, page == 0);
                }
            }
        }
        cs_metadata(&state, 2u << 4);
        v[0x90 / 4] = 2;
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
        assert(memcmp(before, canvas, sizeof(before)) && canvas[29 * 128 + 121]);
        protected_pixels(canvas, 0);
        v[0x90 / 4] = 3;
        context((uint8_t *)v);
        state.value[CP_WT_ATTACK] = 0;
        state.value[CP_WT_RELEASE] = 0;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        memcpy(before, canvas, sizeof(before));
        state.value[CP_WT_ATTACK] = 127;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(memcmp(before, canvas, sizeof(before)));
        cs_metadata(&state, 3u << 4);
        v[0x90 / 4] = 3;
        context((uint8_t *)v);
        state.value[CP_FM_SUSTAIN] = 0;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        memcpy(before, canvas, sizeof(before));
        state.value[CP_FM_SUSTAIN] = 100;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(memcmp(before, canvas, sizeof(before)));
        protected_pixels(canvas, 0);
        CvDisplay telemetry = {0};
        telemetry.fresh = 1;
        telemetry.note_count = 2;
        telemetry.pitches[0] = 60;
        telemetry.pitches[1] = 64;
        telemetry.arp_note = 64;
        telemetry.arp_active = 1;
        telemetry.clock_state = IS_CLOCK_RUNNING;
        telemetry.raw[CP_ARP_MIX] = 64;
        telemetry.raw[CP_OCTAVES] = 3;
        telemetry.raw[CP_GATE] = 50;
        memset(canvas, 0, sizeof(canvas));
        diagram_arp(canvas, &telemetry);
        assert(canvas[38 * 128 + 81] && !strcmp(arp_caption, "E4"));
        telemetry.fresh = 0;
        memset(canvas, 0, sizeof(canvas));
        diagram_arp(canvas, &telemetry);
        assert(!canvas[38 * 128 + 81] && !strcmp(arp_caption, "LINK LOST"));
        telemetry.fresh = 1;
        telemetry.clock_state = IS_CLOCK_LOST;
        memset(canvas, 0, sizeof(canvas));
        diagram_arp(canvas, &telemetry);
        assert(!canvas[38 * 128 + 81] && !strcmp(arp_caption, "CLK LOST"));
        telemetry.fault = 2;
        telemetry.clock_state = IS_CLOCK_RUNNING;
        memset(canvas, 0, sizeof(canvas));
        diagram_arp(canvas, &telemetry);
        assert(!canvas[38 * 128 + 81] && !strcmp(arp_caption, "VOICE ERR"));
    } else if (!strcmp(argv[1], "tone")) {
        real_display = 1;
        cs_metadata(&state, 2u << 4);
        v[0x90 / 4] = 2;
        context((uint8_t *)v);
        for (unsigned i = 0; i < 8; ++i)
            assert(ip_src_parameter((uint8_t *)v, i) == 0);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        event((uint8_t *)e, 2, 350, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WT_POS] == 350 && !stock_turns);
        event((uint8_t *)e, 6, 100, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_DRIFT] == 100 && !stock_turns);
        focus_mask = 0;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *labels[] = {"WAVE", "POS", "DEP", "HZ", "SHAP", "DRFT", "CLK", "RST"};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], labels[i]));
        protected_pixels(canvas, 0);
        for (unsigned i = 0; i < 3; ++i) {
            event((uint8_t *)e, 5, 1, i, 0);
            ip_src_turn((uint8_t *)v, (uint8_t *)e);
            assert(state.value[CP_WT_SHAPE] == 0);
        }
        event((uint8_t *)e, 5, 1, 1000, 1);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WT_SHAPE] == 1 && !stock_turns);
        event((uint8_t *)e, 5, 4, 1001, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WT_SHAPE] == 2 && !stock_turns);
        for (unsigned key = 41; key <= 48; ++key) {
            e[3] = key;
            assert(ip_src_key((uint8_t *)v, (uint8_t *)e) == 1);
        }
    } else if (!strcmp(argv[1], "wavetable-fm")) {
        real_display = 1;
        cs_metadata(&state, 2u << 4);
        v[0x90 / 4] = 3;
        context((uint8_t *)v);
        assert(mf_src_page_count(type_id) == 4);
        state.value[CP_WTFM_DEPTH] = 100;
        state.value[CP_WTFM_RATIO] = 15;
        state.value[CP_WTFM_ENV] = 0;
        state.value[CP_WTFM_WHEEL] = 200;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *labels[] = {"ATK", "REL", "FM", "RAT", "PENV", "FENV", "VEL", ""};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], labels[i]));
        assert(!strcmp(body_text[3], "8.00") && graph_mask == 0x74);
        protected_pixels(canvas, 0);
        v[0x190 / 4] = 0x01010101;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *values[] = {"5", "15", "100%", "8.00", "0%", "-100%", "0%", ""};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], values[i]));
        protected_pixels(canvas, 0);
        for (unsigned i = 0; i < 8; ++i)
            assert(ip_src_parameter((uint8_t *)v, i) == 0);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        event((uint8_t *)e, 3, -1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1 && state.value[CP_WTFM_DEPTH] == 99);
        event((uint8_t *)e, 8, 1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1 && !stock_turns);
        event((uint8_t *)e, 9, 1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 7 && stock_turns == 1);
        event((uint8_t *)e, 4, -3, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WTFM_RATIO] == 15);
        event((uint8_t *)e, 4, -1, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(state.value[CP_WTFM_RATIO] == 14);
        for (unsigned key = 41; key <= 48; ++key) {
            e[3] = key;
            assert(ip_src_key((uint8_t *)v, (uint8_t *)e) == 1);
        }
    } else if (!strcmp(argv[1], "chord-preview")) {
        CsState shown = state;
        IsStatus status = {0};
        CvDisplay d;
        shown.mode = KB_ROOT;
        shown.generation = 1;
        shown.track = 0;
        status.mode = KB_PLAY;
        status.generation = 1;
        status.track = 0;
        status.fast_valid = 1;
        status.fast_at = now;
        cv_build_owner(&d, &shown, &status, now, "0", "127", 0, KB_PLAY);
        assert(d.fresh && d.fault != 1);
        const uint8_t notes[3] = {60, 64, 67};
        cv_preview_notes(&d, notes, 3);
        assert(d.note_count == 3 && !strcmp(d.playing, "C") && !strcmp(d.heading, "C"));
        assert(d.pitches[0] == 60 && d.pitches[1] == 64 && d.pitches[2] == 67);
    } else if (!strcmp(argv[1], "fm-envelope")) {
        CvDisplay env = {0};
        env.focus_id = CS_NONE;
        env.raw[CP_FM_SUSTAIN] = 100;
        /* Zero attack must remain vertical for every release value. */
        for (unsigned rel = 0; rel < 128; rel += rel == 0 ? 1 : rel == 1 ? 63 : 63) {
            env.raw[CP_FM_RELEASE] = (uint16_t)rel;
            memset(canvas, 0, sizeof(canvas));
            diagram_envelope(canvas, &env, 1);
            for (int y = 16; y < 28; ++y)
                assert(canvas[y * 128 + 48] && !canvas[y * 128 + 49]);
        }
        /* An instant release is vertical at the sustain corner, even when all
         * envelope times are at zero. */
        env.raw[CP_FM_RELEASE] = 0;
        memset(canvas, 0, sizeof(canvas));
        diagram_envelope(canvas, &env, 1);
        for (int y = 16; y < 28; ++y)
            assert(canvas[y * 128 + 64]);
        /* The horizontal sustain has a constant width as decay changes;
         * locate its two hollow nodes in the painted graph. */
        for (unsigned level = 20; level <= 80; level += 30)
            for (unsigned dec = 0; dec <= 127; dec += dec == 0 ? 64 : 63) {
                env.raw[CP_FM_SUSTAIN] = (uint16_t)level;
                env.raw[CP_FM_DECAY] = (uint16_t)dec;
                memset(canvas, 0, sizeof(canvas));
                diagram_envelope(canvas, &env, 1);
                int y = 29 - (int)((16 * level + 50) / 100), nodes[2], count = 0;
                for (int x = 47; x < 100 && count < 2; ++x)
                    if (!canvas[y * 128 + x] && canvas[y * 128 + x - 1] &&
                        canvas[y * 128 + x + 1] && canvas[(y - 1) * 128 + x - 1] &&
                        canvas[(y - 1) * 128 + x] && canvas[(y - 1) * 128 + x + 1] &&
                        canvas[(y + 1) * 128 + x - 1] && canvas[(y + 1) * 128 + x] &&
                        canvas[(y + 1) * 128 + x + 1])
                        nodes[count++] = x;
                assert(count == 2 && nodes[1] - nodes[0] == 16);
            }
        env.raw[CP_FM_SUSTAIN] = 60;
        env.raw[CP_FM_DECAY] = 50;
        memset(canvas, 0, sizeof(canvas));
        diagram_envelope(canvas, &env, 1);
        uint8_t middle[18 * 47];
        int corner = 0;
        for (int x = 50; x < 123; ++x)
            if (canvas[12 * 128 + x]) {
                corner = x - 1;
                break;
            }
        assert(corner > 51);
        for (int y = 12; y < 30; ++y)
            for (int x = 50; x < 97; ++x)
                middle[(y - 12) * 47 + x - 50] = canvas[y * 128 + x];
        env.raw[CP_FM_ATTACK] = 127;
        env.raw[CP_FM_RELEASE] = 127;
        memset(canvas, 0, sizeof(canvas));
        diagram_envelope(canvas, &env, 1);
        for (int y = 12; y < 30; ++y)
            for (int x = 50; x < corner - 1; ++x)
                assert(middle[(y - 12) * 47 + x - 50] == canvas[y * 128 + x]);
        cs_metadata(&state, 3u << 4);
        v[0x90 / 4] = 3;
        context((uint8_t *)v);
        assert(mf_src_page_count(type_id) == 4);
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
        assert(mf_src_page_count(type_id) == 4 && v[0x90 / 4] == 3);
        assert(ip_src_parameter((uint8_t *)v, 0) == 0);
        cs_metadata(&state, 0);
        context((uint8_t *)v);
        assert(mf_src_page_count(type_id) == 4 && v[0x90 / 4] == 3);
    } else if (!strcmp(argv[1], "fm-presentation")) {
        real_display = 1;
        cs_metadata(&state, 3u << 4);
        v[0x90 / 4] = 2;
        context((uint8_t *)v);
        state.value[CP_FM_ALG] = 3;
        state.value[CP_FM_BAL] = 100;
        state.value[CP_FM_RTA] = 0;
        state.value[CP_FM_RTB] = 15;
        state.value[CP_FM_FB] = 100;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *tone_labels[] = {"WAVE", "DEP", "ALG", "BAL", "RTA", "RTB", "FDBK", "DRFT"};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], tone_labels[i]));
        assert(!strcmp(body_text[4], "0.25") && !strcmp(body_text[5], "8.00"));
        assert(graph_mask == 0xc0 && graph_value[6] == 127);
        protected_pixels(canvas, 0);
        v[0x190 / 4] = 0x01010101;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *tone_values[] = {"FM", "32", "CHAIN", "100%", "0.25", "8.00", "100%", "0"};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], tone_values[i]));
        assert(graph_mask == 0xc0);
        protected_pixels(canvas, 0);
        state.value[CP_FM_RTA] = 5;
        state.value[CP_FM_RTB] = 7;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[4], "1.41") && !strcmp(label_text[5], "1.73"));
        char feedback[32];
        cv_feedback(feedback, &state, 4);
        assert(!strcmp(feedback, "PAIR A RATIO=SQRT2*"));
        cv_feedback(feedback, &state, 5);
        assert(!strcmp(feedback, "PAIR B RATIO=SQRT3*"));
        v[0x90 / 4] = 3;
        v[0x190 / 4] = 0;
        context((uint8_t *)v);
        const uint16_t envelope[] = {127, 127, 100, 127, 0, 100, 200};
        for (unsigned i = 0; i < 7; ++i)
            state.value[CP_FM_ATTACK + i] = envelope[i];
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *env_labels[] = {"ATK", "DEC", "SUS", "REL", "ENV", "VEL", "", ""};
        for (unsigned i = 0; i < 8; ++i)
            assert(!strcmp(label_text[i], env_labels[i]));
        assert(graph_mask == 0x30 && graph_value[4] == 0 && graph_value[5] == 63);
        assert(canvas[20 * 128 + 24] == 0); /* FM ENV A is custom, not protected TUNE. */
        protected_pixels(canvas, 0);
        v[0x190 / 4] = 0x01010101;
        clear_paint(canvas);
        ip_src_draw((uint8_t *)v, canvas);
        const char *env_values[] = {"127", "127", "100%", "127", "-100%", "0%", "", ""};
        for (unsigned i = 0; i < 8; ++i) {
            assert(!strcmp(label_text[i], env_values[i]));
            assert(strlen(label_text[i]) <= 5);
        }
        assert(graph_mask == 0x30);
        protected_pixels(canvas, 0);
        assert(ip_src_parameter((uint8_t *)v, 0) == 0 && ip_src_parameter((uint8_t *)v, 7) == 0);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        event((uint8_t *)e, 8, 1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 1);
        event((uint8_t *)e, 9, 1, 0, 0);
        assert(ip_src_turn((uint8_t *)v, (uint8_t *)e) == 7 && stock_turns == 1);
    } else if (!strcmp(argv[1], "routing")) {
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
        assert(!strcmp(label_text[1], "COLR"));
        v[0x190 / 4] = 0x01010101;
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[1], "64"));
        v[0x190 / 4] = 0;
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[1], "COLR"));
        event((uint8_t *)e, 7, 1, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[6], "64") && toasts == 1);
        focus_mask = 0;
        ip_src_draw((uint8_t *)v, canvas);
        assert(!strcmp(label_text[6], "COLR"));
        v[0x90 / 4] = 1;
        context((uint8_t *)v);
        assert(ip_src_parameter((uint8_t *)v, 8) == 10);
        uint32_t revision = state.revision;
        event((uint8_t *)e, 8, 12, 0, 0);
        ip_src_turn((uint8_t *)v, (uint8_t *)e);
        assert(cs_choice_value(&state, CS_BPM) == 120 && state.revision == revision &&
               !stock_turns);
        /* Earlier checks intentionally use the all-COLR display stub. Switch
         * to the real model to prove retired controls are blank, and seed the
         * canvas to verify their old labels/graphics are actually erased. */
        real_display = 1;
        focus_mask = 0;
        for (unsigned values = 0; values < 2; ++values) {
            v[0x190 / 4] = values ? 0x01010101 : 0;
            clear_paint(canvas);
            ip_src_draw((uint8_t *)v, canvas);
            for (unsigned cell = 6; cell < 8; ++cell) {
                assert(!label_text[cell][0] && !body_text[cell][0] && !(graph_mask & (1u << cell)));
                /* Read-only diagram pixels may occupy these cells. */
            }
            protected_pixels(canvas, 0);
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
        v[0x90 / 4] = 2;
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
        assert(mf_src_page_count(type_id) == 1);
        selected = 0;
    } else
        return 2;
    puts("ok");
    return 0;
}
