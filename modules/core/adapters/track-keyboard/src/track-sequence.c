#include "track-sequence.h"
#include "track-memory.h"

#ifdef __m68k__
extern void *tk_trig_stock_alloc(uint32_t);
#endif

enum { QUEUE_CAPACITY = 32 };

typedef struct {
    TkSequenceHit hits[QUEUE_CAPACITY];
    volatile uint8_t write, read;
} TkSequenceQueue;

typedef struct {
    uintptr_t address;
    const TkPattern *pattern;
    TkSequencePolicy policy;
} TkPreparedSequence;

typedef struct {
    TkSequenceQueue queues[TK_SEQUENCE_SOURCES];
    TkSequenceAudio audio[TK_TRACKS];
    TkPreparedSequence prepared[2][TK_SEQUENCE_TARGETS];
} TkSequenceState;

static TkSequenceState *state;
static TkSequencePolicy policies[2];
static volatile uint8_t active_policy;
static volatile uint8_t active_prepared;
static volatile uintptr_t active_pattern;
static uintptr_t track_pattern[TK_TRACKS];
static bool was_running;

static void publish(void)
{
    __asm__ volatile("" ::: "memory");
}

bool tk_sequence_init(void)
{
    if (state)
        return true;
#ifdef __m68k__
    TkSequenceState *allocated = tk_trig_stock_alloc(sizeof *allocated);
    if (!allocated)
        return false;
    memset(allocated, 0, sizeof *allocated);
    publish();
    state = allocated;
#else
    static TkSequenceState test_state;
    state = &test_state;
#endif
    return true;
}

const TkSequenceAudio *tk_sequence_audio(uint8_t track)
{
    if (!state || track >= TK_TRACKS || !state->audio[track].sound)
        return NULL;
    return &state->audio[track];
}

static const TkPreparedSequence *prepared_pattern(uintptr_t address)
{
    if (!state || !address)
        return NULL;
    const TkPreparedSequence *targets = state->prepared[active_prepared];
    for (unsigned i = 0; i < TK_SEQUENCE_TARGETS; ++i)
        if (targets[i].address == address)
            return &targets[i];
    return NULL;
}

void tk_sequence_prepare(const TkSequenceTarget *targets, unsigned count)
{
    if (!state)
        return;
    uint8_t bank = active_prepared ^ 1u;
    TkPreparedSequence *prepared = state->prepared[bank];
    unsigned used = 0;
    for (unsigned i = 0; i < count && used < TK_SEQUENCE_TARGETS; ++i) {
        const TkSequenceTarget *target = &targets[i];
        if (!target->address || !target->pattern || target->blocked_tracks == UINT16_MAX)
            continue;
        TkPreparedSequence *entry = &prepared[used++];
        entry->address = target->address;
        entry->pattern = target->pattern;
        tk_service_sequence_policy_for(&entry->policy, target->pattern);
        for (uint8_t track = 0; track < TK_TRACKS; ++track)
            if (target->blocked_tracks & (UINT32_C(1) << track))
                memset(entry->policy.steps[track], 0, sizeof entry->policy.steps[track]);
    }
    for (unsigned i = used; i < TK_SEQUENCE_TARGETS; ++i)
        prepared[i].address = 0;
    publish();
    active_prepared = bank;
}

bool tk_sequence_wants_seed(uintptr_t pattern, uint8_t track, uint8_t step)
{
    if (!pattern || track >= TK_TRACKS || step >= TK_STEPS)
        return false;
    if (active_pattern == pattern)
        return (policies[active_policy].steps[track][step / 32] & (UINT32_C(1) << (step % 32))) !=
               0;
    const TkPreparedSequence *prepared = prepared_pattern(pattern);
    if (!prepared)
        return false;
    return (prepared->policy.steps[track][step / 32] & (UINT32_C(1) << (step % 32))) != 0;
}

bool tk_sequence_offer(uint8_t source, const TkSequenceHit *hit)
{
    if (!state || source >= TK_SEQUENCE_SOURCES || !hit || !hit->pattern ||
        (active_pattern != hit->pattern && !prepared_pattern(hit->pattern)) ||
        hit->track >= TK_TRACKS || hit->step >= TK_STEPS || hit->count > TK_NOTES)
        return false;
    for (uint8_t i = 0; i < hit->count; ++i)
        if (hit->notes[i].pitch > 127 || !hit->notes[i].velocity || hit->notes[i].velocity > 127 ||
            hit->notes[i].length > 127)
            return false;
    TkSequenceQueue *q = &state->queues[source];
    uint8_t write = q->write;
    uint8_t next = (uint8_t)((write + 1u) % QUEUE_CAPACITY);
    if (next == q->read)
        return false;
    q->hits[write] = *hit;
    publish();
    q->write = next;
    return true;
}

