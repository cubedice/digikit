#include "track-capture.h"

#include "track-memory.h"

extern uint32_t dkrt_blocks(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

enum {
    NOTE_TABLE = 0x6a50,
    CAPTURE_HISTORY = 2 * TK_NOTES,
    COMMIT_CAPACITY = 64,
    MATCH_BLOCKS = 1500,
    UNKNOWN_PITCH = 128,
    UNKNOWN_STEP = 255,
};

typedef struct {
    uint32_t id, started, released_at;
    TkRecordedShape shape;
    uint16_t group, bpm;
    uint8_t pitch, velocity, channel, source, step;
    bool released, step_record;
} Input;

typedef struct {
    Input inputs[CAPTURE_HISTORY];
    uint16_t step_group[TK_STEPS], group;
    uint8_t count;
    bool step_mode;
} CaptureTrack;

typedef struct {
    uint32_t pattern, arrived;
    uint8_t track, step, pitch, velocity;
} Commit;

#ifdef __m68k__
extern void *tk_trig_stock_alloc(uint32_t);
static CaptureTrack *tracks;
#else
static CaptureTrack host_tracks[TK_TRACKS];
static CaptureTrack *tracks = host_tracks;
#endif
static Commit commits[COMMIT_CAPACITY];
static volatile uint8_t read_commit, write_commit;
static volatile uint32_t pattern;
static uint16_t tempo;

volatile uint32_t tk_capture_committed, tk_capture_dropped;
volatile uint32_t tk_capture_last_count;

static uint8_t group_placement(const CaptureTrack *t, uint8_t index)
{
    const Input *note = &t->inputs[index];
    /* A stock audio trig can report just one pitch for a held PLAY chord.
     * Use its placement for the other known pitches until stock supplies
     * their own. Keep distinct native placements when the gesture spans steps. */
    for (uint8_t i = index; i > 0; --i) {
        const Input *earlier = &t->inputs[i - 1u];
        if (earlier->group == note->group && earlier->step < TK_STEPS)
            return earlier->step;
    }
    for (uint8_t i = index + 1u; i < t->count; ++i) {
        const Input *later = &t->inputs[i];
        if (later->group == note->group && later->step < TK_STEPS)
            return later->step;
    }
    return UNKNOWN_STEP;
}

static uint8_t placement(const CaptureTrack *t, uint8_t index)
{
    const Input *note = &t->inputs[index];
    if (note->step < TK_STEPS || note->pitch == UNKNOWN_PITCH || note->shape.chord != TK_CHORD_PLAY)
        return note->step;
    return group_placement(t, index);
}

static bool rethink(uint8_t track, uint8_t step, uint16_t group)
{
    CaptureTrack *t = &tracks[track];
    if (step >= TK_STEPS || t->step_group[step] != group)
        return true;
    TkSeed seed = {0};
    TkRecordedShape shape;
    uint32_t first = 0;
    for (uint8_t i = 0; i < t->count; ++i) {
        const Input *note = &t->inputs[i];
        if (note->group != group || placement(t, i) != step)
            continue;
        if (!seed.count) {
            first = note->started;
            shape = note->shape;
        }
        if (seed.count == TK_NOTES) {
            ++tk_capture_dropped;
            return false;
        }
        /* Step entry records pitches on the trig, irrespective of the time
         * between key presses. Live recording can still capture a roll. */
        uint32_t delay = note->step_record ? 0 : note->started - first;
        uint64_t gate = note->released && note->bpm
                            ? (uint64_t)(note->released_at - first) * 2u * note->bpm
                            : 0;
        seed.notes[seed.count++] = (TkSeedNote){
            .pitch = note->pitch,
            .velocity = note->velocity,
            .delay_us = (uint32_t)((uint64_t)delay * 2000u / 3u),
            .gate_ticks = note->released && !gate ? 1 : gate,
        };
    }
    if (!seed.count)
        return true;
    if (!tk_service_store_edit_seed(track, step, &seed, &shape)) {
        ++tk_capture_dropped;
        return false;
    }
    tk_capture_last_count = seed.count;
    ++tk_capture_committed;
    return true;
}

static bool release(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint32_t now)
{
    CaptureTrack *t = &tracks[track];
    for (uint8_t i = 0; i < t->count; ++i) {
        Input *note = &t->inputs[i];
        if (note->released || note->source != source || note->channel != channel || note->id != id)
            continue;
        note->released = true;
        note->released_at = now;
        rethink(track, placement(t, i), note->group);
        return true;
    }
    return false;
}

static void reclaim(CaptureTrack *t, uint32_t now)
{
    uint8_t kept = 0;
    for (uint8_t i = 0; i < t->count; ++i) {
        Input note = t->inputs[i];
        if (note.group != t->group && note.released) {
            bool pending = false;
            for (uint8_t j = 0; j < t->count; ++j) {
                const Input *other = &t->inputs[j];
                if (other->group == note.group &&
                    (!other->released ||
                     (other->step >= TK_STEPS && now - other->released_at <= MATCH_BLOCKS))) {
                    pending = true;
                    break;
                }
            }
            if (!pending)
                continue;
        }
        t->inputs[kept++] = note;
    }
    t->count = kept;
}

bool tk_capture_input_at(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                         uint8_t velocity, bool step_record, uint32_t now)
{
    if (!pattern || track >= TK_TRACKS || source >= TK_SEQUENCER || channel > 15)
        return false;
    if (!velocity)
        return release(track, source, channel, id, now);
    TkParams params;
    TkRecordedShape shape;
    bool unset_pitch = step_record && source == TK_PANEL && pitch == UNKNOWN_PITCH;
    if ((!unset_pitch && pitch > 127) || velocity > 127 || !tk_trig_display(track, &params) ||
        !(params.arp || params.poly) || !tk_service_edit_shape(track, &shape))
        return false;
    CaptureTrack *t = &tracks[track];
    uint8_t held = 0, group_count = 0;
    for (uint8_t i = 0; i < t->count; ++i) {
        const Input *note = &t->inputs[i];
        if (note->group != t->group)
            continue;
        ++group_count;
        if (!note->released) {
            if (note->source == source && note->channel == channel && note->id == id)
                return false;
            ++held;
        }
    }
    if (!held || t->step_mode != step_record) {
        if (!++t->group)
            ++t->group;
        t->step_mode = step_record;
        group_count = 0;
    }
    reclaim(t, now);
    if (t->count == CAPTURE_HISTORY || group_count == TK_NOTES) {
        ++tk_capture_dropped;
        return false;
    }
    t->inputs[t->count++] = (Input){
        .id = id,
        .started = now,
        .group = t->group,
        .pitch = pitch,
        .velocity = velocity,
        .channel = channel,
        .source = source,
        .step = UNKNOWN_STEP,
        .shape = shape,
        .bpm = tempo,
        .step_record = step_record,
    };
    if (group_count)
        rethink(track, placement(t, t->count - 1u), t->group);
    return true;
}

void tk_capture_shape_changed(uint8_t track)
{
    TkRecordedShape shape;
    if (!pattern || track >= TK_TRACKS || !tk_service_edit_shape(track, &shape))
        return;
    CaptureTrack *t = &tracks[track];
    bool held = false;
    for (uint8_t i = 0; i < t->count; ++i)
        held |= t->inputs[i].group == t->group && !t->inputs[i].released;
    if (!held)
        return;
    uint32_t changed[TK_STEPS / 32] = {0};
    for (uint8_t i = 0; i < t->count; ++i) {
        Input *note = &t->inputs[i];
        if (note->group != t->group)
            continue;
        note->shape = shape;
        if (note->step < TK_STEPS)
            changed[note->step / 32u] |= UINT32_C(1) << (note->step % 32u);
    }
    for (uint8_t step = 0; step < TK_STEPS; ++step)
        if (changed[step / 32u] & (UINT32_C(1) << (step % 32u)))
            rethink(track, step, t->group);
}

bool tk_capture_input(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                      uint8_t velocity, bool step_record)
{
    return tk_capture_input_at(track, source, channel, id, pitch, velocity, step_record,
                               dkrt_blocks());
}

void tk_capture_panel_press(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity,
                            uint32_t step_record)
{
    if (key < 16 && track < TK_TRACKS && pitch <= UNKNOWN_PITCH && velocity && velocity <= 127)
        tk_capture_input((uint8_t)track, TK_PANEL, 0, key, (uint8_t)pitch, (uint8_t)velocity,
                         step_record != 0);
}

void tk_capture_panel_release(uint32_t key)
{
    if (!pattern || key >= 16)
        return;
    uint32_t now = dkrt_blocks();
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        release(track, TK_PANEL, 0, key, now);
}

void tk_capture_note_commit(uint32_t note_table, uint32_t track, uint32_t step, uint32_t pitch,
                            uint32_t velocity)
{
    if (note_table != pattern + NOTE_TABLE || !pattern || track >= TK_TRACKS || step >= TK_STEPS ||
        pitch > 127 || velocity > 255)
        return;
    uint32_t sr = dkrt_irq_mask();
    uint8_t next = (uint8_t)((write_commit + 1u) % COMMIT_CAPACITY);
    if (next != read_commit) {
        commits[write_commit] = (Commit){
            .pattern = pattern,
            .arrived = dkrt_blocks(),
            .track = (uint8_t)track,
            .step = (uint8_t)step,
            .pitch = (uint8_t)pitch,
            .velocity = velocity && velocity <= 127 ? (uint8_t)velocity : 100,
        };
        write_commit = next;
    } else {
        ++tk_capture_dropped;
    }
    dkrt_irq_restore(sr);
}

static void consume(const Commit *commit)
{
    CaptureTrack *t = &tracks[commit->track];
    for (uint8_t i = 0; i < t->count; ++i) {
        Input *note = &t->inputs[i];
        if (note->step < TK_STEPS ||
            (note->pitch != commit->pitch && note->pitch != UNKNOWN_PITCH) ||
            (int32_t)(commit->arrived - note->started) < -2 ||
            (note->released && (int32_t)(commit->arrived - note->released_at) > MATCH_BLOCKS))
            continue;
        uint8_t previous_step =
            note->shape.chord == TK_CHORD_PLAY ? group_placement(t, i) : UNKNOWN_STEP;
        if (note->pitch == UNKNOWN_PITCH) {
            note->pitch = commit->pitch;
            note->velocity = commit->velocity;
        }
        uint16_t previous_group = t->step_group[commit->step];
        note->step = commit->step;
        t->step_group[commit->step] = note->group;
        if (!rethink(commit->track, commit->step, note->group)) {
            note->step = UNKNOWN_STEP;
            t->step_group[commit->step] = previous_group;
        } else if (previous_step != commit->step) {
            rethink(commit->track, previous_step, note->group);
        }
        return;
    }
}

void tk_capture_poll(uint32_t edit_pattern, uint16_t bpm)
{
    if (tk_valid_tempo(bpm))
        tempo = bpm;
    if (pattern != edit_pattern) {
#ifdef __m68k__
        if (!tracks)
            tracks = tk_trig_stock_alloc(sizeof *tracks * TK_TRACKS);
#endif
        if (!tracks)
            return;
        uint32_t sr = dkrt_irq_mask();
        pattern = 0;
        read_commit = write_commit;
        dkrt_irq_restore(sr);
        memset(tracks, 0, sizeof *tracks * TK_TRACKS);
        pattern = edit_pattern;
    }
    if (!pattern)
        return;
    for (;;) {
        uint32_t sr = dkrt_irq_mask();
        if (read_commit == write_commit) {
            dkrt_irq_restore(sr);
            break;
        }
        Commit commit = commits[read_commit];
        read_commit = (uint8_t)((read_commit + 1u) % COMMIT_CAPACITY);
        dkrt_irq_restore(sr);
        if (commit.pattern == pattern)
            consume(&commit);
    }
}
