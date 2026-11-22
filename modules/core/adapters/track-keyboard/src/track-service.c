#include "track-service.h"
#include "track-sequence.h"

#include "track-memory.h"

typedef struct {
    TkState live;
    TkState sequence;
    const TkPattern *sequence_pattern;
    TkTrackBackend backend;
    uint16_t sequence_pinned;
    uint8_t number;
    bool live_has_priority, early_seed;
} TkTrack;

#ifdef __m68k__
extern void *tk_fw_alloc(uint32_t size);
static TkTrack *tracks;
#else
static TkTrack tracks[TK_TRACKS];
#endif

bool tk_service_init(void)
{
#ifdef __m68k__
    if (!tracks) {
        TkTrack *memory = tk_fw_alloc(sizeof(TkTrack) * TK_TRACKS);
        if (!memory)
            return false;
        memset(memory, 0, sizeof(TkTrack) * TK_TRACKS);
        tracks = memory;
    }
#endif
    return true;
}
static TkPattern *editing;
static const TkPattern *playing;
static uint32_t edit_generation;
static uint16_t tempo;
static uint16_t manual_retrig_tracks;
static const uint32_t sequence_token_bit = UINT32_C(0x80000000);
static void live_priority(TkTrack *t, bool muting);
static TkTrackConfig track_settings(uint8_t track);
static void configure_live(TkTrack *t);

static bool send_live(void *context, const TkNoteEvent *event)
{
    TkTrack *t = context;
    if (event->on && event->lane == TK_ARP && (manual_retrig_tracks & (1u << t->number)))
        return false;
    return t->backend.output && t->backend.output(t->backend.context, t->number, event);
}

static bool send_sequence(void *context, const TkNoteEvent *event)
{
    TkTrack *t = context;
    /* The two schedulers use different token spaces for the same destination. */
    TkNoteEvent sequenced = *event;
    sequenced.token ^= sequence_token_bit;
    return send_live(t, &sequenced);
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

bool tk_pattern_set_seed(TkPattern *p, uint8_t track, uint8_t step, const TkSeed *seed)
{
    if (!p || track >= TK_TRACKS || step >= TK_STEPS ||
        (seed && (seed->length_ticks || !tk_seed_valid(seed))))
        return false;
    uint16_t index = (uint16_t)(track * TK_STEPS + step + 1);
    TkPatternSeed *available = NULL;
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i) {
        TkPatternSeed *slot = &p->seeds[i];
        if (slot->index == index) {
            if (seed)
                slot->seed = *seed;
            else
                slot->index = 0;
            return true;
        }
        if (!available && !slot->index)
            available = slot;
    }
    if (!seed)
        return true;
    if (!available)
        return false;
    available->seed = *seed;
    available->index = index;
    return true;
}

static const TkStepLocks *find_step(const TkPattern *p, uint8_t track, uint8_t step)
{
    uint16_t index = (uint16_t)(track * TK_STEPS + step + 1);
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        if (p->steps[i].index == index)
            return &p->steps[i].locks;
    return NULL;
}

static bool store_step(TkPattern *p, uint8_t track, uint8_t step, const TkStepLocks *locks)
{
    uint16_t index = (uint16_t)(track * TK_STEPS + step + 1);
    TkPatternStep *slot = NULL;
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i) {
        if (p->steps[i].index == index) {
            slot = &p->steps[i];
            break;
        }
        if (!slot && !p->steps[i].index)
            slot = &p->steps[i];
    }
    if (!locks->mask) {
        if (slot && slot->index == index)
            *slot = (TkPatternStep){0};
        return true;
    }
    if (!slot)
        return false;
    *slot = (TkPatternStep){.index = index, .locks = *locks};
    return true;
}