static void end(uint8_t track)
{
    if (track_pattern[track])
        tk_service_end_seed(track);
    track_pattern[track] = 0;
    state->audio[track].sound = 0;
}

static void accept(const TkSequenceHit *hit, const TkPattern *prepared, const uint32_t lengths[128])
{
    if (hit->parameters_only) {
        TkSequenceAudio *audio = &state->audio[hit->track];
        if (track_pattern[hit->track] != hit->pattern || !audio->sound)
            return;
        for (uint8_t i = 0; i < hit->audio.count; ++i) {
            TkSequenceLock lock = hit->audio.locks[i];
            uint8_t index = 0;
            while (index < audio->count && audio->locks[index].parameter != lock.parameter)
                ++index;
            if (index < TK_NATIVE_LOCKS_PER_HIT) {
                audio->locks[index] = lock;
                if (index == audio->count)
                    ++audio->count;
            }
        }
        return;
    }
    if (!hit->count) {
        end(hit->track);
        return;
    }
    TkSeed seed = {
        .count = hit->count,
        .length_ticks = (uint64_t)lengths[hit->notes[0].length] * 2u,
    };
    for (uint8_t i = 0; i < hit->count; ++i) {
        TkSequenceNote note = hit->notes[i];
        /* Native LEN table is expressed in half-sequencer ticks. */
        uint64_t duration = (uint64_t)lengths[note.length] * 2u;
        if (seed.length_ticks && duration > seed.length_ticks)
            duration = seed.length_ticks;
        seed.notes[i] =
            (TkSeedNote){.pitch = note.pitch, .velocity = note.velocity, .gate_ticks = duration};
    }
    state->audio[hit->track] = hit->audio;
    bool started = prepared ? tk_service_seed_for(prepared, hit->track, hit->step, &seed)
                            : tk_service_seed(hit->track, hit->step, &seed);
    if (started)
        track_pattern[hit->track] = hit->pattern;
    else
        end(hit->track);
}

TkSequenceQueueEnds tk_sequence_queue_ends(void)
{
    TkSequenceQueueEnds ends = {0};
    if (state)
        for (uint8_t source = 0; source < TK_SEQUENCE_SOURCES; ++source)
            ends.write[source] = state->queues[source].write;
    return ends;
}

void tk_sequence_drain_through(uintptr_t pattern, bool running, uint16_t muted,
                               const uint32_t lengths[128], TkSequenceQueueEnds ends)
{
    if (!state)
        return;
    bool changed = active_pattern != pattern;
    if (changed || (was_running && !running))
        active_pattern = 0;
    if (!running) {
        tk_sequence_prepare(NULL, 0);
        muted = UINT16_MAX;
    }
    was_running = running;
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (track_pattern[track] &&
            ((muted & (UINT32_C(1) << track)) || (changed && track_pattern[track] != pattern)))
            end(track);
    for (uint8_t source = 0; source < TK_SEQUENCE_SOURCES; ++source) {
        TkSequenceQueue *q = &state->queues[source];
        uint8_t end = ends.write[source];
        while (q->read != end) {
            uint8_t read = q->read;
            const TkSequenceHit *hit = &q->hits[read];
            if (running && !(muted & (UINT32_C(1) << hit->track))) {
                if (hit->pattern == pattern) {
                    accept(hit, NULL, lengths);
                } else {
                    const TkPreparedSequence *prepared = prepared_pattern(hit->pattern);
                    if (prepared)
                        accept(hit, prepared->pattern, lengths);
                }
            }
            publish();
            q->read = (uint8_t)((read + 1u) % QUEUE_CAPACITY);
        }
    }
    if (pattern) {
        /* Arm the selected pattern even before PLAY. A native start can
         * deliver its first due event before the next service poll. */
        uint8_t next_policy = active_policy ^ 1u;
        tk_service_sequence_policy(&policies[next_policy]);
        publish();
        active_policy = next_policy;
        active_pattern = pattern;
    }
}

void tk_sequence_drain(uintptr_t pattern, bool running, uint16_t muted, const uint32_t lengths[128])
{
    tk_sequence_drain_through(pattern, running, muted, lengths, tk_sequence_queue_ends());
}

void tk_sequence_reset(void)
{
    tk_sequence_drain(0, false, UINT16_MAX, NULL);
}
