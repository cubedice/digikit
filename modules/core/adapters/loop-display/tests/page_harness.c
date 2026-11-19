#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/model.h"
/* The home: the values as the page sets them. */
static uint32_t xfad, arms, drops;
uint32_t dkrt_loop_get(uint32_t index)
{
    return index == LP_XFAD ? xfad : index == LP_ARM ? arms : index == LP_DROP ? drops : 0;
}
void dkrt_loop_xfad(uint32_t value)
{
    assert(value <= LP_MAX);
    xfad = value;
}
void dkrt_loop_arm(void)
{
    ++arms;
}
void dkrt_loop_drop(void)
{
    ++drops;
}
static void init(LoopUi *s)
{
    memset(s, 0, sizeof(*s));
    s->active = 1;
    xfad = arms = drops = 0;
}
static void controls(void)
{
    LoopUi s;
    init(&s);
    /* H and LEVEL are the crossfader: a tick is a step, the ends are LIVE and LOOP. */
    assert(ld_turn(&s, LD_H, 5) == LD_OWN && xfad == 5);
    assert(ld_turn(&s, LD_LEVEL, 3) == LD_OWN && xfad == 8);
    assert(ld_turn(&s, LD_H, -2) == LD_OWN && xfad == 6);
    assert(ld_turn(&s, LD_LEVEL, 500) == LD_OWN && xfad == LP_MAX);
    assert(ld_turn(&s, LD_H, -500) == LD_OWN && xfad == 0);
    /* A to G are taken and move nothing. */
    xfad = 40;
    for (uint32_t encoder = 0; encoder < LD_H; ++encoder)
        assert(ld_turn(&s, encoder, 9) == LD_OWN);
    assert(xfad == 40 && !arms && !drops);
    assert(ld_turn(&s, 9, 1) == LD_PASS);

    /* The press of H is ARM: once a press, whatever follows it; its release is this page's. */
    assert(ld_key(&s, LD_PUSH + LD_H, LD_PRESS, 0) == LD_OWN && arms == 1);
    assert(ld_key(&s, LD_PUSH + LD_H, LD_REPEAT, 0) == LD_OWN && arms == 1);
    assert(ld_key(&s, LD_PUSH + LD_H, LD_CHORD, 0) == LD_OWN && arms == 1);
    assert(ld_key(&s, LD_PUSH + LD_H, LD_RELEASE, 0) == LD_OWN && arms == 1);
    assert(ld_key(&s, LD_PUSH + LD_H, LD_RELEASE, 0) == LD_PASS);
    /* A second press takes the arm back: the engine toggles on every change of the counter. */
    assert(ld_key(&s, LD_PUSH + LD_H, LD_PRESS, 0) == LD_OWN && arms == 2);
    /* Its release is lost under another view: the next press is a press again. */
    s.active = 0;
    s.active = 1;
    assert(ld_key(&s, LD_PUSH + LD_H, LD_PRESS, 0) == LD_OWN && arms == 3);
    assert(ld_key(&s, LD_PUSH + LD_H, LD_RELEASE, 0) == LD_OWN);
    /* The presses of A to G are taken and do nothing. */
    for (uint32_t i = 0; i < LD_H; ++i) {
        assert(ld_key(&s, LD_PUSH + i, LD_PRESS, 0) == LD_OWN);
        assert(ld_key(&s, LD_PUSH + i, LD_RELEASE, 0) == LD_OWN);
    }
    assert(arms == 3 && !drops && xfad == 40);

    /* FUNC + NO drops the loop; NO alone, and every other key, is the stock view's. */
    assert(ld_key(&s, LD_NO, LD_PRESS, 1) == LD_OWN && drops == 1);
    assert(ld_key(&s, LD_NO, LD_RELEASE, 1) == LD_OWN && drops == 1);
    assert(ld_key(&s, LD_NO, LD_PRESS, 0) == LD_PASS &&
           ld_key(&s, LD_NO, LD_RELEASE, 0) == LD_PASS);
    /* a press that comes as a chord notice while FUNC is down */
    assert(ld_key(&s, LD_NO, LD_CHORD, 1) == LD_OWN && drops == 2);
    assert(ld_key(&s, LD_NO, LD_RELEASE, 1) == LD_OWN);
    assert(ld_key(&s, 5, LD_PRESS, 1) == LD_PASS && ld_key(&s, 11, LD_PRESS, 0) == LD_PASS);
    assert(ld_key(&s, 64, LD_PRESS, 0) == LD_PASS);
    assert(drops == 2);

    /* A page that is not on top takes nothing, but gives back a key it took. */
    assert(ld_key(&s, LD_PUSH + LD_H, LD_PRESS, 0) == LD_OWN && arms == 4);
    s.active = 0;
    assert(ld_key(&s, LD_PUSH + LD_H, LD_RELEASE, 0) == LD_OWN);
    assert(ld_key(&s, LD_PUSH + LD_H, LD_PRESS, 0) == LD_PASS && arms == 4);
    assert(ld_key(&s, LD_NO, LD_PRESS, 1) == LD_PASS && drops == 2);
    assert(ld_turn(&s, LD_H, 3) == LD_PASS && xfad == 40);
}
static void same(const char *got, const char *want)
{
    if (strcmp(got, want)) {
        fprintf(stderr, "got \"%s\", want \"%s\"\n", got, want);
        assert(0);
    }
}
static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
/* A status: state bits, the loop's frames and steps, the frames and steps of the cycle that plays, its step, the
 * loop's phase and the crossfader as applied. */
