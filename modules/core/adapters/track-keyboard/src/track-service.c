#include "track-service.h"

#include "track-memory.h"

typedef struct {
    TkState live;
    TkState sequence;
    TkTrackBackend backend;
    uint8_t number;
    bool live_has_priority;
} TkTrack;

static TkTrack tracks[TK_TRACKS];
static TkPattern *editing;
static const TkPattern *playing;
static uint32_t edit_generation;
static uint16_t tempo;
static void live_priority(TkTrack *t, bool muting);

static bool send_live(void *context, const TkNoteEvent *event)
{
    TkTrack *t = context;
    return t->backend.output && t->backend.output(t->backend.context, t->number, event);
}

static bool send_sequence(void *context, const TkNoteEvent *event)
{
    TkTrack *t = context;
    /* The two schedulers use different token spaces for the same destination. */
    TkNoteEvent sequenced = *event;
    sequenced.token ^= 0x80000000u;
    return send_live(t, &sequenced);
}

void tk_pattern_init(TkPattern *p)
{
    memset(p, 0, sizeof *p);
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        p->tracks[track].params = tk_defaults();
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

static void configure_live(TkTrack *t)
{
    const TkPattern *p = playing ? playing : editing;
    if (!t->backend.output)
        return;
    if (p) {
        const TkTrackConfig *config = &p->tracks[t->number];
        if (!(config->params.poly || config->params.arp))
            tk_panic(&t->live);
        tk_configure(&t->live, &config->params, &config->user);
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
        tk_stop_transport(&t->sequence);
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

bool tk_service_enabled(uint8_t track)
{
    return track < TK_TRACKS && (playing || editing) && tracks[track].backend.output &&
           (tracks[track].live.params.arp || tracks[track].live.params.poly);
}

static void live_priority(TkTrack *t, bool muting)
{
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

bool tk_service_seed(uint8_t track, uint8_t step, const TkSeed *seed)
{
    const TkPattern *p = playing ? playing : editing;
    if (!p || track >= TK_TRACKS || step >= TK_STEPS || !tracks[track].backend.output)
        return false;
    TkTrack *t = &tracks[track];
    const TkStepLocks *locked = find_step(p, track, step);
    TkParams params;
    TkRecipe recipe;
    if (!tk_resolve_step(&p->tracks[track], locked, &params, &recipe))
        return false;
    if (!(params.arp || params.poly)) {
        tk_stop_transport(&t->sequence);
        return false;
    }
    if (!tk_seed_valid(seed))
        return false;
    TkSeed effective = *seed;
    if (params.chord != TK_CHORD_PLAY)
        effective.count = 1;
    tk_stop_seed(&t->sequence);
    if (!tk_configure(&t->sequence, &params, &recipe) || !tk_seed(&t->sequence, &effective))
        return false;
    return true;
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

void tk_service_boundary(void)
{
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        if (!tracks[i].backend.output)
            continue;
        tk_pattern_boundary(&tracks[i].live);
        tk_pattern_boundary(&tracks[i].sequence);
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

bool tk_trig_display(uint8_t track, TkParams *out)
{
    if (!tk_trig_ready(track))
        return false;
    *out = editing->tracks[track].params;
    return true;
}

bool tk_trig_write(uint8_t track, enum TkControl control, int value)
{
    if (!tk_trig_ready(track) || !tk_set_control(&editing->tracks[track].params, control, value))
        return false;
    if (!playing || playing == editing) {
        configure_live(&tracks[track]);
        live_priority(&tracks[track], tracks[track].live.key_count != 0);
    }
    return true;
}

bool tk_trig_lock_display(uint8_t track, uint8_t step, TkParams *out, uint16_t *lock_mask)
{
    if (!tk_trig_ready(track) || step >= TK_STEPS)
        return false;
    const TkStepLocks *locks = find_step(editing, track, step);
    TkRecipe ignored;
    if (!tk_resolve_step(&editing->tracks[track], locks, out, &ignored))
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
    return store_step(editing, track, step, &next);
}

bool tk_notes_open(uint8_t track, uint8_t step, bool step_scope, TkNotesDraft *draft)
{
    if (!tk_trig_ready(track) || (step_scope && step >= TK_STEPS))
        return false;
    const TkStepLocks *locks = step_scope ? find_step(editing, track, step) : NULL;
    TkRecipe recipe;
    TkParams ignored;
    if (!tk_resolve_step(&editing->tracks[track], locks, &ignored, &recipe))
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
        !tk_trig_ready(draft->track) || !draft->recipe.count || !tk_valid_recipe(&draft->recipe))
        return false;
    if (draft->step_scope) {
        const TkStepLocks *prev = find_step(editing, draft->track, draft->step);
        TkStepLocks next = prev ? *prev : (TkStepLocks){0};
        next.user = draft->recipe;
        next.mask |= TK_RECIPE_OVERRIDE;
        if (!tk_set_lock(&next, TK_CTL_CHORD, TK_CHORD_USER) ||
            !store_step(editing, draft->track, draft->step, &next))
            return false;
    } else {
        editing->tracks[draft->track].user = draft->recipe;
        tk_trig_write(draft->track, TK_CTL_CHORD, TK_CHORD_USER);
    }
    draft->open = false;
    return true;
}
