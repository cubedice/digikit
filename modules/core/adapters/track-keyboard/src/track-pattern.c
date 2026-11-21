#include "track-service.h"
#include "track-memory.h"

enum { LEGACY_HEADER = 12, HEADER = TK_PATTERN_HEADER, STEP_BYTES = 64 };
enum { TRACK_MASK = 12, SEED_COUNT = 14 };
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

static bool saved_track(const TkTrackConfig *config)
{
    if (config->override || config->user.count || config->helpers || config->canopy_mix ||
        config->params.legacy_swing || config->selector_octave != -1 ||
        config->selector_channel != -1)
        return true;
    TkTrackConfig defaults = tk_track_defaults();
    for (enum TkControl control = TK_CTL_ARP; control < TK_CONTROL_COUNT; ++control) {
        int actual, initial;
        tk_get_control(&config->params, control, &actual);
        tk_get_control(&defaults.params, control, &initial);
        if (actual != initial)
            return true;
    }
    return false;
}

static uint16_t saved_tracks(const TkPattern *p)
{
    uint16_t mask = 0;
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (saved_track(&p->tracks[track]))
            mask |= (uint16_t)(1u << track);
    return mask;
}

static bool belongs(uint16_t index, uint8_t track)
{
    return index > track * TK_STEPS && index <= (track + 1u) * TK_STEPS;
}

bool tk_pattern_step_attachment(const TkPattern *p, uint8_t track, uint8_t step,
                                TkStepAttachment *out)
{
    memset(out, 0, sizeof *out);
    if (!p || track >= TK_TRACKS || step >= TK_STEPS)
        return false;
    uint16_t index = (uint16_t)(track * TK_STEPS + step + 1u);
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (p->steps[i].index == index) {
            out->locks = p->steps[i].locks;
            break;
        }
    const TkSeed *seed = tk_pattern_seed(p, track, step);
    if (seed)
        out->seed = *seed;
    return out->locks.mask || out->seed.count;
}

static bool selected(uint16_t index, uint8_t track, const uint32_t positions[4])
{
    unsigned step = index - (unsigned)track * TK_STEPS - 1u;
    return step < TK_STEPS && (positions[step / 32u] & (1u << (step % 32u)));
}

enum TkAttachmentResult tk_pattern_attach_batch(TkPattern *p, uint8_t track, const int32_t *steps,
                                                uint8_t count, const TkStepAttachment *attachments,
                                                bool commit, bool *changed)
{
    if (track >= TK_TRACKS || !steps || !count || count > TK_STEPS)
        return TK_ATTACH_INVALID;
    uint32_t positions[4] = {0};
    unsigned needed_locks = 0, needed_seeds = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (steps[i] < 0 || steps[i] >= TK_STEPS)
            return TK_ATTACH_INVALID;
        unsigned step = (unsigned)steps[i];
        uint32_t bit = 1u << (step % 32u);
        if (attachments && (positions[step / 32u] & bit))
            return TK_ATTACH_INVALID;
        positions[step / 32u] |= bit;
        if (attachments) {
            needed_locks += attachments[i].locks.mask != 0;
            needed_seeds += attachments[i].seed.count != 0;
        }
    }
    *changed = false;
    if (!p)
        return needed_locks || needed_seeds ? TK_ATTACH_FULL : TK_ATTACH_OK;
    unsigned available_locks = 0, available_seeds = 0;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i) {
        uint16_t index = p->steps[i].index;
        bool replace = selected(index, track, positions);
        available_locks += !index || replace;
        *changed |= replace;
    }
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i) {
        uint16_t index = p->seeds[i].index;
        bool replace = selected(index, track, positions);
        available_seeds += !index || replace;
        *changed |= replace;
    }
    if (available_locks < needed_locks || available_seeds < needed_seeds)
        return TK_ATTACH_FULL;
    *changed |= needed_locks || needed_seeds;
    if (!commit || !*changed)
        return TK_ATTACH_OK;

    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (selected(p->steps[i].index, track, positions))
            p->steps[i].index = 0;
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i)
        if (selected(p->seeds[i].index, track, positions))
            p->seeds[i].index = 0;
    for (unsigned i = 0, lock = 0, seed = 0; attachments && i < count; ++i) {
        uint16_t index = (uint16_t)(track * TK_STEPS + steps[i] + 1u);
        if (attachments[i].locks.mask) {
            while (p->steps[lock].index)
                ++lock;
            p->steps[lock++] = (TkPatternStep){.index = index, .locks = attachments[i].locks};
        }
        if (attachments[i].seed.count) {
            while (p->seeds[seed].index)
                ++seed;
            p->seeds[seed++] = (TkPatternSeed){.index = index, .seed = attachments[i].seed};
        }
    }
    return TK_ATTACH_OK;
}

