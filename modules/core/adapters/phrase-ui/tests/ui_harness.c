#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/model.h"
#include "../src/render.h"
static uint32_t raw[PH_PARAM_COUNT], action, actions, writes;
uint32_t dkrt_phrase_get(uint32_t p)
{
    assert(p < PH_PARAM_COUNT);
    return raw[p];
}
int32_t dkrt_phrase_set(uint32_t p, uint32_t v)
{
    assert(p < PH_PARAM_COUNT);
    raw[p] = v;
    ++writes;
    return 0;
}
int32_t dkrt_phrase_action(uint32_t a)
{
    action = a;
    ++actions;
    return 0;
}
static void press(PhraseUi *s, uint32_t k)
{
    assert(pu_key(s, k, PU_PRESS) == PU_OWN);
}
static void release(PhraseUi *s, uint32_t k)
{
    assert(pu_key(s, k, PU_RELEASE) == PU_OWN);
}
static void tap(PhraseUi *s, uint32_t k)
{
    press(s, k);
    release(s, k);
}
static void init(PhraseUi *s)
{
    memset(s, 0, sizeof(*s));
    memset(raw, 0, sizeof(raw));
    raw[PH_P_LEN] = 127;
    for (uint32_t i = PH_P_GAIN0; i < PH_PARAM_COUNT; ++i)
        raw[i] = 100;
    pu_context(s, 33, 1);
    /* A fresh owner shows DELAY; the cases of the four older pages start on LOOP. */
    assert(s->page == PH_PAGE_DELAY);
    s->page = PH_PAGE_LOOP;
    s->fresh = 1;
}
static void navigation(void)
{
    PhraseUi s;
    init(&s);
    tap(&s, PU_UP);
    assert(s.page == PH_PAGE_DELAY); /* the page before LOOP */
    tap(&s, PU_UP);
    assert(s.page == 3);
    tap(&s, PU_DOWN);
    tap(&s, PU_DOWN);
    assert(!s.page);
    tap(&s, PU_DOWN);
    assert(s.page == 1);
    pu_context(&s, 0, 1);
    pu_context(&s, 33, 1);
    assert(s.page == 1);
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    assert(pu_key(&s, PU_DOWN, PU_PRESS) == PU_PASS);
    assert(s.page == 1);
    assert(pu_key(&s, PU_DOWN, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, PU_FX, PU_CHORD) == PU_NEXT_FX);
    assert(s.page == 1);
    release(&s, PU_FX);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS);
    tap(&s, PU_H);
    assert(s.overlay);
    tap(&s, PU_DOWN);
    assert(s.page == 1 && s.overlay);
    tap(&s, PU_NO);
    assert(!s.overlay && s.page == 1);
    pu_context(&s, 33, 2);
    assert(s.page == PH_PAGE_DELAY);
}
static void sources(void)
{
    PhraseUi s;
    init(&s);
    tap(&s, PU_H);
    tap(&s, PU_TRIG + 2);
    assert(raw[PH_P_MASK] == 4);
    press(&s, PU_TRIG + 2);
    pu_turn(&s, 8, 5);
    release(&s, PU_TRIG + 2);
    assert(raw[PH_P_MASK] == 4 && raw[PH_P_GAIN0 + 2] == 105 && !raw[PH_P_FADE]);
    press(&s, PU_TRIG + 1);
    press(&s, PU_TRIG + 5);
    assert(pu_gain_track(&s) == 5);
    pu_turn(&s, 8, -2);
    release(&s, PU_TRIG + 5);
    assert(pu_gain_track(&s) == 1);
    pu_turn(&s, 8, 3);
    release(&s, PU_TRIG + 1);
    assert(raw[PH_P_GAIN0 + 5] == 98 && raw[PH_P_GAIN0 + 1] == 103 && raw[PH_P_MASK] == 4);
    raw[PH_P_MASK] |= 0x30000;
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    tap(&s, PU_TRIG + 7);
    assert(raw[PH_P_MASK] == 0x30080);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS);
    press(&s, PU_TRIG + 7);
    tap(&s, PU_NO);
    release(&s, PU_TRIG + 7);
    assert(raw[PH_P_MASK] == 0x30080 && !s.overlay); /* closing never leaks the release */
    pu_turn(&s, 8, 4);
    assert(raw[PH_P_FADE] == 4);
    raw[PH_P_GAIN0 + 7] = 0;
    raw[PH_P_EXT_GAIN] = 0;
    assert(!pu_effective_mask());
}
static void capture_lock(void)
{
    PhraseUi s;
    init(&s);
    tap(&s, PU_H);
    tap(&s, PU_YES);
    assert(action == PH_ACTION_ARM);
    s.status[PH_S_STATE / 4] = PH_ARMED;
    s.status[PH_S_REC_ACTIVE / 4] = 1;
    uint32_t before = writes;
    tap(&s, PU_TRIG + 2);
    pu_turn(&s, 0, 4);
    pu_turn(&s, 1, 3);
    assert(before == writes);
    press(&s, PU_TRIG);
    pu_turn(&s, 8, 2);
    release(&s, PU_TRIG);
    assert(before == writes);
    pu_turn(&s, 8, 2);
    assert(raw[PH_P_FADE] == 2);
    tap(&s, PU_YES);
    assert(action == PH_ACTION_CANCEL);
    s.status[PH_S_STATE / 4] = PH_HANDOFF;
    tap(&s, PU_TRIG + 3);
    assert(!raw[PH_P_MASK]);
}
static void clear_gesture(void)
{
    PhraseUi s;
    init(&s);
    uint32_t before = actions;
    assert(pu_key(&s, PU_NO, PU_PRESS) == PU_PASS);
    assert(pu_key(&s, PU_NO, PU_RELEASE) == PU_PASS && actions == before);
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    press(&s, PU_NO);
    assert(action == PH_ACTION_CLEAR && actions == before + 1);
    assert(pu_key(&s, PU_NO, PU_REPEAT) == PU_OWN && actions == before + 1);
    pu_context(&s, 0, 1);
    release(&s, PU_NO); /* release cannot escape after view loss */
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    assert(pu_key(&s, PU_NO, PU_PRESS) == PU_PASS && actions == before + 1);
    assert(pu_key(&s, PU_NO, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS);
    pu_context(&s, 33, 1);
    tap(&s, PU_H);
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    tap(&s, PU_NO);
    assert(s.overlay && action == PH_ACTION_CLEAR && actions == before + 2);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS);
    tap(&s, PU_NO);
    assert(!s.overlay && actions == before + 2);
}
static void led_ownership(void)
{
    PhraseUi s;
    init(&s);
    raw[PH_P_MASK] = 5;
    assert(pu_led(&s, 26, 0x11) == 0x11);
    tap(&s, PU_H);
    assert(pu_led(&s, 26, 0x11) == 0x71 && pu_led(&s, 27, 0x71) == 0x02);
    assert(pu_led(&s, 25, 0x21) == 0x21 && pu_led(&s, 42, 0x21) == 0x21);
    raw[PH_P_GAIN0] = 0;
    assert(pu_led(&s, 26, 0x11) == 0x02);
    tap(&s, PU_NO);
    assert(pu_led(&s, 26, 0x11) == 0x11);
    /* Never acquire a stock-owned key held before opening the overlay. */
    assert(pu_key(&s, PU_TRIG, PU_PRESS) == PU_PASS);
    tap(&s, PU_H);
    assert(pu_key(&s, PU_TRIG, PU_REPEAT) == PU_PASS);
    assert(pu_key(&s, PU_TRIG, PU_RELEASE) == PU_PASS);
}
static void covered_view(void)
{
    PhraseUi s;
    init(&s);
    tap(&s, PU_DOWN);
    tap(&s, PU_H);
    press(&s, PU_TRIG + 4);
    pu_context(&s, 0, 1);
    assert(!s.overlay && s.page == 1 && pu_led(&s, 30, 0x11) == 0x11);
    assert(pu_turn(&s, 8, 3) == PU_PASS && !raw[PH_P_FADE]);
    release(&s, PU_TRIG + 4);
    assert(!raw[PH_P_MASK]);
    pu_context(&s, 33, 1);
    assert(!s.overlay && s.page == 1);
}
static void native_gestures(void)
{
    assert(pu_native_flags(1) == PU_PRESS && pu_native_flags(5) == PU_PRESS);
    assert(pu_native_flags(9) == PU_REPEAT && pu_native_flags(2) == PU_CHORD);
    assert(pu_native_flags(0) == PU_RELEASE && pu_native_flags(4) == PU_RELEASE);
    assert(pu_native_flags(16) == PU_RELEASE);
    PhraseUi s;
    init(&s);
    tap(&s, PU_H);
    assert(pu_key(&s, PU_TRIG + 2, pu_native_flags(1)) == PU_OWN);
    assert(pu_key(&s, PU_TRIG + 2, pu_native_flags(0)) == PU_OWN && raw[PH_P_MASK] == 4);
    press(&s, PU_TRIG + 3);
    pu_context(&s, 0, 1);
    const uint32_t up[2] = {0, 0};
    pu_reconcile(&s, up);
    assert(!s.owned[0] && !s.held[0] && raw[PH_P_MASK] == 4);
    pu_context(&s, 33, 1);
    tap(&s, PU_DOWN);
    assert(s.page == 1);
    tap(&s, PU_H);
    release(&s, PU_TRIG + 3);
    assert(raw[PH_P_MASK] == 4);
    press(&s, PU_H);
    pu_context(&s, 0, 1);
    pu_reconcile(&s, up);
    pu_context(&s, 33, 1);
    tap(&s, PU_H);
    assert(s.overlay); /* missed H release cannot suppress next press */
    press(&s, PU_TRIG + 4);
    pu_context(&s, 0, 1);
    pu_reconcile(&s, up);
    pu_context(&s, 33, 1);
    tap(&s, PU_H);
    tap(&s, PU_TRIG + 4);
    assert(raw[PH_P_MASK] == 20); /* new initial gesture replaces the old tombstone */
    uint32_t before = actions;
    tap(&s, PU_NO);
    assert(pu_key(&s, PU_H, pu_native_flags(9)) == PU_PASS && !s.overlay && actions == before);
    assert(pu_key(&s, PU_H, pu_native_flags(0)) == PU_PASS);
    /* A fresh modified down event belongs to the new view. It must replace
     * a cancelled gesture's tombstone so its later release also stays stock. */
    init(&s);
    tap(&s, PU_H);
    press(&s, PU_TRIG + 2);
    pu_context(&s, 0, 1);
    pu_reconcile(&s, up);
    assert(pu_key(&s, PU_TRIG + 2, PU_CHORD) == PU_PASS);
    assert(pu_key(&s, PU_TRIG + 2, PU_RELEASE) == PU_PASS);
    assert(!raw[PH_P_MASK]);
    /* A chord notification during the original still-held gesture is not
     * a new press. It and its release remain cancelled and cannot leak. */
    init(&s);
    tap(&s, PU_H);
    press(&s, PU_TRIG + 2);
    pu_context(&s, 0, 1);
    const uint32_t still_down[2] = {1u << (PU_TRIG + 2), 0};
    pu_reconcile(&s, still_down);
    assert(pu_key(&s, PU_TRIG + 2, PU_CHORD) == PU_OWN);
    assert(pu_key(&s, PU_TRIG + 2, PU_RELEASE) == PU_OWN && !raw[PH_P_MASK]);
}
static void detents(void)
{
    PhraseUi s;
    init(&s);
    pu_turn(&s, 1, 1);
    pu_turn(&s, 1, 1);
    pu_turn(&s, 1, 1);
    assert(!raw[PH_P_FROM]);
    pu_turn(&s, 1, 1);
    assert(raw[PH_P_FROM] == 1);
    pu_turn(&s, 1, -3);
    assert(raw[PH_P_FROM] == 1);
    pu_turn(&s, 1, -1);
    assert(!raw[PH_P_FROM]);
    tap(&s, PU_DOWN);
    pu_turn(&s, 1, 100);
    assert(raw[PH_P_RATE] == 1); /* no skipping */
    pu_turn(&s, 0, 8);
    assert(raw[PH_P_SIZE] == 8); /* numeric remains responsive */
    uint32_t before = writes;
    pu_turn(&s, 6, 10);
    pu_turn(&s, 7, 10);
    assert(writes == before);
    char b[16];
    pu_format(b, PH_P_TUNE, 47);
    assert(!strcmp(b, "-0.5"));
    pu_format(b, PH_P_SCAN, 0);
    assert(!strcmp(b, "-2.00X"));
    pu_format(b, PH_P_SIZE, 127);
    assert(!strcmp(b, "500MS"));
    pu_format(b, PH_P_DEC, 0);
    assert(!strcmp(b, "0.15S"));
    pu_format(b, PH_P_DEC, 127);
    assert(!strcmp(b, "8.00S"));
}
static unsigned pixels, texts, knobs, live_error;
static unsigned header_only;
static int cursor_x;
static void draw_box(void *c, int x, int y, int w, int h, int ink)
{
    (void)c;
    assert(x >= 0 && y >= 0 && x + w <= 128 && y + h <= 64 && w > 0 && h > 0);
    if (header_only)
        assert(y + h <= 10);
    pixels += (unsigned)(w * h);
    if (w == 1 && h == 9 && !ink)
        cursor_x = x;
}
static void draw_text(void *c, int x, int y, const char *s, int ink)
{
    (void)c;
    (void)ink;
    assert(x >= 0 && x < 128 && y >= 0 && y + 5 <= 64);
    if (header_only)
        assert(y + 5 <= 10);
    assert(strlen(s) < 16);
    if (!strcmp(s, "LIVE ERR"))
        ++live_error;
    ++texts;
}
static void draw_knob(void *c, uint32_t cell, uint32_t value)
{
    (void)c;
    assert(!header_only && cell < 8 && value <= 127);
    ++knobs;
}
static void rendering(void)
{
    PhraseUi s;
    init(&s);
    PhraseCanvas c = {0, draw_box, draw_text, draw_knob};
    for (uint32_t page = 0; page < PH_PAGES; ++page)
        for (uint32_t st = 0; st <= PH_HANDOFF; ++st) {
            s.page = page;
            s.status[PH_S_STATE / 4] = st;
            s.status[PH_S_HELD_FRAMES / 4] = 384000;
            s.status[PH_S_CROP_START / 4] = 96000;
            s.status[PH_S_CROP_LEN / 4] = 192000;
            s.status[PH_S_GRAIN_VALID / 4] = 0xff;
            for (uint32_t i = 0; i < 4; ++i)
                s.status[PH_S_GRAIN_POS0 / 4 + i] = 0xe0002000;
            for (uint32_t overlay = 0; overlay < 2; ++overlay) {
                s.overlay = overlay;
                pu_render(&s, &c);
            }
        }
    s.fresh = 0;
    pu_render(&s, &c);
    assert(pixels && texts && knobs);
    s.page = 2;
    s.overlay = 0;
    s.fresh = 1;
    union {
        float f;
        uint32_t u;
    } phase = {96000.0f};
    s.status[PH_S_SCAN_PHASE / 4] = phase.u;
    raw[PH_P_POS] = 0;
    cursor_x = -1;
    pu_render(&s, &c);
    assert(cursor_x == 60); /* crop start + 96000 frames = phrase midpoint */
    raw[PH_P_POS] = 127;
    cursor_x = -1;
    pu_render(&s, &c);
    assert(cursor_x == 60); /* a complete crop wraps; POS stays separate */
    s.fresh = 0;
    cursor_x = -1;
    pu_render(&s, &c);
    assert(cursor_x == -1);
    s.fresh = 1;
    s.status[PH_S_ROUTE_FAULT / 4] = 1;
    pu_render(&s, &c);
    assert(live_error == 1);
    /* A passive native overlay repaints only the header after the full FX
     * view: restoring it must never erase control values or input tiles. */
    header_only = 1;
    for (uint32_t overlay = 0; overlay < 2; ++overlay) {
        s.overlay = overlay;
        pu_render_header(&s, &c);
    }
    header_only = 0;
}
int main(void)
{
    navigation();
    sources();
    capture_lock();
    clear_gesture();
    led_ownership();
    covered_view();
    native_gestures();
    detents();
    rendering();
    puts(
        "phrase UI: ownership, navigation, capture locking, detents, LEDs and render bounds passed");
    return 0;
}