bool tk_service_store_edit_seed(uint8_t track, uint8_t step, const TkSeed *seed,
                                const TkRecordedShape *shape)
{
    if (!editing || track >= TK_TRACKS || step >= TK_STEPS || !shape)
        return false;
    const TkStepLocks *existing = find_step(editing, track, step);
    TkStepLocks previous = existing ? *existing : (TkStepLocks){0};
    TkStepLocks next = previous;
    if (!tk_set_lock(&next, TK_CTL_CHORD, shape->chord) ||
        !tk_set_lock(&next, TK_CTL_VOICING, shape->voicing) ||
        !tk_set_lock(&next, TK_CTL_STRUM, shape->strum_ms))
        return false;
    next.mask &= (uint16_t)~TK_RECIPE_OVERRIDE;
    if (shape->chord == TK_CHORD_USER && tk_valid_recipe(&shape->user)) {
        next.user = shape->user;
        next.mask |= TK_RECIPE_OVERRIDE;
    }
    if (!store_step(editing, track, step, &next))
        return false;
    if (tk_pattern_set_seed(editing, track, step, seed))
        return true;
    store_step(editing, track, step, &previous);
    return false;
}

bool tk_service_edit_shape(uint8_t track, TkRecordedShape *shape)
{
    if (!tk_trig_ready(track) || !shape)
        return false;
    const TkTrackConfig config = track_settings(track);
    *shape = (TkRecordedShape){.chord = config.params.chord,
                               .voicing = config.params.voicing,
                               .strum_ms = config.params.strum_ms,
                               .user = config.user};
    return true;
}

const TkTrackConfig *tk_service_active_config(uint8_t track)
{
    const TkPattern *p = playing ? playing : editing;
    return p && track < TK_TRACKS ? &p->tracks[track] : NULL;
}

static TkTrackConfig track_settings(uint8_t track)
{
    TkTrackConfig config = editing->tracks[track];
    TkTrackInherit inherit = tracks[track].backend.inherit;
    if (!config.override && inherit) {
        TkTrackConfig legacy;
        if (inherit(track, &legacy)) {
            legacy.helpers = config.helpers;
            if (config.selector_octave >= 0 || config.selector_channel >= 0) {
                legacy.selector_octave = config.selector_octave;
                legacy.selector_channel = config.selector_channel;
            }
            config = legacy;
        }
    }
    return config;
}

static void apply_track_settings(uint8_t track, const TkTrackConfig *config)
{
    editing->tracks[track] = *config;
    if (!playing || playing == editing) {
        configure_live(&tracks[track]);
        live_priority(&tracks[track], tracks[track].live.key_count != 0);
    }
}

static bool resolve_mode(const TkTrack *t, const TkTrackConfig *c, TkParams *params)
{
    if (params->poly || params->arp)
        return true;
    if (!t->backend.inherit || !c->override)
        return false;
    params->poly = 1;
    params->voices = 1;
    params->chord = TK_CHORD_PLAY;
    return true;
}

static void configure_live(TkTrack *t)
{
    const TkPattern *p = playing ? playing : editing;
    if (!t->backend.output)
        return;
    if (p) {
        const TkTrackConfig *config = &p->tracks[t->number];
        TkParams params = config->params;
        resolve_mode(t, config, &params);
        tk_configure(&t->live, &params, &config->user);
    } else {
        tk_panic(&t->live);
    }
}

void tk_service_edit_pattern(TkPattern *p)
{
    ++edit_generation;
    editing = p;
    if (!playing)
        for (unsigned i = 0; i < TK_TRACKS; ++i)
            configure_live(&tracks[i]);
}

void tk_service_play_pattern(const TkPattern *p)
{
    if (playing == p)
        return;
    playing = p;
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        TkTrack *t = &tracks[i];
        if (!t->backend.output)
            continue;
        /* A negative-microtimed destination note can already be sounding. */
        if (!p || t->sequence_pattern != p) {
            tk_stop_transport(&t->sequence);
            t->sequence_pattern = NULL;
        }
        configure_live(t);
        live_priority(t, t->live.key_count != 0);
    }
}

