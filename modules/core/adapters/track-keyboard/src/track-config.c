#include "track-config.h"

#ifdef __m68k__
/* Keep switch jump-table data out of executable sections on ColdFire. */
#pragma GCC optimize("no-jump-tables")
#endif

#include "track-memory.h"

bool tk_get_control(const TkParams *p, enum TkControl id, int *value)
{
    if (id < 0 || id >= TK_CONTROL_COUNT)
        return false;
    const int controls[] = {p->arp,   p->poly,  p->order, p->gate,    p->rate,     p->octaves,
                            p->swing, p->reset, p->chord, p->voicing, p->strum_ms, p->voices};
    *value = controls[id];
    return true;
}

bool tk_set_control(TkParams *p, enum TkControl id, int v)
{
    /* Validate before narrowing, then validate ranges and commit together. */
    if (v < -3 || v > 500 || (id != TK_CTL_OCTAVES && v < 0) ||
        (id != TK_CTL_STRUM && v > UINT8_MAX))
        return false;
    TkParams next = *p;
    uint8_t u = (uint8_t)v;
    switch (id) {
    case TK_CTL_ARP:
        next.arp = u;
        break;
    case TK_CTL_POLY:
        next.poly = u;
        break;
    case TK_CTL_ORDER:
        next.order = u;
        break;
    case TK_CTL_GATE:
        next.gate = u;
        break;
    case TK_CTL_RATE:
        next.rate = u;
        break;
    case TK_CTL_OCTAVES:
        next.octaves = (int8_t)v;
        break;
    case TK_CTL_SWING:
        next.swing = u;
        next.legacy_swing = 0;
        break;
    case TK_CTL_RESET:
        next.reset = u;
        break;
    case TK_CTL_CHORD:
        next.chord = u;
        break;
    case TK_CTL_VOICING:
        next.voicing = u;
        break;
    case TK_CTL_STRUM:
        next.strum_ms = (uint16_t)v;
        break;
    case TK_CTL_VOICES:
        next.voices = u;
        break;
    default:
        return false;
    }
    if (!tk_valid(&next))
        return false;
    *p = next;
    return true;
}

bool tk_set_lock(TkStepLocks *locks, enum TkControl control, int value)
{
    TkParams params = tk_defaults();
    if (control == TK_CTL_VOICES || !tk_set_control(&params, control, value))
        return false;
    locks->mask |= (uint16_t)(1u << control);
    locks->values[control] = (int16_t)value;
    return true;
}

void tk_clear_lock(TkStepLocks *locks, enum TkControl control)
{
    if (control >= 0 && control < TK_CONTROL_COUNT)
        locks->mask &= (uint16_t)~(1u << control);
}

bool tk_resolve_step(const TkTrackConfig *config, const TkStepLocks *locks, TkParams *params,
                     TkRecipe *user)
{
    if (!tk_valid(&config->params) || !tk_valid_recipe(&config->user))
        return false;
    TkParams next = config->params;
    const TkRecipe *recipe = &config->user;
    if (locks) {
        if (locks->mask & ~(TK_RECIPE_OVERRIDE | ((1u << TK_CTL_VOICES) - 1u)))
            return false;
        for (enum TkControl id = TK_CTL_ARP; id < TK_CTL_VOICES; ++id)
            if ((locks->mask & (1u << id)) && !tk_set_control(&next, id, locks->values[id]))
                return false;
        if (locks->mask & TK_RECIPE_OVERRIDE) {
            if (!tk_valid_recipe(&locks->user))
                return false;
            recipe = &locks->user;
        }
    }
    *params = next;
    *user = *recipe;
    return true;
}

static bool valid_helpers(uint16_t mask)
{
    uint8_t n = 0;
    while (mask) {
        mask &= (uint16_t)(mask - 1u);
        ++n;
    }
    return n <= 3;
}

bool tk_assign_helpers(TkTrackConfig *config, uint8_t owner, uint16_t request, uint16_t audio,
                       uint16_t empty, uint16_t already_reserved)
{
    if (owner >= TK_TRACKS || (request & (1u << owner)) || !valid_helpers(request))
        return false;
    if (request & ~(audio & empty & (uint16_t)~already_reserved))
        return false;
    config->helpers = request;
    return true;
}

TkTrackConfig tk_track_defaults(void)
{
    return (TkTrackConfig){.params = tk_defaults(), .selector_octave = -1, .selector_channel = -1};
}

