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
        t->lock_count > MI_LOCKS || t->shared_lanes > 3 || !note_packet(packet))
        return 0;
    uint32_t wave = 0, wave_value = 0;
    for (uint32_t i = 0; i < t->lock_count; ++i)
        if ((t->locks[i] >> 10) == 34) {
            wave_value = t->locks[i] & 1023u;
            if (++wave > 1 || wave_value > 7)
                return 0;
        }
    const uint32_t sound = t->sound_controls >> 31;
    const uint32_t has_source = wave | sound;
    if (t->source_valid != has_source)
        return 0;
    if (sound ? (t->sound_tune < 0x400 || t->sound_tune > 0x7c00 || t->source[0] > 127 ||
                 t->source[1] > 1023 || (wave && wave_value != (t->sound_controls & 7u)))
              : (t->sound_controls || t->sound_tune))
        return 0;
    uint32_t w[ML_WORDS];
    w[0] = ML_MAGIC;
    w[1] = packet[2];
    w[2] = packet[3];
    w[3] = packet[9];
    w[4] = ((packet[10] >> 10) & 15u) | (t->shared_lanes << 4) | (t->lock_count << 8) |
           (has_source << 16) | (sound << 17);
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t a = 2 * i < t->lock_count ? t->locks[2 * i] : 0;
        uint32_t b = 2 * i + 1 < t->lock_count ? t->locks[2 * i + 1] : 0;
        w[5 + i] = a | (b << 16);
    }
    for (uint32_t i = 0; i < 9; ++i) {
        uint32_t a = has_source && 2 * i < MI_SOURCE_VALUES ? t->source[2 * i] : 0;
        uint32_t b = has_source && 2 * i + 1 < MI_SOURCE_VALUES ? t->source[2 * i + 1] : 0;
        w[8 + i] = a | (b << 16);
    }
    if (sound) {
        w[7] |= t->sound_controls << 16;
        w[16] |= t->sound_controls & 0x7fff0000u;
        w[8] = t->source[0] | ((uint32_t)t->sound_tune << 7) | ((uint32_t)t->source[1] << 22);
    }
    w[17] = check(w);
    for (uint32_t i = 0; i < ML_WORDS; ++i)
        out[i] = w[i];
    return 1;
}
int ml_decode(const uint32_t side[ML_WORDS], const uint32_t *packet, uint16_t locks[MI_LOCKS],
              uint32_t *count, uint16_t source[MI_SOURCE_VALUES], uint32_t *source_valid,
              uint32_t *sound_controls, uint16_t *sound_tune)
{
    if (!side || !locks || !count || !source || !source_valid || !sound_controls || !sound_tune ||
        !note_packet(packet) || side[0] != ML_MAGIC || side[1] != packet[2] ||
        side[2] != packet[3] || side[3] != packet[9] || (side[4] & ~0x3073fu) ||
        (side[4] & 15u) != ((packet[10] >> 10) & 15u) || side[17] != check(side))
        return 0;
    uint32_t n = (side[4] >> 8) & 7u;
    uint32_t has_source = (side[4] >> 16) & 1u, sound = (side[4] >> 17) & 1u;
    uint32_t wave = 0, wave_value = 0;
    uint32_t controls = (side[7] >> 16) | (side[16] & 0xffff0000u);
    uint16_t tune = 0;
    if (n > MI_LOCKS || (controls & MI_SOUND_VALID) || (!sound && controls))
        return 0;
    if (sound) {
        tune = (uint16_t)((side[8] >> 7) & 0x7fffu);
        if (tune < 0x400 || tune > 0x7c00)
            return 0;
    }
    uint16_t tmp[MI_LOCKS];
    for (uint32_t i = 0; i < MI_LOCKS; ++i) {
        tmp[i] = (uint16_t)(side[5 + i / 2] >> (16 * (i & 1u)));
        if (i >= n && tmp[i])
            return 0;
        if (i < n && (tmp[i] >> 10) == 34) {
            wave_value = tmp[i] & 1023u;
            if (++wave > 1 || wave_value > 7)
                return 0;
        }
    }
    if ((wave | sound) != has_source || (sound && wave && wave_value != (controls & 7u)))
        return 0;
    uint16_t source_tmp[MI_SOURCE_VALUES];
    for (uint32_t i = 0; i < MI_SOURCE_VALUES; ++i) {
        source_tmp[i] = (uint16_t)(side[8 + i / 2] >> (16 * (i & 1u)));
        if (sound && i < 2)
            source_tmp[i] = (uint16_t)(i ? side[8] >> 22 : side[8] & 127u);
        if (!has_source && source_tmp[i])
            return 0;
    }
    for (uint32_t i = 0; i < MI_LOCKS; ++i)
        locks[i] = tmp[i];
    for (uint32_t i = 0; i < MI_SOURCE_VALUES; ++i)
        source[i] = source_tmp[i];
    *count = n;
    *source_valid = has_source;
    *sound_controls = sound ? controls | MI_SOUND_VALID : 0;
    *sound_tune = tune;
    return 1;
}