bool tk_service_set_backend(uint8_t track, const TkTrackBackend *backend)
{
    if (track >= TK_TRACKS ||
        (backend && (!backend->output || !backend->voices || backend->voices > TK_NOTES)))
        return false;
    TkTrack *t = &tracks[track];
    tk_panic(&t->live);
    tk_panic(&t->sequence);
    memset(t, 0, sizeof *t);
    t->number = track;
    if (!backend)
        return true;
    t->backend = *backend;
    tk_init(&t->live, send_live, t);
    tk_init(&t->sequence, send_sequence, t);
    tk_set_backend(&t->live, backend->voices, backend->blend);
    tk_set_backend(&t->sequence, backend->voices, backend->blend);
    tk_tick(&t->live, 0, tempo);
    tk_tick(&t->sequence, 0, tempo);
    configure_live(t);
    return true;
}

void tk_service_set_voice_capacity(uint8_t track, uint8_t voices)
{
    if (track >= TK_TRACKS || !voices || voices > TK_NOTES)
        return;
    TkTrack *t = &tracks[track];
    if (!t->backend.output || t->backend.voices == voices)
        return;
    t->backend.voices = voices;
    tk_set_backend(&t->live, voices, t->backend.blend);
    tk_set_backend(&t->sequence, voices, t->backend.blend);
}

uint16_t tk_service_helpers(uint8_t track)
{
    return tk_trig_ready(track) ? editing->tracks[track].helpers : 0;
}

bool tk_service_assign_helpers(uint8_t track, uint16_t request, uint16_t selectable)
{
    if (!tk_trig_ready(track) || tracks[track].backend.inherit)
        return false;
    TkTrackConfig *config = &editing->tracks[track];
    return tk_assign_helpers(config, track, request, UINT16_MAX, selectable | config->helpers, 0);
}

static bool available(uint8_t track)
{
    const TkTrackBackend *backend = &tracks[track].backend;
    return backend->output && (!backend->available || backend->available(track));
}

void tk_service_cancel_track(uint8_t track)
{
    if (track >= TK_TRACKS || !tracks[track].backend.output)
        return;
    TkTrack *t = &tracks[track];
    tk_panic(&t->live);
    tk_stop_transport(&t->sequence);
    t->sequence_pattern = NULL;
    live_priority(t, false);
}

bool tk_service_enabled(uint8_t track)
{
    return track < TK_TRACKS && (playing || editing) && available(track) &&
           (tracks[track].live.params.arp || tracks[track].live.params.poly);
}

static void live_priority(TkTrack *t, bool muting)
{
    muting = muting && (t->live.params.arp || t->live.params.poly);
    if (t->live_has_priority == muting)
        return;
    t->live_has_priority = muting;
    tk_mute_output(&t->sequence, muting);
}

bool tk_service_press(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                      uint8_t velocity)
{
    if (!tk_service_enabled(track))
        return false;
    TkTrack *t = &tracks[track];
    live_priority(t, true);
    tk_press(&t->live, source, channel, id, pitch, velocity);
    live_priority(t, t->live.key_count != 0);
    /* Capacity declines still belong here so a matching stock note does not
     * start outside the ownership ledger.
     */
    return true;
}

void tk_service_release(uint8_t track, uint8_t source, uint8_t channel, uint32_t id)
{
    if (track >= TK_TRACKS || !tracks[track].backend.output)
        return;
    TkTrack *t = &tracks[track];
    tk_release(&t->live, source, channel, id);
    live_priority(t, t->live.key_count != 0);
}

void tk_service_pedal(uint8_t track, uint8_t source, uint8_t channel, bool down)
{
    if (track >= TK_TRACKS || !tracks[track].backend.output)
        return;
    TkTrack *t = &tracks[track];
    tk_pedal(&t->live, source, channel, down);
    live_priority(t, t->live.key_count != 0);
}

