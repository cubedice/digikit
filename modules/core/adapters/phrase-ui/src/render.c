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
    /* Retain the last held loop through the same heartbeat grace as the input route. */
    uint32_t state = s->loop && (s->loop_fresh || s->loop_had) ? st[LS_STATE] : 0,
             loop = state & LS_LOOP, armed = (state & LS_ARMED) != 0,
             inpt = dkrt_phrase_get(PH_P_INPT), selecting = pu_selecting(s);
    page->tracks = pu_tracks();
    page->loop_input = inpt && !selecting;
    page->loop_ready = loop;
    page->armed = armed;
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
    page->link_error = !frames && (!s->fresh || s->status[PH_S_ERROR / 4]);
    if (frames)
        pu_time(time, frames);
    else
        put(time, page->link_error ? "NO LINK" : "--");
    put(page->time, time);
    page->mix = dkrt_phrase_get(PH_P_MIX);
    page->zone_index = zone;
    page->sync = sync;
    page->repeats = hold ? PH_RPTS_UNITY : repeats;
    page->colour = colour;
    page->halo = halo;
    page->skew = skew;
    cell(&page->cells[0], "ZONE", pu_zone_name(zone), flip ? "FLIP" : "", flip);
    if (sync) {
        /* the ratio of the zone's 1/1; the header has the time it gives */
        pu_format(text, PH_P_RATIO, dkrt_phrase_get(PH_P_RATIO));
        cell(&page->cells[1], "RATE", text, "SYNC", 0);
    } else
        cell(&page->cells[1], "RATE", frames ? time : "--", "FREE", 0);
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

/* Seven rows with the low bit at the left edge. */
static void glyph(const PhraseCanvas *c, int x, int y, const uint16_t rows[7])
{
    for (int row = 0; row < 7; ++row)
        for (int bits = rows[row], px = x; bits; bits >>= 1, ++px)
            if (bits & 1)
                box(c, px, y + row, 1, 1, 1);
}

/* Fifteen scanlines for the graphic, with the caption below. */
static void icon(const PhrasePage *p, const PhraseCanvas *c, int i, int x, int y)
{
    switch (i) {
    case 0: /* Zone window: eight progressively longer ranges. */
        for (int z = 0; z < PH_ZONES; ++z) {
            int active = (uint32_t)z == p->zone_index;
            int height = 4 + z + 3 * active;
            box(c, x - 11 + 3 * z, y + 15 - height, 1 + active, height, 1);
        }
        break;
    case 1: /* Live delay time or the selected tempo ratio. */
        text(c, x, y + 5, p->cells[1].value, 1);
        break;
    case 2: { /* Stepped echoes lead to the infinity glyph at nominal unity. */
        static const uint16_t infinity[] = {0x306, 0x489, 0x451, 0x421, 0x451, 0x489, 0x306};
        if (p->repeats >= PH_RPTS_UNITY) {
            glyph(c, x - 5, y + 4, infinity);
        } else {
            int dashes = p->repeats * 5 / PH_RPTS_UNITY;
            int left = x - dashes * 5 / 2 - 1;
            for (int j = 0; j <= dashes; ++j)
                box(c, left + 5 * j, y + 6, 3, 2, 1);
        }
        break;
    }
    case 3: { /* Filled on the dark side, outlined from the centre towards bright. */
        int radius = 1 + (p->colour * 6 + 63) / 127, size = 2 * radius + 1;
        int left = x - radius, top = y + 7 - radius;
        box(c, left, top, size, size, 1);
        if (p->colour >= 64)
            box(c, left + 1, top + 1, size - 2, size - 2, 0);
        break;
    }
    case 4: { /* Overlapping rings spread out in the order they appear. */
        static const uint16_t ring[] = {0x1c, 0x22, 0x41, 0x41, 0x41, 0x22, 0x1c};
        static const int8_t offsets[][2] = {{0, 0}, {-4, -2}, {4, 2}, {-8, 2}, {8, -2}};
        int extra = (p->halo * 4 + 63) / 127;
        for (int j = 0; j <= extra; ++j)
            glyph(c, x - 3 + offsets[j][0], y + 4 + offsets[j][1], ring);
        break;
    }
    case 5: { /* Left and right delay lengths diverge about the centre. */
        int spread = (p->skew * 16 + 63) / 127 - 8;
        box(c, x - 10, y + 3, 11 - spread, 3, 1);
        box(c, x - 10, y + 9, 11 + spread, 3, 1);
        break;
    }
    case 6:
        if (p->loop_input) {
            /* Loop route: a closed path when it has material, open otherwise. */
            box(c, x - 6, y + 2, 13, 1, 1);
            box(c, x + 6, y + 2, 1, 11, 1);
            box(c, x - 6, y + 12, 13, 1, 1);
            if (p->loop_ready)
                box(c, x - 6, y + 2, 1, 11, 1);
            box(c, x - 3, y + 3, 1, 1, 1);
        } else {
            /* TRIG 1–8 above 9–16, filled cells are routed. */
            for (int t = 0; t < 16; ++t) {
                int selected = (p->tracks >> t) & 1;
                box(c, x - 11 + 3 * (t % 8), y + 3 + 6 * (t / 8), 1 + selected, 1 + 2 * selected,
                    1);
            }
        }
        break;
    case 7: { /* The slider follows the applied crossfade during a transition. */
        uint32_t fader = p->cells[7].fader;
        box(c, x - 11, y + 7, 23, 1, 1);
        box(c, x - 11, y + 3, 1, 9, 1);
        box(c, x + 11, y + 3, 1, 9, 1);
        if (fader)
            box(c, x - 11 + (int)(fader - 1) * 20 / 127, y + 1, 3, 13, 1);
        break;
    }
    }
}