/* TKEY, version, recipe count, helper mask, control words, recipe, metadata. */
enum { RECIPE_OFFSET = 8 + 2 * TK_CONTROL_COUNT };
enum { SELECTOR_OCTAVE = RECIPE_OFFSET + 2 * TK_NOTES, SELECTOR_CHANNEL, CANOPY_MIX, OVERRIDE };
_Static_assert(SELECTOR_OCTAVE == TK_CONFIG_V1_BYTES && OVERRIDE + 1 == TK_CONFIG_BYTES,
               "track record layout");

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static unsigned get_u16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

bool tk_config_encode(const TkTrackConfig *config, uint8_t *data, size_t size)
{
    if (!data || size != TK_CONFIG_BYTES || !tk_valid(&config->params) ||
        !tk_valid_recipe(&config->user) || !valid_helpers(config->helpers) ||
        config->selector_octave < -1 || config->selector_octave > 10 ||
        config->selector_channel < -1 || config->selector_channel > 15 || config->canopy_mix > 127)
        return false;
    memset(data, 0, size);
    memcpy(data, "TKEY", 4);
    data[4] = 2;
    data[5] = config->user.count;
    put_u16(data + 6, config->helpers);
    for (enum TkControl id = TK_CTL_ARP; id < TK_CONTROL_COUNT; ++id) {
        int value = 0;
        tk_get_control(&config->params, id, &value);
        /* Bias signed controls so the representation is independent of C layout. */
        uint16_t stored = (uint16_t)(id == TK_CTL_OCTAVES ? value + 3 : value);
        if (id == TK_CTL_SWING && config->params.legacy_swing)
            stored |= (uint16_t)config->params.legacy_swing << 8;
        put_u16(data + 8 + 2 * id, stored);
    }
    for (uint8_t i = 0; i < config->user.count; ++i) {
        data[RECIPE_OFFSET + 2 * i] = (uint8_t)(config->user.notes[i].interval + 127);
        data[RECIPE_OFFSET + 2 * i + 1] = config->user.notes[i].velocity;
    }
    data[SELECTOR_OCTAVE] = (uint8_t)(config->selector_octave + 1);
    data[SELECTOR_CHANNEL] = (uint8_t)(config->selector_channel + 1);
    data[CANOPY_MIX] = config->canopy_mix;
    data[OVERRIDE] = config->override;
    return true;
}

bool tk_config_decode(TkTrackConfig *config, const uint8_t *data, size_t size)
{
    if (!data || (size != TK_CONFIG_BYTES && size != TK_CONFIG_V1_BYTES) ||
        !tk_bytes_equal(data, "TKEY", 4) || data[5] > TK_NOTES ||
        data[4] != (size == TK_CONFIG_V1_BYTES ? 1 : 2))
        return false;
    TkTrackConfig next = tk_track_defaults();
    next.helpers = (uint16_t)get_u16(data + 6);
    if (data[4] == 2) {
        if (data[SELECTOR_OCTAVE] > 11 || data[SELECTOR_CHANNEL] > 16 || data[CANOPY_MIX] > 127 ||
            data[OVERRIDE] > 1)
            return false;
        next.selector_octave = (int8_t)(data[SELECTOR_OCTAVE] - 1);
        next.selector_channel = (int8_t)(data[SELECTOR_CHANNEL] - 1);
        next.canopy_mix = data[CANOPY_MIX];
        next.override = data[OVERRIDE];
    }
    if (!valid_helpers(next.helpers))
        return false;
    for (enum TkControl id = TK_CTL_ARP; id < TK_CONTROL_COUNT; ++id) {
        int value = (int)get_u16(data + 8 + 2 * id);
        unsigned legacy_swing = data[4] == 2 && id == TK_CTL_SWING ? (unsigned)value >> 8 : 0;
        if (legacy_swing > 75)
            return false;
        if (legacy_swing)
            value &= 255;
        if (!tk_set_control(&next.params, id, id == TK_CTL_OCTAVES ? value - 3 : value))
            return false;
        if (legacy_swing)
            next.params.legacy_swing = (uint8_t)legacy_swing;
    }
    next.user.count = data[5];
    for (uint8_t i = 0; i < TK_NOTES; ++i) {
        uint8_t interval = data[RECIPE_OFFSET + 2 * i], velocity = data[RECIPE_OFFSET + 2 * i + 1];
        if (i < next.user.count) {
            if (interval == UINT8_MAX || velocity > 127)
                return false;
            next.user.notes[i] = (TkRecipeNote){(int8_t)(interval - 127), velocity};
        } else if (interval || velocity)
            return false;
    }
    *config = next;
    return true;
}
