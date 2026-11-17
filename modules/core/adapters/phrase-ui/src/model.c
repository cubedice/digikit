#include "model.h"
/* Keep generated branch data out of validated executable sections. This
 * freestanding module has no libc, so leave small clear loops as stores. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
static int held(const PhraseUi *s, uint32_t k)
{
    return (s->held[k >> 5] >> (k & 31)) & 1;
}
static int owned(const PhraseUi *s, uint32_t k)
{
    return (s->owned[k >> 5] >> (k & 31)) & 1;
}
static void mark(uint32_t b[2], uint32_t k, int on)
{
    if (on)
        b[k >> 5] |= 1u << (k & 31);
    else
        b[k >> 5] &= ~(1u << (k & 31));
}
static int locked(const PhraseUi *s)
{
    return s->status[PH_S_STATE / 4] == PH_ARMED || s->status[PH_S_STATE / 4] == PH_RECORDING ||
           s->status[PH_S_STATE / 4] == PH_HANDOFF;
}
void pu_leave(PhraseUi *s)
{
    s->overlay = 0;
    s->adjusted = 0xffff;
    for (uint32_t i = 0; i < 2; ++i)
        s->cancelled[i] |= s->owned[i];
    for (uint32_t i = 0; i < 9; ++i)
        s->detent[i] = 0;
    ++s->revision;
}
uint32_t pu_native_flags(uint32_t flags)
{
    /* Native key-up has bit0 clear; bit4 identifies the release/click event.
     * Bit1 notifications may accompany a held FUNC chord, not a release. */
    if (flags & 16)
        return PU_RELEASE;
    if (flags & 8)
        return PU_REPEAT;
    if (flags & 1)
        return PU_PRESS;
    if (flags & 2)
        return PU_CHORD;
    return PU_RELEASE;
}
void pu_reconcile(PhraseUi *s, const uint32_t physical[2])
{
    /* A covering view can consume an owned key's up event. Retire that
     * physical gesture without a synthetic toggle. Keep a tombstone so a
     * delayed old release is harmless; an initial press starts a new gesture. */
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t up = s->cancelled[i] & ~physical[i];
        s->owned[i] &= ~up;
        s->held[i] &= ~up;
    }
}
void pu_context(PhraseUi *s, uint32_t active, uint32_t generation)
{
    if (generation != s->generation) {
        pu_leave(s);
        s->page = 0;
        s->generation = generation;
    }
    if (!active && s->active)
        pu_leave(s);
    s->active = active;
}
void pu_status(PhraseUi *s, const uint32_t *status, uint32_t fresh)
{
    for (uint32_t i = 0; i < PH_STATUS_WORDS; ++i)
        s->status[i] = status[i];
    s->fresh = fresh;
    if (s->focus_ticks)
        --s->focus_ticks;
}
int32_t pu_gain_track(const PhraseUi *s)
{
    int32_t track = -1;
    uint32_t newest = 0;
    if (!s->overlay)
        return -1;
    for (uint32_t i = 0; i < 16; ++i)
        if (held(s, PU_TRIG + i) && owned(s, PU_TRIG + i) && s->order[i] >= newest) {
            newest = s->order[i];
            track = (int32_t)i;
        }
    return track;
}
uint32_t pu_effective_mask(void)
{
    uint32_t mask = dkrt_phrase_get(PH_P_MASK) & PH_INPUT_MASK;
    for (uint32_t i = 0; i < 16; ++i)
        if (!dkrt_phrase_get(PH_P_GAIN0 + i))
            mask &= ~(1u << i);
    if (!dkrt_phrase_get(PH_P_EXT_GAIN))
        mask &= 0xffff;
    return mask;
}
uint32_t pu_led(const PhraseUi *s, uint32_t logical, uint32_t stock)
{
    if (!s->active || !s->overlay || logical < 26 || logical > 41)
        return stock;
    /* Native white steady and dim modes. Held-vs-next is shown on screen;
     * the physical TRIG grid always represents the next effective input. */
    return (pu_effective_mask() & (1u << (logical - 26))) ? 0x71 : 0x02;
}
static int modified(const PhraseUi *s)
{
    if (held(s, 16) || held(s, 17) || held(s, 18))
        return 1;
    for (uint32_t k = PU_TRIG; k <= PU_H; ++k)
        if (held(s, k))
            return 1;
    return 0;
}
uint32_t pu_key(PhraseUi *s, uint32_t key, uint32_t flags)
{
    if (key >= 64)
        return PU_PASS;
    int release = (flags & PU_RELEASE) != 0;
    if (((flags & PU_PRESS) || ((flags & PU_CHORD) && !held(s, key))) &&
        (s->cancelled[key >> 5] & (1u << (key & 31)))) {
        mark(s->cancelled, key, 0);
        mark(s->owned, key, 0);
        mark(s->held, key, 0);
    }
    int was_owned = owned(s, key), cancelled = (s->cancelled[key >> 5] >> (key & 31)) & 1;
    if (release) {
        mark(s->held, key, 0);
        mark(s->owned, key, 0);
        mark(s->cancelled, key, 0);
        if (was_owned || cancelled) {
            if (!cancelled && s->active && s->overlay && key >= PU_TRIG && key < PU_TRIG + 16 &&
                !locked(s)) {
                uint32_t track = key - PU_TRIG;
                if (!(s->adjusted & (1u << track)))
                    dkrt_phrase_set(PH_P_MASK, dkrt_phrase_get(PH_P_MASK) ^ (1u << track));
                s->adjusted &= ~(1u << track);
            }
            ++s->revision;
            return PU_OWN;
        }
        return PU_PASS;
    }
    int first = !held(s, key);
    mark(s->held, key, 1);
    if (was_owned)
        return PU_OWN;
    if (!s->active)
        return PU_PASS;
    if (flags & PU_REPEAT)
        return PU_PASS;
    /* A key held before IN opened remains the stock handler's gesture. */
    if (!first)
        return PU_PASS;
    if (key == PU_FX && held(s, PU_FUNC) && first) {
        pu_leave(s);
        mark(s->owned, key, 1);
        return PU_NEXT_FX;
    }
    if (key == PU_NO && held(s, PU_FUNC)) {
        dkrt_phrase_action(PH_ACTION_CLEAR);
        mark(s->owned, key, 1);
        ++s->revision;
        return PU_OWN;
    }
    if (s->overlay) {
        if (key == PU_H || key == PU_NO) {
            if (first)
                pu_leave(s);
            mark(s->owned, key, 1);
            return PU_OWN;
        }
        if (key == PU_YES) {
            if (first)
                dkrt_phrase_action(locked(s) ? PH_ACTION_CANCEL : PH_ACTION_ARM);
            mark(s->owned, key, 1);
            ++s->revision;
            return PU_OWN;
        }
        if (key >= PU_TRIG && key < PU_TRIG + 16) {
            uint32_t track = key - PU_TRIG;
            mark(s->owned, key, 1);
            s->order[track] = ++s->serial;
            s->adjusted &= ~(1u << track);
            if (locked(s))
                s->adjusted |= 1u << track;
            else if (held(s, PU_FUNC)) {
                dkrt_phrase_set(PH_P_MASK, (dkrt_phrase_get(PH_P_MASK) & 0x30000) | (1u << track));
                s->adjusted |= 1u << track;
            }
            ++s->revision;
            return PU_OWN;
        }
        if (key == PU_UP || key == PU_DOWN || (key >= 41 && key <= 48)) {
            mark(s->owned, key, 1);
            return PU_OWN;
        }
        return PU_PASS;
    }
    if (key == PU_H && first && !held(s, PU_FUNC)) {
        s->overlay = 1;
        mark(s->owned, key, 1);
        ++s->revision;
        return PU_OWN;
    }
    if ((key == PU_UP || key == PU_DOWN) && first && !modified(s)) {
        s->page = (s->page + (key == PU_DOWN ? 1 : 3)) & 3;
        mark(s->owned, key, 1);
        for (uint32_t i = 0; i < 9; ++i)
            s->detent[i] = 0;
        ++s->revision;
        return PU_OWN;
    }
    if (key >= 41 && key <= 48) {
        mark(s->owned, key, 1);
        return PU_OWN;
    }
    return PU_PASS;
}
uint32_t pu_parameter(uint32_t page, uint32_t encoder)
{
    static const uint8_t mapping[4][8] = {{0, 1, 2, 3, 4, 5, 255, 255},
                                          {6, 7, 8, 9, 10, 11, 255, 255},
                                          {12, 13, 14, 15, 255, 255, 255, 255},
                                          {16, 17, 18, 19, 255, 255, 255, 255}};
    return page < 4 && encoder < 8 ? mapping[page][encoder] : 255;
}
static uint32_t maximum(uint32_t p)
{
    if (p == PH_P_REC || p == PH_P_FROM)
        return 1;
    if (p == PH_P_TAKE || p == PH_P_CUE || p == PH_P_SCAN)
        return 128;
    if (p == PH_P_STRT)
        return 126;
    if (p == PH_P_RATE)
        return 12;
    if (p == PH_P_TUNE)
        return 96;
    if (p == PH_P_SHAP)
        return 3;
    return 127;
}
uint32_t pu_turn(PhraseUi *s, uint32_t encoder, int32_t delta)
{
    if (!s->active || encoder > 8)
        return PU_PASS;
    uint32_t p = encoder == 8 ? PH_P_FADE : pu_parameter(s->page, encoder), max = 127;
    if (s->overlay) {
        if (encoder == 8) {
            int32_t track = pu_gain_track(s);
            if (track >= 0) {
                if (locked(s))
                    return PU_OWN;
                p = PH_P_GAIN0 + (uint32_t)track;
                s->adjusted |= 1u << track;
            }
        } else {
            if (locked(s))
                return PU_OWN;
            if (encoder == 0)
                p = PH_P_MASK;
            else if (encoder == 1)
                p = PH_P_EXT_GAIN;
            else
                return PU_OWN;
        }
    }
    if (p >= PH_PARAM_COUNT)
        return PU_OWN;
    if ((p == PH_P_REC || p == PH_P_FROM || p == PH_P_RATE || p == PH_P_SHAP || p == PH_P_MASK)) {
        /* A long accelerated event never skips several discrete choices. */
        if (delta > 4)
            delta = 4;
        if (delta < -4)
            delta = -4;
        int32_t d = s->detent[encoder] + delta;
        delta = d / 4;
        s->detent[encoder] = d % 4;
        if (!delta)
            return PU_OWN;
    }
    uint32_t raw = dkrt_phrase_get(p);
    if (p == PH_P_MASK) {
        raw = (raw >> 16) & 3;
        max = 3;
    } else
        max = maximum(p);
    int32_t value = (int32_t)raw + delta;
    uint32_t min = p == PH_P_LEN ? 1 : 0;
    if (value < (int32_t)min)
        value = (int32_t)min;
    if (value > (int32_t)max)
        value = (int32_t)max;
    if (p == PH_P_MASK)
        value = (int32_t)((dkrt_phrase_get(p) & 0xffff) | ((uint32_t)value << 16));
    dkrt_phrase_set(p, (uint32_t)value);
    s->focus = p;
    s->focus_ticks = 45;
    ++s->revision;
    return PU_OWN;
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
void pu_format(char out[16], uint32_t p, uint32_t raw)
{
    static const char *rates[] = {"OFF",  "1/1",  "1/2",  "1/4",   "1/8",   "1/16", "1/32",
                                  "1/64", "1/4T", "1/8T", "1/16T", "1/32T", "1/64T"};
    static const char *shapes[] = {"SOFT", "TRI", "RISE", "FALL"};
    if (p == PH_P_REC) {
        copy(out, raw ? "ON" : "OFF");
        return;
    }
    if (p == PH_P_FROM) {
        copy(out, raw ? "PAST" : "NEXT");
        return;
    }
    if (p == PH_P_TAKE && !raw) {
        copy(out, "PAT");
        return;
    }
    if (p == PH_P_CUE && !raw) {
        copy(out, "NOW");
        return;
    }
    if (p == PH_P_RATE) {
        copy(out, rates[raw <= 12 ? raw : 0]);
        return;
    }
    if (p == PH_P_SHAP) {
        copy(out, shapes[raw & 3]);
        return;
    }
    if (p == PH_P_TUNE) {
        int32_t n = (int32_t)raw - 48;
        char *s = out;
        if (n < 0) {
            *s++ = '-';
            n = -n;
        } else
            *s++ = '+';
        s = number(s, (uint32_t)n / 2);
        if (n & 1)
            copy(s, ".5");
        return;
    }
    if (p == PH_P_SCAN) {
        int32_t n = (int32_t)raw - 64;
        char *s = out;
        if (n < 0) {
            *s++ = '-';
            n = -n;
        } else
            *s++ = '+';
        uint32_t c = (uint32_t)n * 100 / 32;
        s = number(s, c / 100);
        *s++ = '.';
        *s++ = (char)('0' + (c % 100) / 10);
        *s++ = (char)('0' + c % 10);
        copy(s, "X");
        return;
    }
    if (p == PH_P_SIZE) {
        copy(number(out, 5 + raw * raw * 495 / (127 * 127)), "MS");
        return;
    }
    if (p == PH_P_DEC) {
        uint32_t cs = 15 + (785 * raw * raw + 8064) / (127 * 127);
        char *s = number(out, cs / 100);
        *s++ = '.';
        *s++ = (char)('0' + (cs % 100) / 10);
        *s++ = (char)('0' + cs % 10);
        copy(s, "S");
        return;
    }
    if (p == PH_P_STRT || p == PH_P_LEN || p == PH_P_SPRY || p == PH_P_JITR || p == PH_P_POS ||
        p == PH_P_REVR || p == PH_P_WDTH || p == PH_P_DIFF || p == PH_P_FADE) {
        copy(number(out, (raw * 100 + 63) / 127), "%");
        return;
    }
    number(out, raw);
}
