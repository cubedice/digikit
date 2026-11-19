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
}
void pu_page(const PhraseUi *s, PhrasePage *page)
{
    char text[16], time[16];
    uint32_t zone = dkrt_phrase_get(PH_P_ZONE), sync = dkrt_phrase_get(PH_P_SYNC),
             flip = dkrt_phrase_get(PH_P_FLIP), hold = dkrt_phrase_get(PH_P_HOLD),
             colour = dkrt_phrase_get(PH_P_COLR), frames = pu_frames(s);
    put(page->title, s->title ? s->title : "PHRASE");
    put(page->zone, pu_zone_name(zone));
    /* The time the engine plays now: it glides after a free RATE move. Without a status there is none to show. */
    if (frames)
        pu_time(time, frames);
    else
        put(time, s->fresh ? "--" : "NO LINK");
    put(page->time, time);
    page->mix = dkrt_phrase_get(PH_P_MIX);
    page->zone_index = zone;
    cell(&page->cells[0], "ZONE", pu_zone_name(zone), flip ? "FLIP" : "", flip);
    if (sync) {
        pu_format(text, PH_P_NOTE, dkrt_phrase_get(PH_P_NOTE));
        cell(&page->cells[1], "RATE", text, "SYNC", 0);
    } else
        cell(&page->cells[1], "RATE", frames ? time : "--", "FREE", 0);
    pu_format(text, PH_P_RPTS, dkrt_phrase_get(PH_P_RPTS));
    cell(&page->cells[2], "RPTS", hold ? "INF" : text, hold ? "HOLD" : "", hold);
    pu_format(text, PH_P_COLR, colour);
    cell(&page->cells[3], "COLR", text, colour < 64 ? "DARK" : colour > 64 ? "BRIT" : "", 0);
    pu_format(text, PH_P_HALO, dkrt_phrase_get(PH_P_HALO));
    cell(&page->cells[4], "HALO", text, "", 0);
    pu_format(text, PH_P_SKEW, dkrt_phrase_get(PH_P_SKEW));
    cell(&page->cells[5], "SKEW", text, "", 0);
    /* Kept for the loop: the input switch and Overloop's crossfader. Nothing moves them yet. */
    cell(&page->cells[6], "INPT", "LIVE", "", 0);
    cell(&page->cells[7], "XFAD", "--", "NO LOOP", 0);
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
void pu_render_header(const PhrasePage *p, const PhraseCanvas *c)
{
    box(c, 0, 0, 128, 10, 0);
    text(c, 15, 2, p->title, 1);
    text(c, 78, 2, p->zone, 1);
    text(c, 110, 2, p->time, 1);
    box(c, 0, 9, 128, 1, 1);
}
void pu_render(const PhrasePage *p, const PhraseCanvas *c)
{
    char mix[8];
    box(c, 0, 0, 128, 64, 0);
    pu_render_header(p, c);
    /* LEVEL: MIX, a column that fills from the bottom. */
    text(c, 10, 12, "MIX", 1);
    rect(c, 6, 19, 9, 36);
    int fill = (int)(p->mix > 127 ? 127 : p->mix) * 32 / 127;
    box(c, 8, 21 + 32 - fill, 5, fill, 1);
    number(mix, p->mix);
    text(c, 10, 57, mix, 1);
    box(c, 20, 37, 108, 1, 1);
    for (uint32_t i = 0; i < 8; ++i) {
        const PhraseCell *k = &p->cells[i];
        int x = 33 + 26 * (int)(i % 4), y = i < 4 ? 11 : 38;
        text(c, x, y + 1, k->label, 1);
        text(c, x, y + 9, k->value, 1);
        if (i == 0)
            /* ZONE: eight marks under the zone's name, the chosen one tall. */
            for (int z = 0; z < PH_ZONES; ++z) {
                int on = (uint32_t)z == p->zone_index;
                box(c, x - 12 + 3 * z, y + (on ? 15 : 16), 2, on ? 2 : 1, 1);
            }
        text(c, x, y + 19, k->state, k->lit ? 0 : 1);
    }
}
