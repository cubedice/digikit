#include "render.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
static void put(char out[8], const char *s)
{
    uint32_t i = 0;
    for (; i < 7 && s[i]; ++i)
        out[i] = s[i];
    for (; i < 8; ++i)
        out[i] = 0;
}
static void cell(PhraseCell *c, const char *label, const char *value, const char *state,
                 uint32_t lit)
{
    put(c->label, label);
    put(c->value, value);
    put(c->state, state);
    c->lit = lit;
    c->fader = 0;
}
static char *copy(char *out, const char *s)
{
    while (*s)
        *out++ = *s++;
    *out = 0;
    return out;
}
/* The crossfader the loop effect applies, 0 .. 127, from its float 0 .. 1 (LS_GAIN). */
static uint32_t heard(uint32_t bits)
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
/* The tracks the effect hears as a cell's text: "5 TRK", or "NO TRK". */
static void tracks_text(char out[16])
{
    uint32_t n = pu_track_count();
    if (!n) {
        copy(out, "NO TRK");
        return;
    }
    pu_format(out, PH_P_MIX, n);
    while (*out)
        ++out;
    copy(out, " TRK");
}
/* G and H, and the header's loop line: the loop effect's state as the page shows it. */
static void loop_cells(const PhraseUi *s, PhrasePage *page)
{
    char text[16], time[16];
    const uint32_t *st = s->loop_status;
    uint32_t state = s->loop_fresh ? st[LS_STATE] : 0, loop = pu_has_loop(s),
             armed = (state & LS_ARMED) != 0, inpt = dkrt_phrase_get(PH_P_INPT),
             selecting = pu_selecting(s);
    page->tracks = pu_tracks();
    page->loop_input = inpt && !selecting;
    page->loop_ready = loop;
    /* INPT and under it the tracks it hears at LIVE, or SILENT at LOOP while there is no loop to hear; while G is held the
     * tracks, and TRIGS inverted: the TRIG keys choose them */
    tracks_text(text);
    if (selecting)
        cell(&page->cells[6], "INPT", text, "TRIGS", 1);
    else
        cell(&page->cells[6], "INPT", inpt ? "LOOP" : "LIVE", inpt && !loop ? "SILENT" : text, 0);
    page->line[0] = 0;
    if (!loop) {
        if (armed) {
            /* what the next change keeps: the steps of the pattern that plays, CUT when the buffers hold less, WAIT before a
             * whole step was heard */
            const char *keeps = "WAIT";
            if (state & LS_NEXT_CUT)
                keeps = "CUT";
            else if (state & LS_TRACK) {
                pu_format(text, PH_P_MIX, (st[LS_STEPS] >> 8) & 255);
                copy(copy(time, text), " ST");
                keeps = time;
            }
            cell(&page->cells[7], "XFAD", "ARMD", keeps, 0);
        } else
            cell(&page->cells[7], "XFAD", "--", "NO LOOP", 0);
        return;
    }
    uint32_t xfad = s->loop->get(LP_XFAD);
    if (xfad == 0)
        copy(text, "LIVE");
    else if (xfad >= LP_MAX)
        copy(text, "LOOP");
    else
        pu_format(text, PH_P_MIX, xfad);
    cell(&page->cells[7], "XFAD", text, armed ? "ARMD" : "", armed);
    page->cells[7].fader = 1 + heard(st[LS_GAIN]);
    if (inpt) {
        /* PRISM hears the loop: the header names it, its steps and its time */
        char *o = copy(page->line, "LOOP ");
        pu_format(text, PH_P_MIX, st[LS_STEPS] & 255);
        o = copy(copy(o, text), " ST ");
        pu_time(time, st[LS_LOOP_FRAMES]);
        copy(o, time);
    }
}
void pu_page(const PhraseUi *s, PhrasePage *page)
{
    char text[16], time[16];
    uint32_t zone = dkrt_phrase_get(PH_P_ZONE), sync = dkrt_phrase_get(PH_P_SYNC),
             flip = dkrt_phrase_get(PH_P_FLIP), hold = dkrt_phrase_get(PH_P_HOLD),
             repeats = dkrt_phrase_get(PH_P_RPTS), colour = dkrt_phrase_get(PH_P_COLR),
             halo = dkrt_phrase_get(PH_P_HALO), skew = dkrt_phrase_get(PH_P_SKEW),
             frames = pu_frames(s);
    put(page->title, s->title ? s->title : "PHRASE");
    put(page->zone, pu_zone_name(zone));
    /* The time the engine plays now: it glides after a free RATE move. Without a status there is none to show. */
    if (frames)
        pu_time(time, frames);
    else
        put(time, s->fresh && !s->status[PH_S_ERROR / 4] ? "--" : "NO LINK");
    put(page->time, time);
    page->mix = dkrt_phrase_get(PH_P_MIX);
    page->zone_index = zone;
    page->repeats = hold ? PH_RPTS_UNITY : repeats;
    page->colour = colour;
    page->halo = halo;
    page->skew = skew;
    cell(&page->cells[0], "ZONE", pu_zone_name(zone), flip ? "FLIP" : "", flip);
    if (sync) {
        /* the ratio of the zone's 1/1; the header has the time it gives */
        uint32_t ratio = dkrt_phrase_get(PH_P_RATIO);
        page->rate = ratio * 127 / (PH_SYNC_RATIOS - 1);
        pu_format(text, PH_P_RATIO, ratio);
        cell(&page->cells[1], "RATE", text, "SYNC", 0);
    } else {
        page->rate = dkrt_phrase_get(PH_P_RATE) / PH_RATE_COARSE;
        cell(&page->cells[1], "RATE", frames ? time : "--", "FREE", 0);
    }
    pu_format(text, PH_P_RPTS, repeats);
    cell(&page->cells[2], "RPTS", hold ? "INF" : text, hold ? "HOLD" : "", hold);
    pu_format(text, PH_P_COLR, colour);
    cell(&page->cells[3], "COLR", text, colour < 64 ? "DARK" : colour > 64 ? "BRIT" : "", 0);
    pu_format(text, PH_P_HALO, halo);
    cell(&page->cells[4], "HALO", text, "", 0);
    pu_format(text, PH_P_SKEW, skew);
    cell(&page->cells[5], "SKEW", text, "", 0);
    /* The input switch and Overloop's crossfader. */
    loop_cells(s, page);
}
static void box(const PhraseCanvas *c, int x, int y, int w, int h, int ink)
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
static void rect(const PhraseCanvas *c, int x, int y, int w, int h)
{
    box(c, x, y, w, 1, 1);
    box(c, x, y + h - 1, w, 1, 1);
    box(c, x, y, 1, h, 1);
    box(c, x + w - 1, y, 1, h, 1);
}
static void text(const PhraseCanvas *c, int x, int y, const char *s, int ink)
{
    if (*s)
        c->text(c->context, x, y, s, ink);
}
static void number(char out[8], uint32_t n)
{
    char t[16];
    pu_format(t, PH_P_MIX, n);
    put(out, t);
}

