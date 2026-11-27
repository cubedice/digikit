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
void ld_observe(LoopUi *s, const uint32_t *st, uint32_t fresh)
{
    if (!s->arm_asked)
        return;
    uint32_t armed = fresh && (st[LS_STATE] & LS_ARMED);
    if (!fresh || armed == s->arm_asked - 1 || st[LS_HEARTBEAT] - s->arm_beat >= LD_ARM_SETTLE)
        s->arm_asked = 0;
}
/* ARM as the page counts it: the state it asked for while the status does not show it yet, else the engine's status
 * (without a fresh one the engine is taken as not armed). BEAT: the heartbeat of the status. */
static uint32_t armed_now(LoopUi *s, uint32_t *beat)
{
    uint32_t st[LS_WORDS];
    uint32_t fresh = dkrt_loop_status(st);
    ld_observe(s, st, fresh);
    *beat = st[LS_HEARTBEAT];
    if (s->arm_asked)
        return s->arm_asked - 1;
    return fresh && (st[LS_STATE] & LS_ARMED);
}
/* ARM to ON: the home's counter moves once when the engine is not there yet (each change of it toggles the engine). */
static void arm_to(LoopUi *s, uint32_t on)
{
    uint32_t beat;
    if (armed_now(s, &beat) == on)
        return;
    dkrt_loop_arm();
    s->arm_asked = 1 + on;
    s->arm_beat = beat;
    ++s->revision;
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
        /* E and H: ARM, or the arm taken back */
        if (key == LD_PUSH + LD_E || key == LD_PUSH + LD_H) {
            uint32_t beat;
            arm_to(s, !armed_now(s, &beat));
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
    if (encoder == LD_E && delta)
        arm_to(s, delta > 0);
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
    p->link_error = !fresh;
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
    /* with a loop, the crossfader as the engine applies it; without one, where XFAD stands (the engine plays LIVE then) */
    p->fader = loop ? fader_of(st[LS_GAIN]) : (xfad > LP_MAX ? LP_MAX : xfad);
    copy(p->arm, armed ? "ON" : "OFF");
    if (xfad == 0)
        copy(p->xfad, "LIVE");
    else if (xfad >= LP_MAX)
        copy(p->xfad, "LOOP");
    else
        number(p->xfad, xfad);
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
/* The stock waveform's end marker: dotted on every second row, with a two-pixel flag on the second row that points into
 * the waveform (DIR 1 at the start, -1 at the end). */
static void marker(const LoopCanvas *c, int x, int dir)
{
    for (int y = LD_WAVE_TOP; y <= LD_WAVE_BOTTOM; y += 2)
        box(c, x, y, 1, 1, 1);
    box(c, dir > 0 ? x : x - 1, LD_WAVE_TOP + 1, 2, 1, 1);
}
/* The stock placeholder of a cell without a control: a dotted square, 17 pixels a side, in the bottom row. */
static void placeholder(const LoopCanvas *c, int x)
{
    for (int i = -7; i <= 7; i += 2) {
        box(c, x + i, 40, 1, 1, 1);
        box(c, x + i, 56, 1, 1, 1);
    }
    for (int y = 41; y <= 55; y += 2) {
        box(c, x - 8, y, 1, 1, 1);
        box(c, x + 8, y, 1, 1, 1);
    }
}
/* ARM's graphic: a ring, filled at its centre while ARM is on. */
static void arm_icon(const LoopCanvas *c, int x, int armed)
{
    static const uint16_t ring[] = {0x0f8, 0x104, 0x202, 0x401, 0x401, 0x401,
                                    0x401, 0x401, 0x202, 0x104, 0x0f8};
    for (int row = 0; row < 11; ++row)
        for (int bits = ring[row], px = x - 5; bits; bits >>= 1, ++px)
            if (bits & 1)
                box(c, px, 42 + row, 1, 1, 1);
    if (armed)
        box(c, x - 2, 45, 5, 5, 1);
}
/* The crossfader's graphic, as on PRISM's page: a rail between two end stops, its knob where the fader stands. */
static void fader_icon(const LoopCanvas *c, int x, uint32_t fader)
{
    box(c, x - 11, 48, 23, 1, 1);
    box(c, x - 11, 44, 1, 9, 1);
    box(c, x + 11, 44, 1, 9, 1);
    box(c, x - 11 + (int)(fader * 20 / 127), 42, 3, 13, 1);
}
/* The stock FX view keeps the header, the effect's tab and the page indicator (x 125 to 127); the page paints the body,
 * x 16 to 123 below the header, and clears the level strip's place under the tab (LEVEL turns XFAD here, there is no
 * level to show). */
void ld_render(const LoopPage *p, const LoopCanvas *c, int show_values, uint32_t focus)
{
    box(c, 16, 10, 108, 54, 0);
    box(c, 0, 38, 16, 26, 0);
    uint16_t wave[LS_WAVE_COLUMNS];
    uint32_t write_column = dkrt_loop_wave_peaks(wave);
    /* the engine's columns merged into the plot's, the largest peak of each side */
    uint16_t up[LD_WAVE_PLOT], down[LD_WAVE_PLOT];
    for (int x = 0; x < LD_WAVE_PLOT; ++x)
        up[x] = down[x] = 0;
    unsigned reference = 32; /* limit the visual gain for very quiet material */
    for (int s = 0; s < LS_WAVE_COLUMNS; ++s) {
        int x = s * LD_WAVE_PLOT / LS_WAVE_COLUMNS;
        unsigned d = wave[s] & 255, u = wave[s] >> 8;
        if (d > down[x])
            down[x] = (uint16_t)d;
        if (u > up[x])
            up[x] = (uint16_t)u;
        if (d > reference)
            reference = d;
        if (u > reference)
            reference = u;
    }
    const int reach = LD_WAVE_MIDDLE - LD_WAVE_TOP - 1;
    unsigned gain = ((unsigned)reach << 8) / reference;
    for (int x = 0; x < LD_WAVE_PLOT; ++x) {
        int h_up = (int)((up[x] * gain + 255) >> 8), h_down = (int)((down[x] * gain + 255) >> 8);
        box(c, LD_WAVE_LEFT + 1 + x, LD_WAVE_MIDDLE - h_up, 1, 1 + h_up + h_down, 1);
    }
    box(c, LD_WAVE_LEFT, LD_WAVE_MIDDLE, LD_WAVE_RIGHT - LD_WAVE_LEFT + 1, 1, 1);
    marker(c, LD_WAVE_LEFT, 1);
    marker(c, LD_WAVE_RIGHT, -1);
    /* The retained loop's phase has ticks at the edges; the live write cursor is dashed through the plot. */
    if (p->loop && p->mark != LD_NO_MARK) {
        int x = LD_WAVE_LEFT + 1 + (int)(p->mark * (LD_WAVE_PLOT - 1) / 1000);
        box(c, x - 1, LD_WAVE_TOP, 3, 1, 1);
        box(c, x, LD_WAVE_TOP + 1, 1, 2, 1);
        box(c, x, LD_WAVE_BOTTOM - 1, 1, 2, 1);
    } else if (!p->loop && write_column < LS_WAVE_COLUMNS) {
        int x = LD_WAVE_LEFT + 1 + (int)(write_column * LD_WAVE_PLOT / LS_WAVE_COLUMNS);
        for (int y = LD_WAVE_TOP; y < LD_WAVE_BOTTOM; y += 4)
            box(c, x, y, 1, 2, 1);
    }
    /* what the loop does: NO LINK while the engine does not report */
    text(c, (LD_WAVE_LEFT + LD_WAVE_RIGHT) / 2, LD_LINE_Y,
         p->link_error ? p->state
         : *p->line2   ? p->line2
                       : p->line1,
         1);
    arm_icon(c, LD_CELL_E, (int)p->armed);
    placeholder(c, LD_CELL_F);
    placeholder(c, LD_CELL_G);
    fader_icon(c, LD_CELL_H, p->fader);
    /* the captions: ARM (ARMD inverted while it is on) and XFAD, or their values */
    if (show_values || ((focus >> LD_E) & 1))
        text(c, LD_CELL_E, 59, p->arm, 1);
    else
        text(c, LD_CELL_E, 59, p->armed ? "ARMD" : "ARM", p->armed ? 0 : 1);
    text(c, LD_CELL_H, 59, show_values || ((focus >> LD_H) & 1) ? p->xfad : "XFAD", 1);
}