void tk_service_clear_source(uint8_t track, uint8_t source, uint8_t channel)
{
    if (track >= TK_TRACKS || !tracks[track].backend.output)
        return;
    TkTrack *t = &tracks[track];
    tk_clear_source(&t->live, source, channel);
    live_priority(t, t->live.key_count != 0);
}

bool tk_service_selector(uint8_t track, bool active, int8_t *octave, int8_t *channel)
{
    const TkPattern *p = active && playing ? playing : editing;
    if (!p || track >= TK_TRACKS)
        return false;
    const TkTrackConfig *config = &p->tracks[track];
    bool enable = !active || tracks[track].live.params.chord != TK_CHORD_PLAY;
    *octave = enable ? config->selector_octave : -1;
    *channel = enable ? config->selector_channel : -1;
    return true;
}

bool tk_service_write_selector(uint8_t track, int8_t octave, int8_t channel)
{
    if (!tk_trig_ready(track) || octave < -1 || octave > 10 || channel < -1 || channel > 15 ||
        (octave >= 0 && channel >= 0))
        return false;
    TkTrackConfig *config = &editing->tracks[track];
    config->selector_octave = octave;
    config->selector_channel = channel;
    return true;
}

void tk_service_select_chord(uint8_t track, enum TkChord chord)
{
    if (!tk_service_enabled(track) || chord < TK_CHORD_MAJ || chord >= TK_CHORD_COUNT)
        return;
    TkTrack *t = &tracks[track];
    if (t->live.params.chord == TK_CHORD_PLAY)
        return;
    if (editing && (!playing || playing == editing)) {
        tk_trig_write(track, TK_CTL_CHORD, chord);
        return;
    }
    TkParams params = t->live.params;
    params.chord = (uint8_t)chord;
    tk_configure(&t->live, &params, &t->live.user);
}

void tk_service_sequence_policy_for(TkSequencePolicy *policy, const TkPattern *p)
{
    memset(policy, 0, sizeof *policy);
    if (!p)
        return;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        TkParams params = p->tracks[track].params;
        if (available(track) && resolve_mode(&tracks[track], &p->tracks[track], &params))
            for (unsigned i = 0; i < TK_STEPS / 32u; ++i)
                policy->steps[track][i] = UINT32_MAX;
    }
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i) {
        const TkPatternStep *entry = &p->steps[i];
        if (!entry->index || entry->index > TK_TRACKS * TK_STEPS ||
            !(entry->locks.mask & ((1u << TK_CTL_ARP) | (1u << TK_CTL_POLY))))
            continue;
        uint8_t track = (uint8_t)((entry->index - 1u) / TK_STEPS);
        uint8_t step = (uint8_t)((entry->index - 1u) % TK_STEPS);
        if (!available(track))
            continue;
        TkParams params;
        TkRecipe recipe;
        uint32_t bit = UINT32_C(1) << (step % 32u);
        if (tk_resolve_step(&p->tracks[track], &entry->locks, &params, &recipe) &&
            resolve_mode(&tracks[track], &p->tracks[track], &params))
            policy->steps[track][step / 32u] |= bit;
        else
            policy->steps[track][step / 32u] &= ~bit;
    }
}

void tk_service_sequence_policy(TkSequencePolicy *policy)
{
    tk_service_sequence_policy_for(policy, playing ? playing : editing);
}

void tk_service_end_seed(uint8_t track)
{
    if (track < TK_TRACKS && tracks[track].backend.output) {
        tk_stop_seed(&tracks[track].sequence);
        tracks[track].sequence_pattern = NULL;
    }
}