/* Seven scanlines per icon, with a pixel of breathing room on either side of the text. */
static void icon(const PhrasePage *p, const PhraseCanvas *c, int i, int x, int y)
{
    switch (i) {
    case 0: /* Zone window: eight progressively longer ranges. */
        for (int z = 0; z < PH_ZONES; ++z) {
            int active = (uint32_t)z == p->zone_index;
            int height = 2 + z / 2 + 2 * active;
            box(c, x - 11 + 3 * z, y + 7 - height, 1 + active, height, 1);
        }
        break;
    case 1: { /* Longer times space the pulses farther apart. */
        int gap = 3 + (p->rate * 7 + 63) / 127;
        box(c, x - 10, y + 5, 22, 1, 1);
        for (int j = 0; j < 3; ++j)
            box(c, x - 10 + gap * j, y + 1, 2, 4, 1);
        break;
    }
    case 2: { /* Echoes decay, sustain at unity or HOLD, and grow above unity. */
        int level = 5 * 16;
        for (int j = 0; j < 5; ++j) {
            int height = (level + 8) / 16;
            if (height > 7)
                height = 7;
            box(c, x - 11 + 5 * j, y + 7 - height, 2, height, 1);
            level = level * p->repeats / PH_RPTS_UNITY;
        }
        break;
    }
    case 3: { /* Feedback spectrum tilts from dark to bright. */
        int tilt = (p->colour * 6 + 63) / 127 - 3;
        for (int j = 0; j < 5; ++j) {
            int height = 4 + tilt * (j - 2) / 2;
            box(c, x - 11 + 5 * j, y + 7 - height, 3, height, 1);
        }
        break;
    }
    case 4: { /* Halo spreads out from the original pulse. */
        int spread = (p->halo * 9 + 63) / 127;
        box(c, x - 1, y + 2, 3, 3, 1);
        if (spread) {
            box(c, x - 2 - spread, y + 1, 1, 5, 1);
            box(c, x + 2 + spread, y + 1, 1, 5, 1);
            box(c, x - 1 - spread, y, 2, 1, 1);
            box(c, x + spread, y + 6, 2, 1, 1);
        }
        break;
    }
    case 5: { /* Left and right delay lengths diverge about the centre. */
        int spread = (p->skew * 16 + 63) / 127 - 8;
        box(c, x - 10, y + 1, 11 - spread, 2, 1);
        box(c, x - 10, y + 4, 11 + spread, 2, 1);
        break;
    }
    case 6:
        if (p->loop_input) {
            /* Loop route: a closed path when it has material, open otherwise. */
            box(c, x - 6, y + 1, 13, 1, 1);
            box(c, x + 6, y + 1, 1, 5, 1);
            box(c, x - 6, y + 5, 13, 1, 1);
            if (p->loop_ready)
                box(c, x - 6, y + 1, 1, 5, 1);
            box(c, x - 3, y + 2, 1, 1, 1);
        } else {
            /* TRIG 1–8 above 9–16, filled cells are routed. */
            for (int t = 0; t < 16; ++t) {
                int selected = (p->tracks >> t) & 1;
                box(c, x - 11 + 3 * (t % 8), y + 1 + 3 * (t / 8), 1 + selected, 1 + selected, 1);
            }
        }
        break;
    case 7: { /* The slider follows the applied crossfade during a transition. */
        uint32_t fader = p->cells[7].fader;
        box(c, x - 11, y + 3, 23, 1, 1);
        box(c, x - 11, y + 1, 1, 5, 1);
        box(c, x + 11, y + 1, 1, 5, 1);
        if (fader)
            box(c, x - 11 + (int)(fader - 1) * 20 / 127, y, 3, 7, 1);
        break;
    }
    }
}