void pu_render_header(const PhrasePage *p, const PhraseCanvas *c, int show_values)
{
    box(c, 0, 0, 128, 10, 0);
    text(c, 15, 2, p->title, 1);
    text(c, 42, 2, p->cells[1].state, 1);
    const PhraseCell *input = &p->cells[6];
    if (show_values && p->line[0] && !input->lit)
        text(c, 90, 2, p->line, 1);
    else {
        const char *route = input->state;
        if (!input->lit) {
            if (p->link_error)
                route = "NO LINK";
            else if (show_values && p->sync)
                route = p->time;
            else if (p->loop_input ? p->loop_ready : p->tracks)
                route = input->value;
        }
        text(c, 76, 2, route, !input->lit);
        if (!p->loop_ready && !p->armed)
            text(c, 111, 2, "NO LOOP", 1);
    }
    box(c, 0, 9, 128, 1, 1);
}
/* Below the header the page leaves x 123 to 127 to the FX view, which draws its page indicator there (x 125 to 127). */
void pu_render(const PhrasePage *p, const PhraseCanvas *c, int show_values)
{
    char mix[8];
    box(c, 0, 0, 124, 64, 0);
    pu_render_header(p, c, show_values);
    if (p->cells[0].lit)
        text(c, 9, 14, "FLIP", 0);
    if (p->cells[2].lit)
        text(c, 9, 22, "HOLD", 0);
    if (p->armed)
        text(c, 9, 30, "ARMD", 0);
    /* The compact send FX level strip has its caption on the bottom row. */
    box(c, 4, 40, 7, 1, 1);
    box(c, 4, 56, 7, 1, 1);
    box(c, 3, 41, 1, 15, 1);
    box(c, 11, 41, 1, 15, 1);
    for (int y = 42; y <= 54; y += 2) {
        box(c, 5, y, 1, 1, 1);
        box(c, 9, y, 1, 1, 1);
    }
    uint32_t level = p->mix > 127 ? 127 : p->mix;
    int fill = level ? 1 + (int)((level - 1) * 12 / 126) : 0;
    box(c, 5, 55 - fill, 5, fill, 1);
    if (show_values)
        number(mix, p->mix);
    text(c, 7, 59, show_values ? mix : "MIX", 1);
    for (uint32_t i = 0; i < 8; ++i) {
        const PhraseCell *k = &p->cells[i];
        int x = 32 + 26 * (int)(i % 4), y = i < 4 ? 13 : 40;
        icon(p, c, (int)i, x, y + 1);
        const char *caption = k->label;
        char tone[8];
        if (show_values && i != 1) {
            caption = k->value;
            if (i == 3 && p->colour != 64) {
                tone[0] = p->colour < 64 ? '-' : '+';
                copy(tone + 1, k->value);
                caption = tone;
            } else if (i == 7 && p->armed && !p->loop_ready) {
                caption = k->state;
            }
        }
        text(c, x, y + 19, caption, 1);
    }
}