bool tk_service_seed_for(const TkPattern *p, uint8_t track, uint8_t step, const TkSeed *seed)
{
    if (!p || track >= TK_TRACKS || step >= TK_STEPS || !available(track))
        return false;
    TkTrack *t = &tracks[track];
    const TkStepLocks *locked = find_step(p, track, step);
    TkParams params;
    TkRecipe recipe;
    if (!tk_resolve_step(&p->tracks[track], locked, &params, &recipe))
        return false;
    if (!tk_seed_valid(seed))
        return false;
    TkSeed effective = *seed;
    const TkSeed *recorded = tk_pattern_seed(p, track, step);
    if (recorded) {
        effective.count = recorded->count;
        for (uint8_t i = 0; i < recorded->count; ++i) {
            TkSeedNote note = recorded->notes[i];
            if (effective.length_ticks && note.gate_ticks > effective.length_ticks)
                note.gate_ticks = effective.length_ticks;
            effective.notes[i] = note;
        }
    }
    uint16_t pinned = locked ? locked->mask : 0;
    if (t->backend.prepare_seed &&
        !t->backend.prepare_seed(track, &params, &recipe, &effective, recorded != NULL, &pinned))
        return false;
    if (!resolve_mode(t, &p->tracks[track], &params)) {
        tk_stop_transport(&t->sequence);
        t->sequence_pattern = NULL;
        return false;
    }
    if (params.chord != TK_CHORD_PLAY)
        effective.count = 1;
    if (t->sequence_pattern && t->sequence_pattern != p)
        tk_stop_transport(&t->sequence);
    else
        tk_stop_seed(&t->sequence);
    t->sequence_pattern = NULL;
    if (!tk_configure(&t->sequence, &params, &recipe) || !tk_seed(&t->sequence, &effective))
        return false;
    t->sequence_pattern = p;
    t->sequence_pinned = pinned;
    t->early_seed = playing && p != playing;
    tk_tick(&t->sequence, 0, tempo);
    return true;
}

bool tk_service_seed(uint8_t track, uint8_t step, const TkSeed *seed)
{
    return tk_service_seed_for(playing ? playing : editing, track, step, seed);
}

void tk_service_tick(uint32_t elapsed_us, uint16_t bpm)
{
    if (tk_valid_tempo(bpm))
        tempo = bpm;
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        TkTrack *t = &tracks[i];
        if (!t->backend.output)
            continue;
        tk_tick(&t->live, elapsed_us, tempo);
        tk_tick(&t->sequence, elapsed_us, tempo);
    }
}

void tk_service_manual_retrig(uint8_t track, bool held)
{
    if (track >= TK_TRACKS)
        return;
    uint16_t mask = (uint16_t)(1u << track);
    if (held) {
        manual_retrig_tracks |= mask;
        tk_retire_arp(&tracks[track].live);
        tk_retire_arp(&tracks[track].sequence);
    } else {
        manual_retrig_tracks &= (uint16_t)~mask;
    }
}

void tk_service_boundary(void)
{
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        TkTrack *t = &tracks[i];
        if (!t->backend.output)
            continue;
        tk_pattern_boundary(&t->live);
        uint8_t cursor = t->sequence.cursor;
        tk_pattern_boundary(&t->sequence);
        if (playing && t->early_seed && t->sequence_pattern == playing) {
            /* The first destination onset already advanced its traversal.
             * Record the actual cycle origin without replaying that note. */
            t->sequence.cursor = cursor;
            t->early_seed = false;
        }
    }
}

void tk_service_stop(void)
{
    tk_service_play_pattern(NULL);
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        if (!tracks[i].backend.output)
            continue;
        tk_stop_transport(&tracks[i].live);
        tk_stop_transport(&tracks[i].sequence);
        tracks[i].sequence_pattern = NULL;
    }
}

bool tk_trig_ready(uint8_t track)
{
    return editing && track < TK_TRACKS && tracks[track].backend.output;
}

uint8_t tk_trig_voice_capacity(uint8_t track)
{
    return tk_trig_ready(track) ? tracks[track].backend.voices : 0;
}

