#include "ownership.h"

void mi_init(MiLedger *m)
{
    uint32_t i;
    /* Byte loop avoids depending on libc on the CF target. */
    uint8_t *p = (uint8_t *)m;
    for (i = 0; i < sizeof(*m); ++i) p[i] = 0;
    for (i = 0; i < MI_ROUTES; ++i) m->routes[i].track = MI_NONE;
    for (i = 0; i < MI_TRACKS; ++i) m->generations[i] = 1;
    m->epoch = 1;
    m->next_token = m->next_sequence = 1;
}

void mi_reset_track(MiLedger *m, uint32_t track)
{
    uint32_t i;
    if (track >= MI_TRACKS) return;
    if (!(m->reset_mask & (1u << track))) {
        ++m->generations[track];
        /* Generation zero is permanently inadmissible until stream recovery. */
    }
    m->reset_mask |= 1u << track;
    for (i = 0; i < MI_KEYS; ++i)
        if (m->keys[i].used && m->keys[i].track == track)
            m->keys[i].tombstone = 1;
    for (i = 0; i < MI_ROUTES; ++i)
        if (m->routes[i].track == track) m->routes[i].enabled = 0;
    /* The transport must service reset_mask ahead of the ring and discard
     * queued events for the old generation; this never frees physical keys. */
}

int mi_reset_ack(MiLedger *m, uint32_t epoch, uint32_t track, uint32_t generation)
{
    if (epoch != m->epoch || track >= MI_TRACKS || !generation || generation != m->generations[track] ||
        !(m->reset_mask & (1u << track))) return 0;
    m->reset_mask &= ~(1u << track);
    return 1;
}

static void overflow(MiLedger *m)
{
    uint32_t i;
    if (m->overruns != UINT32_MAX) ++m->overruns;
    /* Losing one ring event breaks ownership, so reset all admitted instances.
     * Quarantine all configured routes: untracked presses cannot be paired. */
    for (i = 0; i < MI_ROUTES; ++i) {
        if (m->routes[i].track < MI_TRACKS) {
            mi_reset_track(m, m->routes[i].track);
            m->routes[i].quarantined = 1;
        }
    }
    m->head = m->count = 0;
}

void mi_recover(MiLedger *m)
{
    uint32_t i, resets = m->reset_mask, overruns = m->overruns, epoch = m->epoch;
    if (epoch == UINT32_MAX) { overflow(m); return; }
    /* This operation is explicit and requires keys up: it is the only path
     * allowed to erase unpaired tombstones after capacity exhaustion. */
    for (i = 0; i < MI_ROUTES; ++i)
        if (m->routes[i].track < MI_TRACKS) resets |= 1u << m->routes[i].track;
    mi_init(m);
    m->epoch = epoch + 1;
    m->reset_mask = resets;
    m->overruns = overruns;
}

int mi_route(MiLedger *m, uint32_t port, uint32_t channel, uint32_t track,
             uint32_t generation, int sequencer)
{
    MiRoute *r;
    if (port >= 2 || channel >= 16 || (track >= MI_TRACKS && track != MI_NONE) ||
        (track != MI_NONE && (generation == 0 || generation != m->generations[track]))) return -1;
    r = &m->routes[port * 16 + channel];
    if (r->quarantined) return -2;
    /* Reset and ACK are an explicit prerequisite, not a UI timing assumption. */
    if (r->enabled || (track < MI_TRACKS && (m->reset_mask & (1u << track)))) return -2;
    r->track = (uint8_t)track;
    r->generation = generation;
    r->sequencer = sequencer != 0;
    r->enabled = track != MI_NONE;
    return 0;
}

static int emit(MiLedger *m, uint32_t token, uint32_t gen, uint8_t track,
                uint8_t kind, uint8_t port, uint8_t channel, uint8_t note, uint8_t value)
{
    MiEvent *e;
    if (m->count == MI_EVENTS || m->next_sequence == 0) {
        overflow(m);
        return 0;
    }
    e = &m->events[(m->head + m->count) % MI_EVENTS];
    e->epoch = m->epoch;
    e->sequence = m->next_sequence++;
    e->token = token;
    e->generation = gen;
    e->track = track;
    e->kind = kind;
    e->port = port;
    e->channel = channel;
    e->note = note;
    e->value = value;
    ++m->count; /* publish last, while caller holds its critical section */
    return 1;
}

