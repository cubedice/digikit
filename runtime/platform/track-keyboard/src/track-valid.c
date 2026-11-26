/* The defaults and validators of the track keyboard's parameters, recipes and seeds: pure functions that the pattern storage
 * (track-pattern.c, track-config.c) uses in the image while services/track-keyboard.dks is not loaded, and the service through
 * the image table (docs/plans/fork-image-diet.md section 9.2). */
#include "track-keyboard.h"
#include "track-service.h"
#include "track-memory.h"

static const uint8_t divisions[] = {1,  2,  3,  4,  5,  6,  8,  10, 12, 16,
                                    20, 24, 32, 40, 48, 64, 80, 7,  5};

TkParams tk_defaults(void)
{
    return (TkParams){.order = TK_ORDER_PLAY, .gate = 50, .rate = 9, .swing = 50, .voices = 4};
}

bool tk_rate_division(uint8_t rate, uint8_t *numerator, uint8_t *denominator)
{
    if (rate >= sizeof divisions)
        return false;
    *numerator = rate >= TK_RATE_7Q ? 1 : 4;
    *denominator = divisions[rate];
    return true;
}

bool tk_valid(const TkParams *p)
{
    return p && p->arp <= 1 && p->poly <= 1 && p->order <= TK_ORDER_RANDOM && p->gate >= 1 &&
           p->gate <= 100 && p->rate < sizeof divisions && p->octaves >= -3 && p->octaves <= 3 &&
           p->swing >= 50 && p->swing <= 75 && p->legacy_swing <= 75 &&
           p->reset <= TK_RESET_CONTINUE && p->chord < TK_CHORD_COUNT &&
           p->voicing <= TK_VOICE_INV2 && p->strum_ms <= 500 && p->voices > 0 &&
           p->voices <= TK_NOTES;
}

bool tk_valid_recipe(const TkRecipe *recipe)
{
    if (!recipe || recipe->count > TK_NOTES)
        return false;
    for (uint8_t i = 0; i < recipe->count; ++i)
        if (recipe->notes[i].interval < -127 || recipe->notes[i].velocity > 127)
            return false;
    return true;
}

bool tk_seed_valid(const TkSeed *seed)
{
    if (!seed || !seed->count || seed->count > TK_NOTES)
        return false;
    if (seed->notes[0].delay_us != 0)
        return false;
    uint32_t last_delay = 0;
    for (uint8_t i = 0; i < seed->count; ++i) {
        TkSeedNote note = seed->notes[i];
        if (note.pitch > 127 || !note.velocity || note.velocity > 127 ||
            note.delay_us < last_delay ||
            (seed->length_ticks && note.gate_ticks > seed->length_ticks))
            return false;
        last_delay = note.delay_us;
    }
    return true;
}

void tk_pattern_init(TkPattern *p)
{
    memset(p, 0, sizeof *p);
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        p->tracks[track] = tk_track_defaults();
}

const TkSeed *tk_pattern_seed(const TkPattern *p, uint8_t track, uint8_t step)
{
    if (!p || track >= TK_TRACKS || step >= TK_STEPS)
        return NULL;
    uint16_t index = (uint16_t)(track * TK_STEPS + step + 1);
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i)
        if (p->seeds[i].index == index)
            return &p->seeds[i].seed;
    return NULL;
}