uint16_t tk_trig_pitch_display(uint8_t track, uint16_t *held)
{
    *held = 0;
    if (!tk_trig_ready(track))
        return 0;
    const TkTrack *t = &tracks[track];
    for (unsigned i = 0; i < t->live.key_count; ++i)
        *held |= (uint16_t)(1u << (t->live.keys[i].pitch % 12u));
    uint16_t sounding = 0;
    const TkState *state = t->live_has_priority ? &t->live : &t->sequence;
    for (unsigned i = 0; i < TK_NOTES; ++i)
        if (state->voices[i].sent)
            sounding |= (uint16_t)(1u << (state->voices[i].note.pitch % 12u));
    if (state->arp_voice.sent)
        sounding |= (uint16_t)(1u << (state->arp_voice.note.pitch % 12u));
    return sounding;
}

int tk_trig_arp_pitch(uint8_t track, uint32_t *token)
{
    if (!tk_trig_ready(track))
        return -1;
    const TkTrack *t = &tracks[track];
    const TkState *state = t->live_has_priority ? &t->live : &t->sequence;
    if (!state->arp_voice.sent)
        return -1;
    *token = state->arp_voice.note.token;
    if (state == &t->sequence)
        *token ^= sequence_token_bit;
    return state->arp_voice.note.pitch;
}

uint8_t tk_trig_held_pitches(uint8_t track, uint8_t pitches[TK_NOTES])
{
    if (!tk_trig_ready(track))
        return 0;
    const TkState *live = &tracks[track].live;
    uint8_t count = 0;
    for (uint8_t i = 0; i < live->key_count && count < TK_NOTES; ++i) {
        const TkKey *key = &live->keys[i];
        if (key->stolen)
            continue;
        uint8_t j = 0;
        while (j < count && pitches[j] != key->pitch)
            ++j;
        if (j == count)
            pitches[count++] = key->pitch;
    }
    return count;
}

bool tk_trig_display(uint8_t track, TkParams *out)
{
    if (!tk_trig_ready(track))
        return false;
    *out = track_settings(track).params;
    return true;
}

bool tk_trig_write(uint8_t track, enum TkControl control, int value)
{
    if (!tk_trig_ready(track))
        return false;
    TkTrackConfig config = track_settings(track);
    if (!tk_set_control(&config.params, control, value))
        return false;
    config.override = true;
    bool traversal = control == TK_CTL_RATE || control == TK_CTL_ORDER || control == TK_CTL_OCTAVES;
    TkTrack *t = &tracks[track];
    if (traversal && (editing->tracks[track].override || !t->backend.inherit)) {
        editing->tracks[track] = config;
        if (!playing || playing == editing)
            tk_arp_traversal(&t->live, &config.params);
    } else {
        apply_track_settings(track, &config);
    }
    if (traversal && t->sequence_pattern == editing && !(t->sequence_pinned & (1u << control))) {
        TkParams params = t->sequence.params;
        tk_set_control(&params, control, value);
        tk_arp_traversal(&t->sequence, &params);
    }
    return true;
}

bool tk_trig_lock_display(uint8_t track, uint8_t step, TkParams *out, uint16_t *lock_mask)
{
    if (!tk_trig_ready(track) || step >= TK_STEPS)
        return false;
    const TkStepLocks *locks = find_step(editing, track, step);
    TkRecipe ignored;
    TkTrackConfig config = track_settings(track);
    if (!tk_resolve_step(&config, locks, out, &ignored))
        return false;
    *lock_mask = locks ? locks->mask : 0;
    return true;
}

bool tk_trig_lock_write(uint8_t track, uint8_t step, enum TkControl control, int value, bool clear)
{
    if (!tk_trig_ready(track) || step >= TK_STEPS || control < 0 || control >= TK_CTL_VOICES)
        return false;
    const TkStepLocks *prev = find_step(editing, track, step);
    TkStepLocks next = prev ? *prev : (TkStepLocks){0};
    if (clear) {
        tk_clear_lock(&next, control);
        if (control == TK_CTL_CHORD)
            next.mask &= (uint16_t)~TK_RECIPE_OVERRIDE;
    } else if (!tk_set_lock(&next, control, value)) {
        return false;
    }
    if (!store_step(editing, track, step, &next))
        return false;
    if (!clear && !editing->tracks[track].override && tracks[track].backend.inherit) {
        TkTrackConfig config = track_settings(track);
        config.override = true;
        apply_track_settings(track, &config);
    }
    return true;
}

