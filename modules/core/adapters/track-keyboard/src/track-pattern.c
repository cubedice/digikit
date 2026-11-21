#include "track-service.h"
#include "track-memory.h"

enum { HEADER = 12, STEP_BYTES = 64, TRACK_BYTES = TK_TRACKS * TK_CONFIG_BYTES };
enum { VALUES = 4, NOTE_COUNT = VALUES + 2 * TK_CONTROL_COUNT, NOTES = NOTE_COUNT + 1 };
_Static_assert(NOTES + 2 * TK_NOTES <= STEP_BYTES, "pattern lock record");

static unsigned read16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static void write16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint32_t checksum(const uint8_t *p, size_t n)
{
    uint32_t hash = 2166136261u;
    while (n--)
        hash = (hash ^ *p++) * 16777619u;
    return hash;
}

size_t tk_pattern_encoded_size(const TkPattern *pattern)
{
    size_t n = HEADER + TRACK_BYTES;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (pattern->steps[i].index)
            n += STEP_BYTES;
    return n;
}

static bool seen_step(uint8_t *bitmap, unsigned index)
{
    if (!index || index > TK_TRACKS * TK_STEPS)
        return true;
    --index;
    uint8_t *word = &bitmap[index / 8];
    uint8_t bit = (uint8_t)(1u << (index % 8));
    if (*word & bit)
        return true;
    *word |= bit;
    return false;
}

static bool encode_step(const TkPatternStep *step, uint8_t *bytes)
{
    TkTrackConfig base = {.params = tk_defaults()};
    TkParams ignored;
    TkRecipe other;
    const TkStepLocks *l = &step->locks;
    if (!l->mask || !tk_resolve_step(&base, l, &ignored, &other))
        return false;
    memset(bytes, 0, STEP_BYTES);
    write16(bytes, step->index);
    write16(bytes + 2, l->mask);
    for (enum TkControl id = TK_CTL_ARP; id < TK_CTL_VOICES; ++id) {
        if (l->mask & (1u << id)) {
            int v = l->values[id] + (id == TK_CTL_OCTAVES ? 3 : 0);
            write16(bytes + VALUES + 2 * id, (unsigned)v);
        }
    }
    bytes[NOTE_COUNT] = other.count;
    for (uint8_t i = 0; i < other.count; ++i) {
        bytes[NOTES + 2 * i] = (uint8_t)(other.notes[i].interval + 127);
        bytes[NOTES + 2 * i + 1] = other.notes[i].velocity;
    }
    return true;
}

bool tk_pattern_encode(const TkPattern *pattern, uint8_t *data, size_t size)
{
    if (!data || size != tk_pattern_encoded_size(pattern))
        return false;
    /* An incomplete write carries no magic and cannot be read as a pattern. */
    memset(data, 0, HEADER);
    uint8_t *next = data + HEADER;
    for (uint8_t track = 0; track < TK_TRACKS; ++track, next += TK_CONFIG_BYTES)
        if (!tk_config_encode(&pattern->tracks[track], next, TK_CONFIG_BYTES) ||
            (pattern->tracks[track].helpers & (1u << track)))
            return false;
    uint8_t seen[(TK_TRACKS * TK_STEPS) / 8] = {0};
    unsigned count = 0;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i) {
        const TkPatternStep *step = &pattern->steps[i];
        if (!step->index)
            continue;
        if (seen_step(seen, step->index) || !encode_step(step, next))
            return false;
        next += STEP_BYTES;
        ++count;
    }
    data[4] = 1;
    write16(data + 6, count);
    uint32_t hash = checksum(data + HEADER, size - HEADER);
    write16(data + 8, hash >> 16);
    write16(data + 10, hash);
    memcpy(data, "TKPT", 4);
    return true;
}

static bool decode_step(TkPatternStep *step, const uint8_t *bytes)
{
    unsigned mask = read16(bytes + 2);
    if (!mask || (mask & ~(TK_RECIPE_OVERRIDE | ((1u << TK_CTL_VOICES) - 1u))))
        return false;
    *step = (TkPatternStep){.index = (uint16_t)read16(bytes)};
    for (enum TkControl id = TK_CTL_ARP; id < TK_CONTROL_COUNT; ++id) {
        int value = (int)read16(bytes + VALUES + 2 * id);
        if (mask & (1u << id)) {
            if (!tk_set_lock(&step->locks, id, value - (id == TK_CTL_OCTAVES ? 3 : 0)))
                return false;
        } else if (value) {
            return false;
        }
    }
    uint8_t count = bytes[NOTE_COUNT];
    if (count > TK_NOTES || (count && !(mask & TK_RECIPE_OVERRIDE)))
        return false;
    step->locks.mask = (uint16_t)mask;
    step->locks.user.count = count;
    for (uint8_t i = 0; i < TK_NOTES; ++i) {
        uint8_t offset = bytes[NOTES + i * 2], velocity = bytes[NOTES + i * 2 + 1];
        if (i < count) {
            if (offset == UINT8_MAX || velocity > 127)
                return false;
            step->locks.user.notes[i] = (TkRecipeNote){(int8_t)(offset - 127), velocity};
        } else if (offset || velocity) {
            return false;
        }
    }
    for (unsigned i = NOTES + 2 * TK_NOTES; i < STEP_BYTES; ++i)
        if (bytes[i])
            return false;
    return true;
}

bool tk_pattern_decode(TkPattern *pattern, const uint8_t *data, size_t size)
{
    if (!data || size < HEADER + TRACK_BYTES || data[0] != 'T' || data[1] != 'K' ||
        data[2] != 'P' || data[3] != 'T' || data[4] != 1 || data[5] != 0)
        return false;
    unsigned count = read16(data + 6);
    if (count > TK_LOCKED_STEPS || size != HEADER + TRACK_BYTES + count * STEP_BYTES ||
        checksum(data + HEADER, size - HEADER) != (read16(data + 8) << 16 | read16(data + 10)))
        return false;
    const uint8_t *next = data + HEADER;
    TkTrackConfig config;
    for (uint8_t track = 0; track < TK_TRACKS; ++track, next += TK_CONFIG_BYTES)
        if (!tk_config_decode(&config, next, TK_CONFIG_BYTES) || (config.helpers & (1u << track)))
            return false;
    uint8_t seen[(TK_TRACKS * TK_STEPS) / 8] = {0};
    TkPatternStep step;
    for (unsigned i = 0; i < count; ++i, next += STEP_BYTES)
        if (seen_step(seen, read16(next)) || !decode_step(&step, next))
            return false;
    /* Fixed-size bounded second pass makes decode atomic without a second
     * pattern-sized stack buffer. Input cannot change during a load callback.
     */
    tk_pattern_init(pattern);
    next = data + HEADER;
    for (uint8_t track = 0; track < TK_TRACKS; ++track, next += TK_CONFIG_BYTES)
        tk_config_decode(&pattern->tracks[track], next, TK_CONFIG_BYTES);
    for (unsigned i = 0; i < count; ++i, next += STEP_BYTES)
        decode_step(&pattern->steps[i], next);
    return true;
}
