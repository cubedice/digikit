#ifndef DK_INSTRUMENT_SHARED_NOTES_H
#define DK_INSTRUMENT_SHARED_NOTES_H

#include "../../../midi-events/src/cf/ownership.h"

/* Reuse the carrier's four foreground voices. Reserve one queue entry for
 * every accepted note's eventual release, even while an ACK is pending. */
enum { ISN_VOICES = 4, ISN_QUEUE = 16 };
typedef struct {
    uint32_t token;
    uint8_t pitch, velocity;
} IsnNote;
typedef struct {
    uint32_t epoch, generation, track, last_token;
    uint8_t head, count, active_count;
    IsnNote active[ISN_VOICES];
    IsnNote pending[ISN_QUEUE];
} IsnQueue;

static inline void isn_sync(IsnQueue *q, uint32_t track, uint32_t epoch, uint32_t generation)
{
    if (q->track != track || q->epoch != epoch || q->generation != generation) {
        q->track = track;
        q->epoch = epoch;
        q->generation = generation;
        q->last_token = 0;
        q->head = q->count = q->active_count = 0;
    }
}

static inline int isn_submit(IsnQueue *q, uint32_t token, uint32_t pitch, uint32_t velocity, int on)
{
    if (!token || pitch > 127 || velocity > 127 || (on && !velocity))
        return 0;
    uint8_t pos;
    for (pos = 0; pos < q->active_count && q->active[pos].token != token; ++pos) {
    }
    if (on) {
        if (token <= q->last_token || pos == ISN_VOICES ||
            q->count + q->active_count + 2 > ISN_QUEUE)
            return 0;
        q->last_token = token;
        q->active[q->active_count++] = (IsnNote){token, (uint8_t)pitch, (uint8_t)velocity};
    } else {
        if (pos == q->active_count || q->active[pos].pitch != pitch)
            return 0;
        q->active[pos] = q->active[--q->active_count];
    }
    q->pending[(q->head + q->count) % ISN_QUEUE] =
        (IsnNote){token, (uint8_t)pitch, on ? (uint8_t)velocity : 0};
    ++q->count;
    return 1;
}

static inline uint32_t isn_take(IsnQueue *q, MiEvent output[8])
{
    uint32_t n = 0;
    for (; n < 8 && q->count; ++n) {
        IsnNote note = q->pending[q->head];
        MiEvent *event = &output[n];
        /* A panel-shaped packet fits the existing wire validators. Shared
         * note ownership is entirely CF-side. The DSP sees one raw voice
         * onset/release and checks generation just as for a local key. */
        event->epoch = q->epoch;
        event->sequence = 0;
        event->token = note.token;
        event->generation = q->generation;
        event->kind = note.velocity ? MI_NOTE_ON : MI_NOTE_OFF;
        event->port = MI_PANEL;
        event->channel = 0;
        event->track = (uint8_t)q->track;
        event->note = note.pitch;
        event->value = note.velocity;
        event->lock_valid = event->lock_count = event->source_valid = 0;
        q->head = (uint8_t)((q->head + 1) % ISN_QUEUE);
        --q->count;
    }
    return n;
}
#endif
