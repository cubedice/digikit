#include "track-canopy.h"

#if defined(__m68k__) && defined(__GNUC__)
#pragma GCC optimize("no-jump-tables", "no-tree-switch-conversion")
#endif

#include "track-midi.h"
#include "track-sequence.h"

#include "../../instrument-play/src/cf/src-control.h"
#include "../../instrument-play/src/cf/shared-notes.h"

extern uint32_t dkrt_instrument_type(void);
extern int instrument_track_owned(uint32_t track);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

static const uint8_t order[] = {TK_ORDER_UP, TK_ORDER_DOWN, TK_ORDER_UPDN, TK_ORDER_PLAY,
                                TK_ORDER_RANDOM};
static const uint8_t rate[] = {3, 6, 8, 9, TK_RATE_5Q, 11, TK_RATE_7Q, 12};

static void set_swing(TkParams *params, uint16_t value)
{
    params->legacy_swing = (uint8_t)value;
    unsigned swing = 50u + value / 2u;
    params->swing = (uint8_t)(swing > 75 ? 75 : swing);
}

static void set_user_recipe(TkRecipe *recipe, const CpPreset *saved)
{
    recipe->count = saved->shape.count;
    for (uint8_t i = 0; i < saved->shape.count; ++i)
        recipe->notes[i] = (TkRecipeNote){.interval = saved->shape.intervals[i]};
}

bool tk_canopy_is_owner(uint8_t track, int type)
{
    return type > 0 && (uint32_t)type == dkrt_instrument_type() && instrument_track_owned(track);
}

static bool legacy(uint8_t track, TkTrackConfig *out)
{
    CpPreset saved;
    if (!instrument_track_keyboard_legacy(track, &saved) || !cp_valid(&saved))
        return false;
    const uint16_t *p = saved.control;
    TkTrackConfig config = tk_track_defaults();
    config.canopy_mix = (uint8_t)p[CP_ARP_MIX];
    config.params.poly = config.canopy_mix < 127;
    config.params.arp = config.canopy_mix != 0;
    config.params.order = order[p[CP_ORDER]];
    config.params.rate = rate[p[CP_RATE] - 1];
    config.params.gate = (uint8_t)p[CP_GATE];
    config.params.octaves = (int8_t)p[CP_OCTAVES] - 3;
    set_swing(&config.params, p[CP_SWING]);
    config.params.reset = (uint8_t)p[CP_RESTART];
    config.params.strum_ms = p[CP_STRUM];
    if (saved.mode == KB_ROOT) {
        config.params.chord = (uint8_t)p[CP_CHORD] + 1u;
        config.params.voicing = (uint8_t)p[CP_VOICING];
        if (config.params.chord == TK_CHORD_USER)
            set_user_recipe(&config.user, &saved);
    }
    *out = config;
    return true;
}

static volatile uint16_t routed;
volatile uint16_t tk_canopy_fallback;
static IsnSnapshot sequence_snapshot[TK_TRACKS];

bool tk_canopy_routed(uint8_t track)
{
    return track < TK_TRACKS && (routed & (1u << track)) != 0;
}

static bool send(void *unused, uint8_t track, const TkNoteEvent *event)
{
    (void)unused;
    IsnSnapshot snapshot = {.valid = 1};
    if (event->on && (event->token & UINT32_C(0x80000000))) {
        snapshot = sequence_snapshot[track];
        snapshot.lanes = event->lanes;
    }
    return instrument_track_keyboard_locked_note(track, event->token, event->pitch, event->velocity,
                                                 event->on, event->lane, &snapshot) != 0;
}

static void apply_control(TkParams *p, uint32_t id, uint32_t value)
{
    switch (id) {
    case CP_CHORD:
        p->chord = (uint8_t)value;
        break;
    case CP_VOICING:
        p->voicing = (uint8_t)value;
        break;
    case CP_STRUM:
        p->strum_ms = (uint16_t)value;
        break;
    case CP_ARP_MIX:
        p->poly = value < 127;
        p->arp = value != 0;
        break;
    case CP_RATE:
        p->rate = rate[value ? value - 1u : 0];
        break;
    case CP_ORDER:
        p->order = order[value];
        break;
    case CP_OCTAVES:
        p->octaves = (int8_t)value - 3;
        break;
    case CP_GATE:
        p->gate = (uint8_t)(value ? value : 1u);
        break;
    case CP_SWING:
        set_swing(p, (uint16_t)value);
        break;
    case CP_RESTART:
        p->reset = (uint8_t)value;
        break;
    }
}