bool tk_trig_lock_batch(uint8_t track, const uint32_t steps[4], enum TkControl control, int value,
                        bool clear)
{
    if (!tk_trig_ready(track) || !steps || control < 0 || control >= TK_CTL_VOICES)
        return false;
    if (!clear) {
        TkStepLocks validated = {0};
        if (!tk_set_lock(&validated, control, value))
            return false;
        unsigned available = 0, needed = 0;
        for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
            available += editing->steps[i].index == 0;
        for (unsigned step = 0; step < TK_STEPS; ++step)
            if ((steps[step / 32u] & (1u << (step % 32u))) &&
                !find_step(editing, track, (uint8_t)step))
                ++needed;
        if (needed > available)
            return false;
    }
    for (unsigned step = 0; step < TK_STEPS; ++step)
        if (steps[step / 32u] & (1u << (step % 32u)))
            tk_trig_lock_write(track, (uint8_t)step, control, value, clear);
    return true;
}

bool tk_notes_open(uint8_t track, uint8_t step, bool step_scope, TkNotesDraft *draft)
{
    if (!tk_trig_ready(track) || (step_scope && step >= TK_STEPS))
        return false;
    const TkStepLocks *locks = step_scope ? find_step(editing, track, step) : NULL;
    TkRecipe recipe;
    TkParams ignored;
    TkTrackConfig config = track_settings(track);
    if (!tk_resolve_step(&config, locks, &ignored, &recipe))
        return false;
    *draft = (TkNotesDraft){.pattern = editing,
                            .edit_generation = edit_generation,
                            .recipe = recipe,
                            .track = track,
                            .step = step,
                            .open = true,
                            .step_scope = step_scope};
    return true;
}

void tk_notes_learn(TkNotesDraft *draft)
{
    if (!draft->open)
        return;
    draft->learn = true;
    draft->clear = false;
    draft->recipe.count = 0;
}

bool tk_notes_append(TkNotesDraft *draft, uint8_t pitch, uint8_t velocity)
{
    if (!draft->open || !draft->learn || pitch > 127 || !velocity || velocity > 127 ||
        draft->recipe.count == TK_NOTES)
        return false;
    if (!draft->recipe.count)
        draft->first_pitch = pitch;
    draft->recipe.notes[draft->recipe.count++] =
        (TkRecipeNote){(int8_t)(pitch - draft->first_pitch), velocity};
    return true;
}

bool tk_notes_commit(TkNotesDraft *draft)
{
    if (!draft->open || draft->pattern != editing || draft->edit_generation != edit_generation ||
        !tk_trig_ready(draft->track) || (!draft->recipe.count && !draft->clear) ||
        !tk_valid_recipe(&draft->recipe))
        return false;
    enum TkChord chord = draft->recipe.count ? TK_CHORD_USER : TK_CHORD_PLAY;
    if (draft->step_scope) {
        const TkStepLocks *prev = find_step(editing, draft->track, draft->step);
        TkStepLocks next = prev ? *prev : (TkStepLocks){0};
        next.user = draft->recipe;
        next.mask |= TK_RECIPE_OVERRIDE;
        if (!tk_set_lock(&next, TK_CTL_CHORD, chord) ||
            !store_step(editing, draft->track, draft->step, &next))
            return false;
        if (!editing->tracks[draft->track].override && tracks[draft->track].backend.inherit) {
            TkTrackConfig config = track_settings(draft->track);
            config.override = true;
            apply_track_settings(draft->track, &config);
        }
    } else {
        TkTrackConfig config = track_settings(draft->track);
        config.user = draft->recipe;
        config.params.chord = chord;
        config.override = true;
        apply_track_settings(draft->track, &config);
    }
    draft->open = false;
    return true;
}
