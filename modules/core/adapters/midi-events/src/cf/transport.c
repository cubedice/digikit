#include "transport.h"

void mt_init(MiTransport *t)
{
    uint32_t i;
    uint8_t *p = (uint8_t *)t;
    for (i = 0; i < sizeof(*t); ++i) p[i] = 0;
    t->next_message = 1;
}

uint32_t mt_check(const uint32_t w[MT_WORDS])
{
    uint32_t i, a = 0, b = 0;
    for (i = 0; i < MT_WORDS; ++i) {
        if (i == 7) continue;
        a += w[i]; b += a;
    }
    return a ^ b;
}

int mt_config_begin(MiTransport *t, const MiLedger *m, uint32_t track,
                    uint32_t revision, uint32_t schema,
                    const uint32_t *words, uint32_t count)
{
    uint32_t i, a = 0, b = 0;
    if (track >= MI_TRACKS || !revision || !schema || !words ||
        !count || count > MT_CONFIG_WORDS || !m->generations[track]) return -1;
    if (t->epoch != m->epoch) { mt_init(t); t->epoch = m->epoch; }
    if (t->config_active || t->pending || t->faults || m->reset_mask) return -2;
    if (revision <= t->applied[track]) return -1;
    for (i = 0; i < count; ++i) {
        t->config[i] = words[i]; a += words[i]; b += a;
    }
    t->config_track = track; t->config_generation = m->generations[track];
    t->config_revision = revision; t->config_schema = schema;
    t->config_words = count; t->config_chunk = 0; t->config_check = a ^ b;
    t->config_active = 1;
    return 0;
}

static void fault(MiTransport *t, MiLedger *m)
{
    uint32_t i;
    if (t->faults != UINT32_MAX) ++t->faults;
    for (i = 0; i < MI_TRACKS; ++i) {
        mi_reset_track(m, i);
        t->applied[i] = 0;
    }
    t->pending = 0;
    t->config_active = 0;
}

const uint32_t *mt_prepare(MiTransport *t, MiLedger *m,
                           const uint32_t revisions[MI_TRACKS], uint32_t block)
{
    uint32_t i, kind, track, count = 0;
    MiEvent first;
    if (t->epoch != m->epoch) {
        mt_init(t); t->epoch = m->epoch;
    }
    if (m->reset_mask) {
        /* A commit may have applied while its ACK was lost. If another track
         * requests reset, also reset this transaction's owner rather than
         * resume notes against an uncertain DSP revision. */
        if (t->config_active && t->pending == MT_CONFIG_COMMIT)
            mi_reset_track(m, t->config_track);
        t->config_active = 0;
        for (i = 0; i < MI_TRACKS; ++i)
            if (m->reset_mask & (1u << i)) t->applied[i] = 0;
    }
    /* A reset supersedes an unacknowledged note batch. The next reset may jump
     * the ordered sequence; an old note ACK can never clear this request. */
    if (t->pending && t->pending != MT_RESET && m->reset_mask) t->pending = 0;
    if (t->pending) {
        if (block - t->sent_block >= MT_STALL_BLOCKS && !t->faults) fault(t, m);
        else return t->words;
    }
    if (!t->next_message) {
        if (!t->faults) fault(t, m);
        return 0; /* explicit stream recovery required; never reuse an identity */
    }
    if (m->reset_mask) {
        for (track = 0; !(m->reset_mask & (1u << track)); ++track) {}
        kind = MT_RESET;
    } else if (t->config_active && !t->faults) {
        track = t->config_track;
        if (t->config_generation != m->generations[track]) {
            t->config_active = 0;
            return 0;
        }
        kind = t->config_chunk * MT_CHUNK_WORDS < t->config_words ?
               MT_CONFIG_CHUNK : MT_CONFIG_COMMIT;
    } else {
        if (t->faults || !mi_peek(m, &first) || !revisions[first.track]) return 0;
        track = first.track; kind = MT_EVENTS;
    }
    for (i = 0; i < MT_WORDS; ++i) t->words[i] = 0;
    t->words[0] = MT_MAGIC;
    t->words[2] = m->epoch;
    t->words[3] = t->next_message++;
    t->words[6] = block;
    t->track = track; t->generation = m->generations[track];
    if (kind == MT_RESET) {
        t->words[8] = track;
        t->words[9] = t->generation;
    } else if (kind == MT_CONFIG_CHUNK || kind == MT_CONFIG_COMMIT) {
        t->words[5] = t->config_revision;
        t->words[8] = track;
        t->words[9] = t->config_generation;
        t->words[10] = t->config_revision;
        t->words[11] = t->config_chunk;
        t->words[12] = t->config_words * 4;
        t->words[13] = t->config_schema;
        t->words[14] = t->config_check;
        if (kind == MT_CONFIG_CHUNK) {
            uint32_t offset = t->config_chunk * MT_CHUNK_WORDS;
            uint32_t n = t->config_words - offset;
            if (n > MT_CHUNK_WORDS) n = MT_CHUNK_WORDS;
            t->words[15] = n;
            for (i = 0; i < n; ++i) t->words[16+i] = t->config[offset+i];
        }
    } else {
        t->words[4] = first.sequence;
        t->words[5] = revisions[track];
        for (i = 0; i < m->count && i < 8; ++i) {
            const MiEvent *e = &m->events[(m->head + i) % MI_EVENTS];
            if (e->epoch != m->epoch || e->track != track ||
                e->generation != t->generation || e->sequence != first.sequence + i) break;
            t->words[8 + 3*i] = e->token;
            t->words[9 + 3*i] = e->generation;
            t->words[10 + 3*i] = e->kind | ((uint32_t)e->port << 4) |
                ((uint32_t)e->channel << 6) | ((uint32_t)e->track << 10) |
                ((uint32_t)e->note << 14) | ((uint32_t)e->value << 21);
            ++count;
        }
    }
    t->words[1] = kind | (count << 8);
    t->words[7] = mt_check(t->words);
    t->pending = kind; t->count = count; t->sent_block = block;
    return t->words;
}