static MiKey *oldest(MiLedger *m, uint8_t port, uint8_t channel, uint8_t note)
{
    uint32_t i;
    MiKey *found = 0;
    for (i = 0; i < MI_KEYS; ++i) {
        MiKey *k = &m->keys[i];
        if (k->used && k->port == port && k->channel == channel && k->note == note &&
            (!found || k->token < found->token)) found = k;
    }
    return found; /* tokens never wrap within an ownership epoch */
}

int mi_message(MiLedger *m, uint32_t port, const uint8_t *msg, uint32_t len)
{
    uint32_t i;
    uint8_t kind, channel, note, value;
    MiRoute *r;
    MiKey *k;
    if (!msg || port >= 2 || len != 3 || msg[0] < 0x80 || msg[0] >= 0xf0 ||
        msg[1] >= 0x80 || msg[2] >= 0x80) return 0;
    kind = msg[0] >> 4;
    channel = msg[0] & 15;
    note = msg[1]; value = msg[2];
    r = &m->routes[port * 16 + channel];
    if (kind == 9 && !value) kind = 8;
    if (kind != 8 && kind != 9 && kind != 11 && kind != 14) return 0;
    if (kind == 11 && note != 64 && note != 120 && note != 121 && note != 123) return 0;
    if (kind == 8) {
        k = oldest(m, (uint8_t)port, channel, note);
        if (k) {
            if (!k->tombstone)
                emit(m, k->token, k->generation, k->track, MI_NOTE_OFF,
                     (uint8_t)port, channel, note, value);
            k->used = 0;
            return 1;
        }
        return r->track != MI_NONE;
    }
    if (r->quarantined) return 1;
    if (r->track == MI_NONE) return 0;
    if (kind == 9) {
        if (!m->next_token) { overflow(m); return 1; }
        for (i = 0; i < MI_KEYS; ++i) if (!m->keys[i].used) break;
        if (i == MI_KEYS) { overflow(m); return 1; }
        k = &m->keys[i];
        k->token = m->next_token++;
        k->generation = r->generation;
        k->track = r->track;
        k->port = (uint8_t)port; k->channel = channel; k->note = note;
        k->tombstone = !r->enabled || r->sequencer; k->used = 1;
        /* Even ignored presses need release pairing across a source/reset
         * transition. They must never release a later accepted same-pitch key. */
        if (!k->tombstone)
            emit(m, k->token, k->generation, k->track, MI_NOTE_ON,
                 (uint8_t)port, channel, note, value);
    } else {
        if (!r->enabled || (r->sequencer && !(kind == 11 && note == 120))) return 1;
        emit(m, 0, r->generation, r->track, kind == 11 ? MI_CC : MI_BEND,
             (uint8_t)port, channel, note, value);
        /* CC120/123 releases logical keys, but retain their physical pairing:
         * old C-off after panic and a fresh C-on must not kill the new C. */
        if (kind == 11 && (note == 120 || note == 123))
            for (i = 0; i < MI_KEYS; ++i)
                if (m->keys[i].used && m->keys[i].port == port &&
                    m->keys[i].channel == channel) m->keys[i].tombstone = 1;
    }
    return 1;
}

int mi_peek(MiLedger *m, MiEvent *out)
{
    if (m->reset_mask || !out) return 0; /* reset has reserved priority */
    while (m->count && m->events[m->head].generation !=
           m->generations[m->events[m->head].track]) {
        m->head = (m->head + 1) % MI_EVENTS;
        --m->count;
    }
    if (!m->count) return 0;
    /* Explicit fields keep this freestanding adapter independent of memcpy. */
    {
        const MiEvent *e = &m->events[m->head];
        out->epoch = e->epoch; out->sequence = e->sequence;
        out->token = e->token; out->generation = e->generation;
        out->kind = e->kind; out->port = e->port; out->channel = e->channel;
        out->track = e->track; out->note = e->note; out->value = e->value;
    }
    return 1;
}

int mi_ack(MiLedger *m, uint32_t epoch, uint32_t sequence)
{
    if (epoch != m->epoch || m->reset_mask || !m->count ||
        m->events[m->head].sequence != sequence) return 0;
    m->head = (m->head + 1) % MI_EVENTS;
    --m->count;
    return 1;
}