bool tk_pattern_set_attachment(TkPattern *p, uint8_t track, uint8_t step,
                               const TkStepAttachment *attachment)
{
    int32_t address = step;
    bool changed;
    return tk_pattern_attach_batch(p, track, &address, 1, attachment, true, &changed) ==
           TK_ATTACH_OK;
}

bool tk_pattern_has_track(const TkPattern *p, uint8_t track)
{
    if (!p || track >= TK_TRACKS)
        return false;
    if (saved_track(&p->tracks[track]))
        return true;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (belongs(p->steps[i].index, track))
            return true;
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i)
        if (belongs(p->seeds[i].index, track))
            return true;
    return false;
}

bool tk_pattern_copy_track(TkPattern *dst, uint8_t to, const TkPattern *src, uint8_t from)
{
    if (!dst || to >= TK_TRACKS || from >= TK_TRACKS)
        return false;
    if (dst == src && to == from)
        return true;
    unsigned steps = 0, seeds = 0, free_steps = 0, free_seeds = 0;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i) {
        steps += src && belongs(src->steps[i].index, from);
        free_steps += !dst->steps[i].index || belongs(dst->steps[i].index, to);
    }
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i) {
        seeds += src && belongs(src->seeds[i].index, from);
        free_seeds += !dst->seeds[i].index || belongs(dst->seeds[i].index, to);
    }
    if (steps > free_steps || seeds > free_seeds)
        return false;
    dst->tracks[to] = src ? src->tracks[from] : tk_track_defaults();
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (belongs(dst->steps[i].index, to))
            dst->steps[i].index = 0;
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i)
        if (belongs(dst->seeds[i].index, to))
            dst->seeds[i].index = 0;
    if (!src)
        return true;
    int offset = ((int)to - from) * TK_STEPS;
    for (unsigned i = 0, out = 0; i < TK_LOCKED_STEPS; ++i) {
        if (!belongs(src->steps[i].index, from))
            continue;
        while (dst->steps[out].index)
            ++out;
        dst->steps[out] = src->steps[i];
        dst->steps[out++].index = (uint16_t)(src->steps[i].index + offset);
    }
    for (unsigned i = 0, out = 0; i < TK_RECORDED_STEPS; ++i) {
        if (!belongs(src->seeds[i].index, from))
            continue;
        while (dst->seeds[out].index)
            ++out;
        dst->seeds[out] = src->seeds[i];
        dst->seeds[out++].index = (uint16_t)(src->seeds[i].index + offset);
    }
    return true;
}

static size_t varint_size(uint64_t value)
{
    size_t n = 1;
    for (; value >= 128; value >>= 7)
        ++n;
    return n;
}

static uint8_t *write_varint(uint8_t *out, uint64_t value)
{
    while (value >= 128) {
        *out++ = (uint8_t)(value | 0x80);
        value >>= 7;
    }
    *out++ = (uint8_t)value;
    return out;
}

static bool read_varint(const uint8_t **in, const uint8_t *end, uint64_t *value)
{
    *value = 0;
    for (unsigned shift = 0; shift < 64 && *in < end; shift += 7) {
        unsigned b = *(*in)++;
        if (shift == 63 && b > 1)
            return false;
        *value |= (uint64_t)(b & 127) << shift;
        if (!(b & 128))
            return !shift || b != 0;
    }
    return false;
}

static size_t seed_bytes(const TkSeed *seed)
{
    if (seed->count > TK_NOTES)
        return 0;
    size_t n = 3;
    for (uint8_t i = 0; i < seed->count; ++i)
        n += 2 + varint_size(seed->notes[i].delay_us) + varint_size(seed->notes[i].gate_ticks);
    return n;
}

static uint8_t *encode_seed(const TkPatternSeed *entry, uint8_t *out)
{
    write16(out, entry->index);
    out[2] = entry->seed.count;
    out += 3;
    for (uint8_t i = 0; i < entry->seed.count; ++i) {
        const TkSeedNote *note = &entry->seed.notes[i];
        *out++ = note->pitch;
        *out++ = note->velocity;
        out = write_varint(out, note->delay_us);
        out = write_varint(out, note->gate_ticks);
    }
    return out;
}

