#include "track-canopy.h"

#if defined(__m68k__) && defined(__GNUC__)
#pragma GCC optimize("no-jump-tables", "no-tree-switch-conversion")
#endif

#include "track-midi.h"
#include "track-sequence.h"

#include "../../instrument-play/src/cf/src-control.h"
#include "../../instrument-play/src/cf/shared-notes.h"

extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

/* CANOPY's host (core/instrument-play) is optional: in an image without it these are 0, every track is a track CANOPY does not own,
 * and the bridge below is not used (docs/plans/fork-image-diet.md section 9). */
extern int cp_valid(const CpPreset *p) __attribute__((weak));
extern int ilp_event_pair(uint16_t parameter, int16_t packed, uint32_t *id, uint32_t *value) __attribute__((weak));
extern int instrument_lock_source(uint32_t track, uint32_t wave, uint16_t *out) __attribute__((weak));
extern int instrument_locked_sound(uint32_t index, uintptr_t expected, CpPreset *out, uint16_t *tune) __attribute__((weak));
extern void instrument_src_track_keyboard(uint32_t track, int active) __attribute__((weak));
extern int instrument_track_keyboard_lanes(uint32_t track, uint32_t lanes) __attribute__((weak));
extern int instrument_track_keyboard_legacy(uint32_t track, CpPreset *out) __attribute__((weak));
extern int instrument_track_keyboard_locked_note(uint32_t track, uint32_t token, uint32_t pitch, uint32_t velocity, uint32_t on,
                                                 uint32_t lane, const struct IsnSnapshot *snapshot) __attribute__((weak));
extern int instrument_track_owned(uint32_t track) __attribute__((weak));

/* All of CANOPY's host is in the image. */
static bool canopy_linked(void)
{
    return cp_valid && ilp_event_pair && instrument_lock_source && instrument_locked_sound && instrument_src_track_keyboard &&
           instrument_track_keyboard_lanes && instrument_track_keyboard_legacy && instrument_track_keyboard_locked_note &&
           instrument_track_owned;
}

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
    return canopy_linked() && type > 0 && (uint32_t)type == dkrt_instrument_type() && instrument_track_owned(track);
}

static bool legacy(uint8_t track, TkTrackConfig *out)
{
    CpPreset saved;
    if (!canopy_linked() || !instrument_track_keyboard_legacy(track, &saved) || !cp_valid(&saved))
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
    if (!canopy_linked())
        return false;
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

static uint32_t preset_wave(const CpPreset *p)
{
    if (p->family == 3)
        return 7;
    return p->family == 2 ? 2u + p->control[CP_WT_TABLE] : p->family;
}

static void load_preset_source(IsnSnapshot *snapshot, const CpPreset *saved, uint32_t wave)
{
    const uint16_t *p = saved->control;
    snapshot->sound_controls = MI_SOUND_VALID | wave | ((uint32_t)p[CP_DRIFT] << 3) |
                               ((uint32_t)p[CP_ATTACK] << 10) | ((uint32_t)p[CP_RELEASE] << 17) |
                               ((uint32_t)p[CP_ARP_MIX] << 24);
    snapshot->source_valid = 1;
    snapshot->source[0] = wave == 7 ? saved->fm[0] : p[CP_COLOR];
    for (uint32_t i = 0; i < 12; ++i)
        snapshot->source[1 + i] = wave == 7 ? saved->fm[1 + i] : p[CP_WT_TABLE + i];
    for (uint32_t i = 0; i < 4; ++i)
        snapshot->source[13 + i] = p[CP_WTFM_DEPTH + i];
    if (wave >= 2 && wave < 7)
        snapshot->source[1] = (uint16_t)(wave - 2);
}

static bool prepare_seed(uint8_t track, TkParams *params, TkRecipe *recipe, TkSeed *seed,
                         bool recorded, uint16_t *pinned_controls)
{
    if (!canopy_linked())
        return false;
    const TkSequenceAudio *audio = tk_sequence_audio(track);
    IsnSnapshot snapshot = {.valid = 1};
    CpPreset saved;
    bool sound_lock = audio && audio->pool_sound;
    if (sound_lock && !instrument_locked_sound(audio->pool_sound - 1u, audio->sound, &saved,
                                               &snapshot.sound_tune))
        return false;
    int32_t wave = sound_lock ? (int32_t)preset_wave(&saved) : -1;
    uint64_t seen = 0;
    TkSeedNote root = seed->notes[0];
    int8_t intervals[ILP_NOTE_LAST - ILP_NOTE_FIRST + 1] = {0};
    for (uint8_t i = 0; audio && i < audio->count; ++i) {
        const TkSequenceLock *lock = &audio->locks[i];
        /* The native TUNE p-lock overrides the Sound's raw TUNE. */
        if (sound_lock && lock->parameter == 25 && lock->value >= 0x400 && lock->value <= 0x7c00)
            snapshot.sound_tune = lock->value;
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
        if (id == 34)
            wave = (int32_t)value;
    }
    if (seen & (UINT64_C(1) << CP_RATE))
        *pinned_controls |= 1u << TK_CTL_RATE;
    if (seen & (UINT64_C(1) << CP_ORDER))
        *pinned_controls |= 1u << TK_CTL_ORDER;
    if (seen & (UINT64_C(1) << CP_OCTAVES))
        *pinned_controls |= 1u << TK_CTL_OCTAVES;
    if (sound_lock)
        load_preset_source(&snapshot, &saved, (uint32_t)wave);
    else if (wave >= 0) {
        uint32_t sr = dkrt_irq_mask();
        snapshot.source_valid =
            (uint8_t)instrument_lock_source(track, (uint32_t)wave, snapshot.source);
        dkrt_irq_restore(sr);
        if (!snapshot.source_valid)
            return false;
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
        if (sound_lock || (instrument_track_keyboard_legacy(track, &saved) && cp_valid(&saved)))
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
    bool fallback = config && config->override && tk_sequence_canopy_sound_fallback(pattern, track);
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
    if (canopy_linked()) {
        instrument_src_track_keyboard(track, active);
        instrument_track_keyboard_lanes(track, lanes);
    }
}