void pu_render_header(const PhrasePage *p, const PhraseCanvas *c)
{
    box(c, 0, 0, 128, 10, 0);
    text(c, 15, 2, p->title, 1);
    if (p->line[0])
        text(c, 82, 2, p->line, 1);
    else {
        text(c, 78, 2, p->zone, 1);
        text(c, 110, 2, p->time, 1);
    }
    box(c, 0, 9, 128, 1, 1);
}
/* Below the header the page leaves x 123 to 127 to the FX view, which draws its page indicator there (x 125 to 127). */
void pu_render(const PhrasePage *p, const PhraseCanvas *c)
{
    char mix[8];
    box(c, 0, 0, 124, 64, 0);
    pu_render_header(p, c);
    /* LEVEL: MIX, a column that fills from the bottom. */
    text(c, 10, 11, "MIX", 1);
    rect(c, 6, 19, 9, 36);
    int fill = (int)(p->mix > 127 ? 127 : p->mix) * 32 / 127;
    box(c, 8, 21 + 32 - fill, 5, fill, 1);
    number(mix, p->mix);
    text(c, 10, 57, mix, 1);
    /* Row 37 stays blank in the grid so inverted states have a clear edge. */
    for (uint32_t i = 0; i < 8; ++i) {
        const PhraseCell *k = &p->cells[i];
        int x = 32 + 26 * (int)(i % 4), y = i < 4 ? 11 : 38; /* the last column ends at x 124 */
        text(c, x, y, k->label, 1);
        icon(p, c, (int)i, x, y + 6);
        /* Leave a gap between the zone name and a wide rate value. */
        int value_x = x + (i == 1) - (i == 0);
        text(c, value_x, y + 14, k->value, 1);
        /* Leave room between the INPT and XFAD states when both are wide. */
        text(c, i == 6 ? x - 7 : x, y + 20, k->state, k->lit ? 0 : 1);
    }
}
