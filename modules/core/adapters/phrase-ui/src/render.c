#include "render.h"
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
    c->text(c->context, x, y, s, ink);
}
static void number(const PhraseCanvas *c, int x, int y, uint32_t n)
{
    char b[16];
    pu_format(b, PH_P_TAKE, n);
    if (!n) {
        b[0] = '0';
        b[1] = 0;
    }
    text(c, x, y, b, 1);
}
static const char *state(const PhraseUi *s)
{
    if (!s->fresh)
        return "LINK LOST";
    if (s->status[PH_S_ROUTE_FAULT / 4])
        return "LIVE ERR";
    static const char *names[] = {"EMPTY", "FILL", "READY", "ARM", "REC", "HELD", "SWAP"};
    uint32_t e = s->status[PH_S_ERROR / 4];
    if (e == PH_ERROR_NO_INPUT)
        return "NO INPUT";
    if (e == PH_ERROR_HISTORY)
        return "FILL";
    if (e == PH_ERROR_CAPACITY)
        return "TOO LONG";
    if (e == PH_ERROR_TIMING)
        return "CUE ERR";
    if (e)
        return "ERROR";
    uint32_t st = s->status[PH_S_STATE / 4];
    return st <= PH_HANDOFF ? names[st] : "ERROR";
}
static int differs(const PhraseUi *s)
{
    return pu_effective_mask() != s->status[PH_S_HELD_MASK / 4];
}
static void fader(const PhraseUi *s, const PhraseCanvas *c)
{
    int32_t track = pu_gain_track(s);
    uint32_t value = dkrt_phrase_get(track >= 0 ? PH_P_GAIN0 + (uint32_t)track : PH_P_FADE);
    text(c, 10, 12, track >= 0 ? "GAIN" : "FADE", 1);
    text(c, 10, 57, track >= 0 ? "0" : "LIVE", 1);
    rect(c, 7, 29, 7, 24);
    box(c, 8, 30 + (int)(21 * (127 - value) / 127), 5, 2, 1);
    if (s->show_values || (s->focus_ticks && s->focus == PH_P_FADE) || track >= 0)
        number(c, 10, 21, value);
    else
        text(c, 10, 21, "FX", 1);
}
static void tile(const PhraseCanvas *c, int x, int y, uint32_t label, int held, int next)
{
    if (next)
        rect(c, x, y, 12, 12);
    if (held)
        box(c, x + 2, y + 2, 8, 8, 1);
    char b[4];
    b[0] = (char)('0' + label / 10);
    b[1] = (char)('0' + label % 10);
    b[2] = 0;
    text(c, x + 6, y + 3, label < 10 ? b + 1 : b, held ? 0 : 1);
}
static void inputs(const PhraseUi *s, const PhraseCanvas *c)
{
    uint32_t next = pu_effective_mask(), held = s->status[PH_S_HELD_MASK / 4];
    for (uint32_t i = 0; i < 16; ++i)
        tile(c, 23 + 13 * (int)(i % 8), 22 + 14 * (int)(i / 8), i + 1, (held >> i) & 1,
             (next >> i) & 1);
    static const char *const exts[] = {"OFF", "L", "R", "ST"};
    text(c, 34, 10, "EXT", 1);
    text(c, 60, 10, "GAIN", 1);
    text(c, 34, 16, exts[(dkrt_phrase_get(PH_P_MASK) >> 16) & 3], 1);
    number(c, 60, 16, dkrt_phrase_get(PH_P_EXT_GAIN));
    for (uint32_t i = 0; i < 2; ++i) {
        int x = 91 + 16 * (int)i;
        uint32_t mask = 1u << (16 + i);
        if (next & mask)
            rect(c, x, 10, 12, 11);
        if (held & mask)
            box(c, x + 2, 12, 8, 7, 1);
        text(c, x + 6, 13, i ? "R" : "L", (held & mask) ? 0 : 1);
    }
    box(c, 24, 52, 5, 5, 1);
    text(c, 43, 52, "HELD", 1);
    rect(c, 59, 52, 5, 5);
    text(c, 79, 52, "NEXT", 1);
    int32_t track = pu_gain_track(s);
    if (track >= 0) {
        text(c, 45, 59, "TRACK", 1);
        number(c, 67, 59, (uint32_t)track + 1);
        number(c, 103, 59, dkrt_phrase_get(PH_P_GAIN0 + (uint32_t)track));
    } else
        text(c, 75, 59,
             s->status[PH_S_STATE / 4] == PH_HANDOFF ? "BUSY"
             : s->status[PH_S_REC_ACTIVE / 4]        ? "YES CANCEL"
                                                     : "YES CAPTURE",
             1);
    if (s->status[PH_S_REC_ACTIVE / 4] || s->status[PH_S_STATE / 4] == PH_HANDOFF)
        text(c, 111, 52, "LOCK", 1);
}
static uint32_t normalize(uint32_t p, uint32_t v)
{
    if (p == PH_P_REC || p == PH_P_FROM)
        return v * 127;
    if (p == PH_P_RATE)
        return v * 127 / 12;
    if (p == PH_P_SHAP)
        return v * 127 / 3;
    if (p == PH_P_TUNE)
        return v * 127 / 96;
    if (p == PH_P_SCAN || p == PH_P_TAKE || p == PH_P_CUE)
        return v * 127 / 128;
    return v > 127 ? 127 : v;
}
static void crop(const PhraseUi *s, const PhraseCanvas *c, int x, int y, int width)
{
    uint32_t frames = s->status[PH_S_HELD_FRAMES / 4], start = s->status[PH_S_CROP_START / 4],
             len = s->status[PH_S_CROP_LEN / 4];
    rect(c, x, y, width, 9);
    if (frames && start < frames && len <= frames - start) {
        int lo = (int)(start * (uint32_t)(width - 2) / frames),
            n = (int)(len * (uint32_t)(width - 2) / frames);
        if (n < 1)
            n = 1;
        box(c, x + 1 + lo, y + 2, n, 5, 1);
    }
}
void pu_render_header(const PhraseUi *s, const PhraseCanvas *c)
{
    static const char *pages[] = {"LOOP", "GRAIN", "MOVE", "SPACE"};
    char title[7];
    const char *name = s->title ? s->title : "PHRASE";
    uint32_t title_len = 0;
    while (title_len < 6 && name[title_len]) {
        title[title_len] = name[title_len];
        ++title_len;
    }
    title[title_len] = 0;
    box(c, 0, 0, 128, 10, 0);
    text(c, 15, 1, title, 1);
    text(c, 49, 1, s->overlay ? "INPUT" : pages[s->page & 3], 1);
    text(c, 91, 1, state(s), 1);
    if (!s->overlay) {
        char p[4] = {(char)('1' + s->page), '/', '4', 0};
        text(c, 119, 1, p, 1);
    } else if (differs(s))
        text(c, 123, 1, "*", 1);
    box(c, 0, 9, 128, 1, 1);
}
void pu_render(const PhraseUi *s, const PhraseCanvas *c)
{
    static const char *labels[] = {"REC",  "FROM", "TAKE", "CUE",  "STRT", "LEN", "SIZE",
                                   "RATE", "TUNE", "SHAP", "SPRY", "JITR", "POS", "SCAN",
                                   "REVR", "WDTH", "DIFF", "DEC",  "HPF",  "LPF"};
    box(c, 0, 0, 128, 64, 0);
    pu_render_header(s, c);
    fader(s, c);
    if (s->overlay) {
        inputs(s, c);
        return;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t p = pu_parameter(s->page, i);
        int x = 33 + 26 * (int)(i % 4), y = i < 4 ? 12 : 39;
        if (p == 255)
            continue;
        uint32_t v = dkrt_phrase_get(p);
        char value[16];
        pu_format(value, p, v);
        if (p == PH_P_REC || p == PH_P_FROM || p == PH_P_RATE || p == PH_P_SHAP || p == PH_P_TAKE ||
            p == PH_P_CUE)
            text(c, x, y + 5, value, 1);
        else
            c->knob(c->context, i, normalize(p, v));
        text(c, x, y + 20,
             (s->show_values || (s->focus_ticks && s->focus == p)) ? value : labels[p], 1);
    }
    /* G/H never contain a second volume knob. H is always the IN action. */
    uint32_t mask = s->status[PH_S_HELD_MASK / 4];
    for (int k = 0; k < 16; ++k) {
        int x = 103 + 4 * (k % 4), y = 40 + 3 * (k / 4);
        if (mask & (1u << k))
            box(c, x, y, 3, 2, 1);
        else
            box(c, x + 1, y, 1, 1, 1);
    }
    text(c, 111, 59, differs(s) ? "IN*" : "IN", 1);
    for (int i = 0; i < 4; ++i)
        box(c, 125, 15 + 11 * i, 2, i == (int)s->page ? 7 : 2, 1);
    if (s->page == 0) {
        crop(s, c, 76, 42, 21);
        uint32_t tenths = s->status[PH_S_CROP_LEN / 4] / 4800;
        char b[16];
        pu_format(b, PH_P_EXT_GAIN, tenths / 10);
        uint32_t n = 0;
        while (b[n])
            ++n;
        b[n++] = '.';
        b[n++] = (char)('0' + tenths % 10);
        b[n] = 0;
        text(c, 85, 52, b, 1);
        text(c, 85, 59, "SEC", 1);
    } else if (s->page == 1) {
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 9; ++k)
                box(c, 75 + j * 4 + k, 48 - (k < 4 ? k : 8 - k), 1, 1, 1);
        number(c, 85, 52, s->status[PH_S_ACTIVE_GRAINS / 4]);
        text(c, 85, 59, s->status[PH_S_LIMIT / 4] ? "LIMIT" : "GRAINS", 1);
    } else if (s->page == 2) {
        crop(s, c, 24, 43, 73);
        uint32_t frames = s->status[PH_S_HELD_FRAMES / 4], start = s->status[PH_S_CROP_START / 4],
                 len = s->status[PH_S_CROP_LEN / 4];
        if (frames && start < frames && len && len <= frames - start) {
            uint32_t position = dkrt_phrase_get(PH_P_POS) * len / 127;
            int x = 25 + (int)(70 * (start + position) / frames);
            box(c, x, 39, 1, 4, 1);
            box(c, x - 1, 39, 3, 1, 1);
            /* The DSP reports its scan offset in float frames relative to
             * this crop. POS is separate and remains where the user set it. */
            union {
                uint32_t u;
                float f;
            } phase = {s->status[PH_S_SCAN_PHASE / 4]};
            if (s->fresh && phase.f >= 0 && phase.f < (float)len) {
                uint32_t head = position + (uint32_t)phase.f;
                if (head >= len)
                    head -= len;
                box(c, 25 + (int)(70 * (start + head) / frames), 43, 1, 9, 0);
            }
            if (s->fresh)
                for (uint32_t grain = 0; grain < 8; ++grain) {
                    if (!(s->status[PH_S_GRAIN_VALID / 4] & (1u << grain)))
                        continue;
                    uint32_t packed = s->status[PH_S_GRAIN_POS0 / 4 + grain / 2];
                    uint32_t normalized = (packed >> (16 * (grain & 1))) & 0xffff;
                    box(c, 25 + (int)(70 * normalized / 65535), 54, 1, 2, 1);
                }
        }
        uint32_t scan = dkrt_phrase_get(PH_P_SCAN);
        if (scan != 64)
            text(c, 93, 36, scan > 64 ? ">" : "<", 1);
        text(c, 59, 59, "CROP / HEAD", 1);
    } else {
        int high = 30 + (int)(14 * dkrt_phrase_get(PH_P_HPF) / 127);
        int low = 75 + (int)(14 * dkrt_phrase_get(PH_P_LPF) / 127);
        box(c, 25, 44, high - 28, 1, 1);
        for (int i = 0; i < 4; ++i)
            box(c, high - 3 + i, 43 - i, 1, 1, 1);
        box(c, high, 40, 48 - high, 1, 1);
        box(c, 72, 40, low - 71, 1, 1);
        for (int i = 1; i < 5; ++i)
            box(c, low + i, 40 + i, 1, 1, 1);
        box(c, low + 5, 44, 96 - low - 5, 1, 1);
        uint32_t diffuse = dkrt_phrase_get(PH_P_DIFF);
        int count = 4 + (int)(12 * dkrt_phrase_get(PH_P_DEC) / 127);
        for (int i = 0; i < count; ++i) {
            int h = (count - i) * 8 / count + 1;
            box(c, 25 + 4 * i, 55 - h, diffuse >= 64 ? 2 : 1, h, 1);
            if (diffuse && i)
                box(c, 23 + 4 * i, 56 - h / 2, 1, h / 2, 1);
        }
        text(c, 59, 59, "GRAINS > TAIL", 1);
    }
}
