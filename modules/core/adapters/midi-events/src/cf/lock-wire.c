#include "lock-wire.h"

static uint32_t check(const uint32_t *w)
{
    uint32_t a = 0x6c6f636bu, b = 0x74726967u;
    for (uint32_t i = 0; i < ML_WORDS - 1; ++i) {
        a += w[i];
        b += a;
    }
    return a ^ b;
}
static int note_packet(const uint32_t *p)
{
    if (!p || p[0] != MT_MAGIC || !p[2] || !p[3] || !p[9])
        return 0;
    uint32_t kind = p[1] & 255u, count = (p[1] >> 8) & 15u;
    return (kind == MT_EVENTS || kind == MT_MAPPED) && count >= 1 && count <= 8 &&
           p[1] == (kind | (count << 8) | MT_LOCKED);
}
int ml_encode(const MiTransport *t, const uint32_t *packet, uint32_t out[ML_WORDS])
{
    if (!t || !out || packet != t->words || !t->pending || !t->lock_valid ||
        t->lock_count > MI_LOCKS || !note_packet(packet))
        return 0;
    uint32_t wave = 0;
    for (uint32_t i = 0; i < t->lock_count; ++i)
        if ((t->locks[i] >> 10) == 34) {
            if (++wave > 1 || (t->locks[i] & 1023u) > 7)
                return 0;
        }
    if (!!t->source_valid != !!wave)
        return 0;
    uint32_t w[ML_WORDS];
    w[0] = ML_MAGIC;
    w[1] = packet[2];
    w[2] = packet[3];
    w[3] = packet[9];
    w[4] = ((packet[10] >> 10) & 15u) | (t->lock_count << 8) | (wave << 16);
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t a = 2 * i < t->lock_count ? t->locks[2 * i] : 0;
        uint32_t b = 2 * i + 1 < t->lock_count ? t->locks[2 * i + 1] : 0;
        w[5 + i] = a | (b << 16);
    }
    for (uint32_t i = 0; i < 9; ++i) {
        uint32_t a = wave && 2 * i < MI_SOURCE_VALUES ? t->source[2 * i] : 0;
        uint32_t b = wave && 2 * i + 1 < MI_SOURCE_VALUES ? t->source[2 * i + 1] : 0;
        w[8 + i] = a | (b << 16);
    }
    w[17] = check(w);
    for (uint32_t i = 0; i < ML_WORDS; ++i)
        out[i] = w[i];
    return 1;
}
int ml_decode(const uint32_t side[ML_WORDS], const uint32_t *packet, uint16_t locks[MI_LOCKS],
              uint32_t *count, uint16_t source[MI_SOURCE_VALUES], uint32_t *source_valid)
{
    if (!side || !locks || !count || !source || !source_valid || !note_packet(packet) ||
        side[0] != ML_MAGIC || side[1] != packet[2] || side[2] != packet[3] ||
        side[3] != packet[9] || (side[4] & ~0x1070fu) ||
        (side[4] & 15u) != ((packet[10] >> 10) & 15u) || side[17] != check(side))
        return 0;
    uint32_t n = (side[4] >> 8) & 7u;
    uint32_t has_source = (side[4] >> 16) & 1u, wave = 0;
    if (n > MI_LOCKS || (side[7] >> 16) || (side[16] >> 16))
        return 0;
    uint16_t tmp[MI_LOCKS];
    for (uint32_t i = 0; i < MI_LOCKS; ++i) {
        tmp[i] = (uint16_t)(side[5 + i / 2] >> (16 * (i & 1u)));
        if (i >= n && tmp[i])
            return 0;
        if (i < n && (tmp[i] >> 10) == 34) {
            if (++wave > 1 || (tmp[i] & 1023u) > 7)
                return 0;
        }
    }
    if (wave != has_source)
        return 0;
    uint16_t source_tmp[MI_SOURCE_VALUES];
    for (uint32_t i = 0; i < MI_SOURCE_VALUES; ++i) {
        source_tmp[i] = (uint16_t)(side[8 + i / 2] >> (16 * (i & 1u)));
        if (!has_source && source_tmp[i])
            return 0;
    }
    for (uint32_t i = 0; i < MI_LOCKS; ++i)
        locks[i] = tmp[i];
    for (uint32_t i = 0; i < MI_SOURCE_VALUES; ++i)
        source[i] = source_tmp[i];
    *count = n;
    *source_valid = has_source;
    return 1;
}
