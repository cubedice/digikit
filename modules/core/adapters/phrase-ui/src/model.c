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
void pu_leave(PhraseUi *s)
{
    /* the TRIG keys and their LEDs go back to the firmware at once; a release of G that comes later is no press */
    s->select = 0;
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
}
static void toggle(PhraseUi *s, uint32_t p)
{
    dkrt_phrase_set(p, !dkrt_phrase_get(p));
    ++s->revision;
}
void pu_loop_status(PhraseUi *s)
{
    s->loop_fresh = s->loop ? s->loop->status(s->loop_status) : 0;
}
uint32_t pu_has_loop(const PhraseUi *s)
{
    return s->loop_fresh && (s->loop_status[LS_STATE] & LS_LOOP);
}
static void input(PhraseUi *s, uint32_t value)
{
    if (dkrt_phrase_get(PH_P_INPT) != value) {
        dkrt_phrase_set(PH_P_INPT, value);
        ++s->revision;
    }
}
void pu_loop(PhraseUi *s)
{
    pu_loop_status(s);
    if (!s->loop_fresh) {
        /* the loop effect does not report: after a while there is no loop to hear */
        if (s->loop_stale < PU_LOOP_GONE)
            ++s->loop_stale;
        else
            input(s, 0);
        return;
    }
    s->loop_stale = 0;
    uint32_t state = s->loop_status[LS_STATE], count = state >> LS_COUNT_SHIFT;
    if (!(state & LS_LOOP))
        input(s, 0);
    else if (s->loop_known && count != s->loop_seen)
        /* an armed pattern change took a new loop: PRISM hears it (the engine counts loops across its restarts) */
        input(s, 1);
    s->loop_seen = count;
    s->loop_known = 1;
}
uint32_t pu_tracks(void)
{
    return dkrt_phrase_get(PH_P_TRKS) & PH_TRACKS_ALL;
}
uint32_t pu_track_count(void)
{
    uint32_t n = 0;
    for (uint32_t tracks = pu_tracks(); tracks; tracks &= tracks - 1)
        ++n;
    return n;
}
uint32_t pu_selecting(const PhraseUi *s)
{
    return s->active && s->select;
}
uint32_t pu_led(const PhraseUi *s, uint32_t logical, uint32_t stock)
{
    if (!pu_selecting(s) || logical < PU_LED_TRIG || logical >= PU_LED_TRIG + PH_TRACKS)
        return stock;
    return (pu_tracks() >> (logical - PU_LED_TRIG)) & 1 ? PU_LED_HEARD : PU_LED_DIM;
}
/* G is released: the selection ends. A short press with no TRIG key and no turn is INPT's switch, while there is a loop. */
static void select_end(PhraseUi *s)
{
    if (!s->select)
        return;
    uint32_t held_for = s->status[PH_S_HEARTBEAT / 4] - s->select_beat,
             timed = s->select_timed && s->fresh;
    if (!s->select_used && (!timed || held_for < PU_SHORT_BLOCKS) && s->active && pu_has_loop(s))
        toggle(s, PH_P_INPT);
    s->select = 0;
    ++s->revision;
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
        if (key == PU_PUSH + PU_SELECT && was_owned)
            select_end(s);
        /* The release of a key this page took is this page's, also after the view was covered. */
        return was_owned || cancelled ? PU_OWN : PU_PASS;
    }
    int first = !held(s, key);
    mark(s->held, key, 1);
    if (was_owned)
        return PU_OWN;
    /* A key held before the page was on top remains the stock handler's gesture. */
    if (!s->active || (flags & PU_REPEAT) || !first)
        return PU_PASS;
    if (key >= PU_TRIG && key < PU_TRIG + PH_TRACKS) {
        /* While G is held a TRIG key turns its track on or off; at any other time it is the firmware's. */
        if (!s->select)
            return PU_PASS;
        dkrt_phrase_set(PH_P_TRKS, pu_tracks() ^ (1u << (key - PU_TRIG)));
        s->select_used = 1;
        ++s->revision;
        mark(s->owned, key, 1);
        return PU_OWN;
    }
    if (key == PU_NO && held(s, PU_FUNC)) {
        /* Drop the loop (taken also without one, so that the stock page does nothing with it). */
        if (pu_has_loop(s)) {
            s->loop->drop();
            ++s->revision;
        }
        mark(s->owned, key, 1);
        return PU_OWN;
    }
    if (key >= PU_PUSH && key < PU_PUSH + 8) {
        /* A is FLIP, B is SYNC, C is HOLD; G holds the selection of the tracks (its release may switch INPT); H is ARM while
         * the loop effect reports. D, E and F are taken and do nothing. */
        static const uint8_t switches[3] = {PH_P_FLIP, PH_P_SYNC, PH_P_HOLD};
        if (key < PU_PUSH + 3)
            toggle(s, switches[key - PU_PUSH]);
        if (key == PU_PUSH + 1)
            s->detent[1] = 0;
        if (key == PU_PUSH + PU_SELECT) {
            s->select = 1;
            s->select_used = 0;
            s->select_beat = s->status[PH_S_HEARTBEAT / 4];
            s->select_timed = s->fresh;
            ++s->revision;
        }
        if (key == PU_PUSH + 7 && s->loop && s->loop_fresh) {
            s->loop->arm();
            ++s->revision;
        }
        mark(s->owned, key, 1);
        return PU_OWN;
    }
    return PU_PASS;
}
uint32_t pu_parameter(uint32_t encoder)
{
    static const uint8_t mapping[9] = {PH_P_ZONE,      PH_P_RATE,      PH_P_RPTS,
                                       PH_P_COLR,      PH_P_HALO,      PH_P_SKEW,
                                       PH_PARAM_COUNT, PH_PARAM_COUNT, PH_P_MIX};
    if (encoder > PU_LEVEL)
        return PH_PARAM_COUNT;
    if (encoder == 1 && dkrt_phrase_get(PH_P_SYNC))
        return PH_P_NOTE;
    return mapping[encoder];
}
static uint32_t maximum(uint32_t p)
{
    if (p == PH_P_ZONE)
        return PH_ZONES - 1;
    if (p == PH_P_RATE)
        return PH_RATE_MAX;
    if (p == PH_P_NOTE)
        return PH_TIME_CHOICES - 1;
    return 127;
}
uint32_t pu_turn(PhraseUi *s, uint32_t encoder, int32_t delta, uint32_t fine)
{
    if (!s->active || encoder > PU_LEVEL)
        return PU_PASS;
    uint32_t p = pu_parameter(encoder);
    /* G and H: taken, so that the stock page does not turn a value under them. While there is a loop, G is the switch (a
     * tick up is LOOP, a tick down LIVE) and H the loop effect's crossfader, a step a tick. A turn of G while it is held
     * makes its release no press. */
    if (p >= PH_PARAM_COUNT) {
        if (encoder == PU_SELECT && delta && s->select)
            s->select_used = 1;
        if (!pu_has_loop(s) || !delta)
            return PU_OWN;
        if (encoder == 6)
            input(s, delta > 0);
        else {
            int32_t value = (int32_t)s->loop->get(LP_XFAD) + delta;
            if (value < 0)
                value = 0;
            if (value > LP_MAX)
                value = LP_MAX;
            s->loop->xfad((uint32_t)value);
            ++s->revision;
        }
        return PU_OWN;
    }
    if (p == PH_P_ZONE || p == PH_P_NOTE) {
        /* A choice takes four ticks, and a long accelerated event never skips several choices. */
        if (delta > 4)
            delta = 4;
        if (delta < -4)
            delta = -4;
        int32_t d = s->detent[encoder] + delta;
        delta = d / 4;
        s->detent[encoder] = d % 4;
        if (!delta)
            return PU_OWN;
    } else if (p == PH_P_RATE && !fine)
        delta *= PH_RATE_COARSE;
    int32_t value = (int32_t)dkrt_phrase_get(p) + delta, max = (int32_t)maximum(p);
    if (value < 0)
        value = 0;
    if (value > max)
        value = max;
    dkrt_phrase_set(p, (uint32_t)value);
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
const char *pu_zone_name(uint32_t zone)
{
    static const char *const names[PH_ZONES] = {"KARP", "FLNG", "CHOR", "SLAP",
                                                "ECHO", "LONG", "PHRS", "LOOP"};
    return names[zone < PH_ZONES ? zone : 0];
}
void pu_format(char out[16], uint32_t p, uint32_t raw)
{
    static const char *const notes[PH_TIME_CHOICES] = {
        "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8",  "1/4T",
        "1/8.", "1/4",   "1/2T",  "1/4.", "1/2",  "1/2.",  "1BAR", "2BAR"};
    if (p == PH_P_ZONE) {
        copy(out, pu_zone_name(raw));
        return;
    }
    if (p == PH_P_NOTE) {
        copy(out, notes[raw < PH_TIME_CHOICES ? raw : 0]);
        return;
    }
    if (p == PH_P_COLR) {
        /* The distance from the centre, where the repeats are clean; the cell names the side. */
        number(out, raw < 64 ? 64 - raw : raw - 64);
        return;
    }
    if (p == PH_P_SKEW && raw != 64) {
        /* The distance from the centre and the side whose time is the longer one. */
        number(copy(out, raw < 64 ? "L " : "R "), raw < 64 ? 64 - raw : raw - 64);
        return;
    }
    number(out, p == PH_P_SKEW ? 0 : raw);
}
void pu_time(char out[16], uint32_t frames)
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
uint32_t pu_frames(const PhraseUi *s)
{
    uint32_t l = s->status[PH_S_TIME_L / 4], r = s->status[PH_S_TIME_R / 4];
    /* no time while the engine does not report, or reports that it has no controls (a silent block) */
    if (!s->fresh || !l || !r || s->status[PH_S_ERROR / 4])
        return 0;
    if (l == r)
        return l;
    /* The integer square root of l * r by bisection, in 32 bits (this processor has no 64-bit product): times of
     * more than 16 bits lose their lowest bits first, at most 16 frames of more than a second. */
    uint32_t shift = 0;
    while ((l | r) >> 16) {
        l >>= 1;
        r >>= 1;
        ++shift;
    }
    uint32_t product = l * r, lo = l < r ? l : r, hi = l < r ? r : l;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1) / 2;
        if (mid * mid <= product)
            lo = mid;
        else
            hi = mid - 1;
    }
    /* to the nearest */
    if (lo * lo + lo < product)
        ++lo;
    return lo << shift;
}