static const uint8_t *decode_seed(TkPatternSeed *entry, const uint8_t *in, const uint8_t *end)
{
    if (end - in < 3 || !in[2] || in[2] > TK_NOTES)
        return NULL;
    *entry = (TkPatternSeed){.index = (uint16_t)read16(in), .seed = {.count = in[2]}};
    in += 3;
    for (uint8_t i = 0; i < entry->seed.count; ++i) {
        if (end - in < 2)
            return NULL;
        TkSeedNote *note = &entry->seed.notes[i];
        note->pitch = *in++;
        note->velocity = *in++;
        uint64_t delay;
        if (!read_varint(&in, end, &delay) || delay > UINT32_MAX ||
            !read_varint(&in, end, &note->gate_ticks))
            return NULL;
        note->delay_us = (uint32_t)delay;
    }
    return tk_seed_valid(&entry->seed) ? in : NULL;
}

size_t tk_pattern_encoded_size(const TkPattern *pattern)
{
    size_t n = HEADER;
    uint16_t mask = saved_tracks(pattern);
    for (; mask; mask &= (uint16_t)(mask - 1))
        n += TK_CONFIG_BYTES;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (pattern->steps[i].index)
            n += STEP_BYTES;
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i)
        if (pattern->seeds[i].index)
            n += seed_bytes(&pattern->seeds[i].seed);
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
    uint16_t mask = saved_tracks(pattern);
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        if (!(mask & (1u << track)))
            continue;
        if (!tk_config_encode(&pattern->tracks[track], next, TK_CONFIG_BYTES) ||
            (pattern->tracks[track].helpers & (1u << track)))
            return false;
        next += TK_CONFIG_BYTES;
    }
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
    memset(seen, 0, sizeof seen);
    unsigned seeds = 0;
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i) {
        const TkPatternSeed *entry = &pattern->seeds[i];
        if (!entry->index)
            continue;
        if (seen_step(seen, entry->index) || entry->seed.length_ticks ||
            !tk_seed_valid(&entry->seed))
            return false;
        next = encode_seed(entry, next);
        ++seeds;
    }
    data[4] = 2;
    write16(data + 6, count);
    write16(data + SEED_COUNT, seeds);
    write16(data + TRACK_MASK, mask);
    uint32_t hash = checksum(data + LEGACY_HEADER, size - LEGACY_HEADER);
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
    if (!data || size < LEGACY_HEADER || !tk_bytes_equal(data, "TKPT", 4) || data[5])
        return false;
    bool legacy = data[4] == 1;
    unsigned header = legacy ? LEGACY_HEADER : HEADER;
    if (size < header || (!legacy && data[4] != 2))
        return false;
    unsigned count = read16(data + 6);
    unsigned seeds = legacy ? 0 : read16(data + SEED_COUNT);
    unsigned track_bytes = legacy ? TK_CONFIG_V1_BYTES : TK_CONFIG_BYTES;
    unsigned mask = legacy ? UINT16_MAX : read16(data + TRACK_MASK);
    size_t expected = header + count * STEP_BYTES;
    for (unsigned bits = mask; bits; bits &= bits - 1)
        expected += track_bytes;
    if (count > TK_LOCKED_STEPS || seeds > TK_RECORDED_STEPS || size < expected ||
        checksum(data + LEGACY_HEADER, size - LEGACY_HEADER) !=
            (read16(data + 8) << 16 | read16(data + 10)))
        return false;
    const uint8_t *next = data + header;
    TkTrackConfig config;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        if (!(mask & (1u << track)))
            continue;
        if (!tk_config_decode(&config, next, track_bytes) || (config.helpers & (1u << track)))
            return false;
        next += track_bytes;
    }
    uint8_t seen[(TK_TRACKS * TK_STEPS) / 8] = {0};
    TkPatternStep step;
    for (unsigned i = 0; i < count; ++i, next += STEP_BYTES)
        if (seen_step(seen, read16(next)) || !decode_step(&step, next))
            return false;
    const uint8_t *first_seed = next;
    const uint8_t *end = data + size;
    memset(seen, 0, sizeof seen);
    TkPatternSeed entry;
    for (unsigned i = 0; i < seeds; ++i) {
        next = decode_seed(&entry, next, end);
        if (!next || seen_step(seen, entry.index))
            return false;
    }
    if (next != end)
        return false;
    /* Validate the entire record before exposing any of it to playback. */
    tk_pattern_init(pattern);
    next = data + header;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        if (!(mask & (1u << track)))
            continue;
        tk_config_decode(&pattern->tracks[track], next, track_bytes);
        next += track_bytes;
    }
    for (unsigned i = 0; i < count; ++i, next += STEP_BYTES)
        decode_step(&pattern->steps[i], next);
    next = first_seed;
    for (unsigned i = 0; i < seeds; ++i)
        next = decode_seed(&pattern->seeds[i], next, end);
    return true;
}