int mt_ack(MiTransport *t, MiLedger *m, uint32_t epoch, uint32_t message)
{
    uint32_t i;
    if (!t->pending || epoch != m->epoch || epoch != t->epoch ||
        message != t->words[3]) return 0;
    if (t->pending == MT_RESET) {
        if (!mi_reset_ack(m, epoch, t->track, t->generation)) return 0;
        t->applied[t->track] = 0;
    } else if (t->pending == MT_CONFIG_CHUNK || t->pending == MT_CONFIG_COMMIT) {
        if (m->reset_mask || !t->config_active ||
            m->generations[t->track] != t->config_generation) return 0;
        if (t->pending == MT_CONFIG_CHUNK) ++t->config_chunk;
        else {
            t->applied[t->track] = t->config_revision;
            t->config_active = 0;
        }
    } else {
        if (m->reset_mask || m->count < t->count ||
            m->generations[t->track] != t->generation) return 0;
        /* Validate the entire retained batch before consuming any event. */
        for (i = 0; i < t->count; ++i) {
            const MiEvent *e = &m->events[(m->head + i) % MI_EVENTS];
            if (e->epoch != epoch || e->sequence != t->words[4] + i ||
                e->track != t->track || e->generation != t->generation) return 0;
        }
        for (i = 0; i < t->count; ++i) mi_ack(m, epoch, t->words[4] + i);
    }
    t->pending = 0;
    return 1;
}

void mt_reply_read(const volatile uint16_t *record, uint32_t out[MT_REPLY_WORDS])
{
    uint32_t i;
    for (i = 0; i < MT_REPLY_WORDS; ++i) {
        uint32_t half = (0x1cu + 0x54u * (21u + i/2u) + 0x44u + (i & 1u)*4u)/2u;
        out[i] = record ? (uint32_t)record[half] | ((uint32_t)record[half+1] << 16) : 0;
    }
}

int mt_reply_valid(const uint32_t reply[MT_REPLY_WORDS])
{
    uint32_t i, a = 0, b = 0;
    if (!reply || reply[0] != MT_REPLY_MAGIC) return 0;
    for (i = 0; i < MT_REPLY_WORDS-1; ++i) {
        a += reply[i]; b += a;
        if (i >= 12 && reply[i]) return 0;
    }
    if (reply[15] != (a ^ b)) return 0;
    if (reply[4] == 0x49525231u) return 1; /* registry validates its own identity/status */
    for (i = 4; i < 12; ++i) if (reply[i]) return 0;
    return 1;
}

int mt_reply_tick(MiTransport *t, MiLedger *m,
                  const uint32_t reply[MT_REPLY_WORDS], uint32_t block)
{
    uint32_t valid = mt_reply_valid(reply);
    int result = 0;
    if (t->epoch != m->epoch) { mt_init(t); t->epoch = m->epoch; }
    if (reply) {
        if (reply[1] != m->epoch) valid = 0;
        if (!valid) result = -1;
    }
    if (valid) {
        uint32_t delta = reply[3] - t->heartbeat;
        if (t->heartbeat_seen && delta >= 0x80000000u) {
            /* Reboot/backward heartbeat: ownership is no longer trustworthy. */
            if (!t->faults) fault(t, m);
            return -1;
        }
        if (!t->heartbeat_seen || delta) {
            t->heartbeat_seen = 1;
            t->heartbeat = reply[3];
            t->heartbeat_block = block;
            result = mt_ack(t, m, reply[1], reply[2]) ? 2 : 1;
        }
    }
    if (t->heartbeat_seen && block - t->heartbeat_block >= MT_STALL_BLOCKS && !t->faults)
        fault(t, m);
    return result;
}


void mt_reply_guard_tick(MtReplyGuard *g, const uint32_t *reply, uint32_t block)
{
    uint32_t delta;
    if (g->invalid) return;
    if (mt_reply_valid(reply)) {
        delta = reply[3] - g->heartbeat;
        if (g->seen && delta >= 0x80000000u) { g->invalid = 1; return; }
        if (!g->seen || delta) {
            g->seen = 1; g->heartbeat = reply[3]; g->block = block;
        }
    }
    if (g->seen && block - g->block >= MT_STALL_BLOCKS) g->invalid = 1;
}
