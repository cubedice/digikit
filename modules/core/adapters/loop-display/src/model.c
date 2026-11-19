#include "model.h"
/* Keep generated branch data out of validated executable sections. This
 * freestanding module has no libc, so leave small clear loops as stores. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
static int owned(const LoopUi *s, uint32_t k)
{
    return (s->owned[k >> 5] >> (k & 31)) & 1;
}
static void own(LoopUi *s, uint32_t k, int on)
{
    if (on)
        s->owned[k >> 5] |= 1u << (k & 31);
    else
        s->owned[k >> 5] &= ~(1u << (k & 31));
}
uint32_t ld_key(LoopUi *s, uint32_t key, uint32_t flags, uint32_t func)
{
    if (key >= 64)
        return LD_PASS;
    int mine = owned(s, key);
    if (flags & LD_RELEASE) {
        /* The release of a key this page took is this page's, also after another view covered it. */
        own(s, key, 0);
        return mine ? LD_OWN : LD_PASS;
    }
    /* A press starts a gesture, also the press of a key whose release was lost under another view; a repeat or a chord
     * notice of a key this page took goes on being this page's. */
    if (!(flags & LD_PRESS)) {
        if (mine || !(flags & LD_CHORD))
            return mine ? LD_OWN : LD_PASS;
    }
    if (!s->active)
        return LD_PASS;
    if (key == LD_NO && func) {
        dkrt_loop_drop();
        ++s->revision;
        own(s, key, 1);
        return LD_OWN;
    }
    if (key >= LD_PUSH && key < LD_PUSH + 8) {
        if (key == LD_PUSH + LD_H) {
            dkrt_loop_arm();
            ++s->revision;
        }
        own(s, key, 1);
        return LD_OWN;
    }
    return LD_PASS;
}
uint32_t ld_turn(LoopUi *s, uint32_t encoder, int32_t delta)
{
    if (!s->active || encoder > LD_LEVEL)
        return LD_PASS;
    if (encoder == LD_H || encoder == LD_LEVEL) {
        int32_t value = (int32_t)dkrt_loop_get(LP_XFAD) + delta;
        if (value < 0)
            value = 0;
        if (value > LP_MAX)
            value = LP_MAX;
        dkrt_loop_xfad((uint32_t)value);
        ++s->revision;
    }
    return LD_OWN;
}
static char *copy(char *out, const char *s)
{
    while (*s)
        *out++ = *s++;
    *out = 0;
    return out;
}
static char *number(char *out, uint32_t n)
{
    char rev[10];
    uint32_t count = 0;
    do {
        rev[count++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (count)
        *out++ = rev[--count];
    *out = 0;
    return out;
}
void ld_time(char out[16], uint32_t frames)
{
    /* 48 frames a millisecond, to the nearest. */
    uint32_t ms = (frames + 24) / 48;
    if (ms < 1000) {
        copy(number(out, ms), " MS");
        return;
    }
    /* Seconds with two places below ten seconds and one from there. */
    uint32_t hundredths = ms < 9995 ? (ms + 5) / 10 : (ms + 50) / 100 * 10;
    char *s = number(out, hundredths / 100);
    *s++ = '.';
    *s++ = (char)('0' + hundredths / 10 % 10);
    if (hundredths < 1000)
        *s++ = (char)('0' + hundredths % 10);
    copy(s, " S");
}
/* "<steps> ST <time>" */
static char *cycle(char *out, uint32_t steps, uint32_t frames)
{
    char time[16];
    ld_time(time, frames);
    out = copy(number(out, steps), " ST ");
    return copy(out, time);
}
/* The crossfader the engine applies, 0 .. 127, from its float 0 .. 1. */
static uint32_t fader_of(uint32_t bits)
{
    if (bits & 0x80000000u)
        return 0;
    if (bits >= 0x3f800000u)
        return 127;
    uint32_t e = (bits >> 23) & 255, shift = 150 - e;
    if (shift >= 32)
        return 0;
    return (((bits & 0x7fffffu) | 0x800000u) * 127u) >> shift;
}
void ld_page(const uint32_t *st, uint32_t fresh, uint32_t xfad, LoopPage *p)
{
    uint32_t state = fresh ? st[LS_STATE] : 0;
    uint32_t loop = (state & LS_LOOP) != 0, armed = (state & LS_ARMED) != 0;
    uint32_t loop_steps = st[LS_STEPS] & 255, steps = (st[LS_STEPS] >> 8) & 255,
             step = (st[LS_STEPS] >> 16) & 255;
    char *s;
    copy(p->title, "OVERLOOP");
    copy(p->state, !fresh          ? "NO LINK"
                   : loop && armed ? "LOOP+ARM"
                   : loop          ? "LOOP"
                   : armed         ? "ARMED"
                                   : "IDLE");
    p->armed = armed;
    p->loop = loop;
    p->line1[0] = p->line2[0] = 0;
    if (armed) {
        /* what the next change keeps: the cycle of the pattern that plays, or the last part of it the buffer holds */
        copy(p->line1, "NEXT CHANGE KEEPS");
        if (!(state & LS_TRACK))
            /* stopped, or no whole step has been heard yet: a change now would take nothing, and the arm would stay */
            copy(p->line2, "NOTHING YET");
        else if (state & LS_NEXT_CUT) {
            char time[16];
            ld_time(time, st[LS_CAPACITY]);
            copy(copy(copy(p->line2, "LAST "), time), " ONLY");
        } else
            cycle(p->line2, steps, st[LS_NEXT_FRAMES]);
    } else if (loop) {
        s = copy(p->line1, "LOOP ");
        cycle(s, loop_steps, st[LS_LOOP_FRAMES]);
        if (state & LS_CUT)
            copy(p->line2, "CUT TO ITS LAST PART");
    } else if (fresh) {
        copy(p->line1, "NO LOOP");
        if (state & LS_TRACK)
            cycle(copy(p->line2, "CYCLE "), steps, st[LS_NEXT_FRAMES]);
    }
    /* the mark: the loop's phase, or the step of the pattern that plays */
    p->mark = LD_NO_MARK;
    if (loop && st[LS_LOOP_FRAMES])
        p->mark = (st[LS_PHASE] % st[LS_LOOP_FRAMES]) / ((st[LS_LOOP_FRAMES] + 999) / 1000);
    else if (fresh && steps)
        p->mark = step * 1000 / steps;
    if (p->mark != LD_NO_MARK && p->mark > 1000)
        p->mark = 1000;
    p->fader = loop ? fader_of(st[LS_GAIN]) : 0;
    copy(p->arm, armed ? "ARM ON" : "ARM OFF");
    s = copy(p->xfad, "XFAD ");
    if (xfad == 0)
        copy(s, "LIVE");
    else if (xfad >= LP_MAX)
        copy(s, "LOOP");
    else
        number(s, xfad);
}
static void box(const LoopCanvas *c, int x, int y, int w, int h, int ink)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > 128)
        w = 128 - x;
    if (y + h > 64)
        h = 64 - y;
    if (w > 0 && h > 0)
        c->box(c->context, x, y, w, h, ink);
}
static void text(const LoopCanvas *c, int x, int y, const char *s, int ink)
{
    if (*s)
        c->text(c->context, x, y, s, ink);
}
void ld_render_header(const LoopPage *p, const LoopCanvas *c)
{
    box(c, 0, 0, 128, 10, 0);
    text(c, 23, 2, p->title, 1);
    text(c, 104, 2, p->state, 1);
    box(c, 0, 9, 128, 1, 1);
}
void ld_render(const LoopPage *p, const LoopCanvas *c)
{
    box(c, 0, 0, 128, 64, 0);
    ld_render_header(p, c);
    text(c, 64, 12, p->line1, 1);
    text(c, 64, 20, p->line2, 1);
    /* the timeline: a cycle from end to end, with quarter marks and the mark of where it stands */
    box(c, 3, 33, 122, 1, 1);
    for (int i = 0; i <= 4; ++i)
        box(c, 3 + i * 121 / 4, 30, 1, 7, 1);
    if (p->mark != LD_NO_MARK) {
        int x = 3 + (int)(p->mark * 121 / 1000);
        box(c, x - 1, 28, 3, 11, 1);
    }
    /* the crossfader as it sounds */
    text(c, 12, 43, "LIVE", 1);
    text(c, 116, 43, "LOOP", 1);
    box(c, 26, 45, 76, 1, 1);
    box(c, 26 + (int)(p->fader * 73 / 127), 41, 3, 9, 1);
    box(c, 0, 52, 128, 1, 1);
    text(c, 32, 56, p->arm, p->armed ? 0 : 1);
    text(c, 96, 56, p->xfad, 1);
}
