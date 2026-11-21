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
    TkSequenceQueue queues[TK_SEQUENCE_SOURCES];
    TkSequenceAudio audio[TK_TRACKS];
} TkSequenceState;

static TkSequenceState *state;
static TkSequencePolicy policies[2];
static volatile uint8_t active_policy;
static volatile uintptr_t active_pattern;
static uint16_t sounding;
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

bool tk_sequence_wants_seed(uintptr_t pattern, uint8_t track, uint8_t step)
{
    if (!pattern || active_pattern != pattern || track >= TK_TRACKS || step >= TK_STEPS)
        return false;
    uint8_t bank = active_policy;
    return (policies[bank].steps[track][step / 32] & (UINT32_C(1) << (step % 32))) != 0;
}

bool tk_sequence_offer(uint8_t source, const TkSequenceHit *hit)
{
    if (!state || source >= TK_SEQUENCE_SOURCES || !hit || !hit->pattern ||
        active_pattern != hit->pattern || hit->track >= TK_TRACKS || hit->step >= TK_STEPS ||
        hit->count > TK_NOTES)
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

static void accept(const TkSequenceHit *hit, const uint32_t lengths[128])
{
    uint16_t mask = (uint16_t)(UINT32_C(1) << hit->track);
    if (!hit->count) {
        if (sounding & mask)
            tk_service_end_seed(hit->track);
        sounding &= (uint16_t)~mask;
        state->audio[hit->track].sound = 0;
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
    if (tk_service_seed(hit->track, hit->step, &seed))
        sounding |= mask;
    else
        state->audio[hit->track].sound = 0;
}

void tk_sequence_drain(uintptr_t pattern, bool running, uint16_t muted, const uint32_t lengths[128])
{
    if (!state)
        return;
    bool changed = active_pattern != pattern;
    if (changed || (was_running && !running))
        active_pattern = 0;
    if (changed || !running)
        muted = UINT16_MAX;
    was_running = running;
    uint16_t departing = muted & sounding;
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (departing & (UINT32_C(1) << track)) {
            tk_service_end_seed(track);
            state->audio[track].sound = 0;
        }
    sounding &= (uint16_t)~departing;
    for (uint8_t source = 0; source < TK_SEQUENCE_SOURCES; ++source) {
        TkSequenceQueue *q = &state->queues[source];
        uint8_t end = q->write;
        while (q->read != end) {
            uint8_t read = q->read;
            const TkSequenceHit *hit = &q->hits[read];
            if (running && hit->pattern == pattern &&
                !(muted & (UINT32_C(1) << hit->track)))
                accept(hit, lengths);
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

void tk_sequence_reset(void)
{
    tk_sequence_drain(0, false, UINT16_MAX, NULL);
}
