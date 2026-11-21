#ifndef DK_INSTRUMENT_SHARED_NOTES_H
#define DK_INSTRUMENT_SHARED_NOTES_H

#include "../../../midi-events/src/cf/ownership.h"

/* Reuse the carrier's four foreground voices. Reserve one queue entry for
 * every accepted note's eventual release, even while an ACK is pending. */
enum { ISN_VOICES = 4, ISN_SHARED_VOICES = 5, ISN_QUEUE = 20 };
#define ISN_ARP_TOKEN UINT32_C(0x40000000)
typedef struct {
    uint32_t token, wire;
    uint8_t pitch, velocity;
} IsnNote;
typedef struct {
    uint32_t epoch, generation, track, last_token;
    uint8_t head, count, active_count;
    IsnNote active[ISN_SHARED_VOICES];
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

static inline int isn_submit_voice(IsnQueue *q, uint32_t token, uint32_t pitch, uint32_t velocity,
                                   int on, int lane)
{
    if (!token || pitch > 127 || velocity > 127 || (on && !velocity))
        return 0;
    uint8_t pos;
    for (pos = 0; pos < q->active_count && q->active[pos].token != token; ++pos) {
    }
    uint32_t wire;
    if (on) {
        unsigned limit = lane < 0 ? ISN_VOICES : ISN_SHARED_VOICES;
        unsigned queue = lane < 0 ? 16u : ISN_QUEUE;
        if (pos < q->active_count || q->active_count == limit ||
            (unsigned)q->count + q->active_count + 2 > queue)
            return 0;
        wire = token;
        if (lane < 0) {
            if (token <= q->last_token)
                return 0;
            q->last_token = token;
        } else {
            if (q->last_token == ISN_ARP_TOKEN - 1)
                return 0;
            wire = ++q->last_token | (lane ? ISN_ARP_TOKEN : 0u);
        }
        q->active[q->active_count++] =
            (IsnNote){token, wire, (uint8_t)pitch, (uint8_t)velocity};
    } else {
        if (pos == q->active_count || q->active[pos].pitch != pitch)
            return 0;
        wire = q->active[pos].wire;
        q->active[pos] = q->active[--q->active_count];
    }
    q->pending[(q->head + q->count) % ISN_QUEUE] =
        (IsnNote){token, wire, (uint8_t)pitch, on ? (uint8_t)velocity : 0};
    ++q->count;
    return 1;
}

static inline int isn_submit(IsnQueue *q, uint32_t token, uint32_t pitch, uint32_t velocity, int on)
{
    return isn_submit_voice(q, token, pitch, velocity, on, -1);
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
        event->token = note.wire;
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