static bool prepare_seed(uint8_t track, TkParams *params, TkRecipe *recipe, TkSeed *seed,
                         bool recorded)
{
    const TkSequenceAudio *audio = tk_sequence_audio(track);
    IsnSnapshot snapshot = {.valid = 1};
    uint64_t seen = 0;
    TkSeedNote root = seed->notes[0];
    int8_t intervals[ILP_NOTE_LAST - ILP_NOTE_FIRST + 1] = {0};
    for (uint8_t i = 0; audio && i < audio->count; ++i) {
        const TkSequenceLock *lock = &audio->locks[i];
        uint32_t id, value;
        if (!ilp_event_pair(lock->parameter, (int16_t)lock->value, &id, &value))
            continue;
        uint64_t bit = UINT64_C(1) << id;
        if (seen & bit)
            return false;
        seen |= bit;
        if (id >= ILP_NOTE_FIRST && id <= ILP_NOTE_LAST) {
            intervals[id - ILP_NOTE_FIRST] = (int8_t)((int)value - ILP_NOTE_CENTER);
            continue;
        }
        if (snapshot.count == MI_LOCKS)
            return false;
        snapshot.locks[snapshot.count++] = (uint16_t)((id << 10) | value);
        apply_control(params, id, value);
        if (id == 34) {
            uint32_t sr = dkrt_irq_mask();
            snapshot.source_valid = (uint8_t)instrument_lock_source(track, value, snapshot.source);
            dkrt_irq_restore(sr);
            if (!snapshot.source_valid)
                return false;
        }
    }
    if (!recorded)
        for (unsigned i = 0; i < sizeof intervals; ++i) {
            int pitch = root.pitch + intervals[i];
            if (!intervals[i] || pitch < 0 || pitch > 127 || seed->count == TK_NOTES)
                continue;
            bool duplicate = false;
            for (uint8_t j = 0; j < seed->count; ++j)
                duplicate |= seed->notes[j].pitch == pitch;
            if (!duplicate) {
                TkSeedNote note = root;
                note.pitch = (uint8_t)pitch;
                seed->notes[seed->count++] = note;
                params->chord = TK_CHORD_PLAY;
            }
        }
    if (params->chord == TK_CHORD_USER && !recipe->count) {
        CpPreset saved;
        if (instrument_track_keyboard_legacy(track, &saved) && cp_valid(&saved))
            set_user_recipe(recipe, &saved);
    }
    sequence_snapshot[track] = snapshot;
    return true;
}

void tk_canopy_backend(TkTrackBackend *out)
{
    *out = (TkTrackBackend){.output = send,
                            .voices = 4,
                            .blend = true,
                            .inherit = legacy,
                            .available = tk_canopy_routed,
                            .prepare_seed = prepare_seed};
}

void tk_canopy_sync(uint8_t track, bool active, uint32_t pattern)
{
    const TkTrackConfig *config = active ? tk_service_active_config(track) : NULL;
    unsigned lanes = 0;
    bool fallback = config && config->override && tk_sequence_canopy_sound_locks(pattern, track);
    uint16_t bit = (uint16_t)(1u << track);
    if (fallback)
        tk_canopy_fallback |= bit;
    else
        tk_canopy_fallback &= (uint16_t)~bit;
    if (config && config->override && !fallback) {
        lanes = config->params.poly | (unsigned)config->params.arp << 1;
        if (!lanes)
            lanes = 1;
    }
    if (!lanes && (routed & bit)) {
        tk_midi_cancel_track(track);
        tk_service_cancel_track(track);
    }
    if (lanes)
        routed |= bit;
    else
        routed &= (uint16_t)~bit;
    instrument_src_track_keyboard(track, active);
    instrument_track_keyboard_lanes(track, lanes);
}