static void status(uint32_t *w, uint32_t state, uint32_t loop_frames, uint32_t loop_steps,
                   uint32_t next_frames, uint32_t steps, uint32_t step, uint32_t phase, float gain)
{
    memset(w, 0, 4 * LS_WORDS);
    w[LS_MAGIC] = LS_MAGIC_VALUE;
    w[LS_STATE] = state;
    w[LS_LOOP_FRAMES] = loop_frames;
    w[LS_NEXT_FRAMES] = next_frames;
    w[LS_PHASE] = phase;
    w[LS_GAIN] = bits(gain);
    w[LS_STEPS] = loop_steps | (steps << 8) | (step << 16);
    w[LS_CAPACITY] = 1031680;
}
static void texts(void)
{
    uint32_t w[LS_WORDS];
    LoopPage p;
    char t[16];
    ld_time(t, 18000);
    same(t, "375 MS");
    ld_time(t, 96000);
    same(t, "2.00 S");
    ld_time(t, 371520);
    same(t, "7.74 S");
    ld_time(t, 1031680);
    same(t, "21.5 S");

    /* Nothing plays, no loop. */
    status(w, 0, 0, 0, 0, 0, 0, 0, 0);
    ld_page(w, 1, 0, &p);
    same(p.title, "OVERLOOP");
    same(p.state, "IDLE");
    same(p.line1, "NO LOOP");
    same(p.line2, "");
    same(p.arm, "ARM OFF");
    same(p.xfad, "XFAD LIVE");
    assert(!p.armed && !p.loop && p.mark == LD_NO_MARK && p.fader == 0);
    /* A pattern plays: its cycle, and the mark at its step. */
    status(w, LS_TRACK, 0, 0, 96000, 16, 4, 0, 0);
    ld_page(w, 1, 64, &p);
    same(p.line2, "CYCLE 16 ST 2.00 S");
    same(p.xfad, "XFAD 64");
    assert(p.mark == 250 && p.fader == 0);
    /* Armed: what the next change keeps. */
    status(w, LS_TRACK | LS_ARMED, 0, 0, 371520, 64, 32, 0, 0);
    ld_page(w, 1, 0, &p);
    same(p.state, "ARMED");
    same(p.line1, "NEXT CHANGE KEEPS");
    same(p.line2, "64 ST 7.74 S");
    same(p.arm, "ARM ON");
    assert(p.armed && p.mark == 500);
    /* Armed while nothing plays: a change now takes nothing. */
    status(w, LS_ARMED, 0, 0, 0, 0, 0, 0, 0);
    ld_page(w, 1, 0, &p);
    same(p.line2, "NOTHING YET");
    /* Armed with a cycle longer than the buffer: the page says what will be kept. */
    status(w, LS_TRACK | LS_ARMED | LS_NEXT_CUT, 0, 0, 1536000, 128, 0, 0, 0);
    ld_page(w, 1, 0, &p);
    same(p.line2, "LAST 21.5 S ONLY");
    /* A loop at LOOP. */
    status(w, LS_LOOP | LS_TRACK, 96000, 16, 96000, 16, 3, 24000, 1.0f);
    ld_page(w, 1, LP_MAX, &p);
    same(p.state, "LOOP");
    same(p.line1, "LOOP 16 ST 2.00 S");
    same(p.line2, "");
    same(p.xfad, "XFAD LOOP");
    assert(p.loop && p.mark == 250 && p.fader == 127);
    /* The crossfader half way: the fader shows what sounds. */
    status(w, LS_LOOP, 96000, 16, 0, 0, 0, 95999, 0.5f);
    ld_page(w, 1, 64, &p);
    assert(p.fader == 63 && p.mark == 999);
    /* A loop at LIVE stays held. */
    status(w, LS_LOOP, 96000, 16, 0, 0, 0, 0, 0.0f);
    ld_page(w, 1, 0, &p);
    same(p.state, "LOOP");
    same(p.xfad, "XFAD LIVE");
    assert(p.fader == 0 && p.mark == 0);
    /* A loop that was cut, and armed again. */
    status(w, LS_LOOP | LS_CUT, 1031680, 128, 0, 0, 0, 0, 1.0f);
    ld_page(w, 1, LP_MAX, &p);
    same(p.line1, "LOOP 128 ST 21.5 S");
    same(p.line2, "CUT TO ITS LAST PART");
    status(w, LS_LOOP | LS_ARMED | LS_TRACK, 96000, 16, 48000, 8, 0, 0, 1.0f);
    ld_page(w, 1, LP_MAX, &p);
    same(p.state, "LOOP+ARM");
    same(p.line2, "8 ST 1.00 S");
    /* No status from the engine. */
    ld_page(w, 0, 30, &p);
    same(p.state, "NO LINK");
    same(p.line1, "");
    same(p.xfad, "XFAD 30");
    assert(!p.loop && !p.armed && p.mark == LD_NO_MARK && p.fader == 0);
}
/* A canvas that checks what the page draws: inside the screen, every text inside its place. */
typedef struct {
    uint32_t boxes, texts, inverted;
} Drawn;
static void drawn_box(void *ctx, int x, int y, int w, int h, int ink)
{
    Drawn *d = ctx;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 128 && y + h <= 64);
    assert(ink == 0 || ink == 1);
    ++d->boxes;
}
static void drawn_text(void *ctx, int x, int y, const char *s, int ink)
{
    Drawn *d = ctx;
    /* The label font is at most 5 pixels a character with its gap, and 5 high. */
    int half = ((int)strlen(s) * 5 + 1) / 2;
    if (!(*s && x - half >= 0 && x + half <= 128 && y >= 0 && y + 5 <= 64)) {
        fprintf(stderr, "text \"%s\" at %d,%d leaves the screen\n", s, x, y);
        assert(0);
    }
    ++d->texts;
    d->inverted += !ink;
}
static void rendering(void)
{
    uint32_t w[LS_WORDS];
    LoopPage p;
    Drawn d = {0};
    LoopCanvas c = {&d, drawn_box, drawn_text};
    /* every state at both ends of the crossfader and of the timeline */
    static const uint32_t states[] = {0,
                                      LS_TRACK,
                                      LS_TRACK | LS_ARMED,
                                      LS_ARMED,
                                      LS_TRACK | LS_ARMED | LS_NEXT_CUT,
                                      LS_LOOP,
                                      LS_LOOP | LS_CUT,
                                      LS_LOOP | LS_ARMED | LS_TRACK};
    for (uint32_t i = 0; i < sizeof(states) / sizeof(*states); ++i)
        for (uint32_t end = 0; end < 2; ++end) {
            status(w, states[i], 1031680, 128, 1536000, 128, end ? 127 : 0, end ? 1031679 : 0,
                   end ? 1.0f : 0.0f);
            memset(&d, 0, sizeof(d));
            ld_page(w, 1, end ? LP_MAX : 0, &p);
            assert(strlen(p.line1) <= 24 && strlen(p.line2) <= 24);
            ld_render(&p, &c);
            assert(d.inverted == ((states[i] & LS_ARMED) ? 1u : 0u));
            assert(d.texts >= 6);
        }
    memset(&d, 0, sizeof(d));
    ld_render_header(&p, &c);
    assert(d.texts == 2);
    ld_page(w, 0, 0, &p);
    ld_render(&p, &c);
}
int main(void)
{
    controls();
    texts();
    rendering();
    puts("loop page: ok");
    return 0;
}
