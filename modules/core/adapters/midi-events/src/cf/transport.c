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

static void mapping_fault(MiLedger *m,uint32_t track)
{
    for (uint32_t i=0;i<MI_ROUTES;++i)
        if (m->routes[i].track==track) m->routes[i].quarantined=1;
    mi_reset_track(m,track);
}

const uint32_t *mt_prepare(MiTransport *t, MiLedger *m,
                           const uint32_t revisions[MI_TRACKS], uint32_t block)
{ return mt_prepare_mapped(t,m,revisions,block,0); }

const uint32_t *mt_prepare_mapped(MiTransport *t, MiLedger *m,
    const uint32_t revisions[MI_TRACKS], uint32_t block, MtTransform transform)
{
    uint32_t i, kind, track, count = 0, consumed = 0;
    MiEvent first, input[8], output[8];
    first.sequence=0; first.track=0;
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
        if (t->faults) return 0;
        /* Publish a pending UI edit before consuming a new controller. This
         * keeps an eight-note voicing diff plus bend within packet capacity. */
        if (transform && revisions[0]) {
            int result=transform(0,m->epoch,m->generations[0],input,0,output);
            if (result<0 || result>8) { mapping_fault(m,0); return 0; }
            if (result) { track=0; kind=MT_EVENTS; count=(uint32_t)result; goto assemble; }
        }
        int present=mi_peek(m,&first);
        if (!present) return 0;
        track=present ? first.track : 0;
        if (!revisions[track]) return 0;
        kind=MT_EVENTS;
        for (i=0;present && i<m->count && i<8;++i) {
            const MiEvent *e=&m->events[(m->head+i)%MI_EVENTS];
            if (e->epoch!=m->epoch || e->track!=track ||
                e->generation!=m->generations[track] || e->sequence!=first.sequence+i) break;
            if (transform && i && (first.kind>MI_NOTE_OFF || e->kind>MI_NOTE_OFF)) break;
            mi_event_copy(&input[consumed++],e);
        }
        if (transform) {
            int result=transform(track,m->epoch,m->generations[track],input,consumed,output);
            if (result<0 || result>8) { mapping_fault(m,track); return 0; }
            count=(uint32_t)result;
        } else { count=consumed; for (i=0;i<count;++i) mi_event_copy(&output[i],&input[i]); }
        if (!count) {
            for (i=0;i<consumed;++i) mi_ack(m,m->epoch,first.sequence+i);
            return 0;
        }
    }
assemble:
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
        t->source_sequence=first.sequence;
        /* Source and audible event counts differ. DSP ordering is packet-based;
         * mapped batches use a local nonzero sequence, never the physical FIFO. */
        t->words[4] = transform ? 1 : first.sequence;
        t->words[5] = revisions[track];
        for (i = 0; i < count; ++i) {
            const MiEvent *e = &output[i];
            t->words[8 + 3*i] = e->token;
            t->words[9 + 3*i] = e->generation;
            t->words[10 + 3*i] = e->kind | ((uint32_t)e->port << 4) |
                ((uint32_t)e->channel << 6) | ((uint32_t)e->track << 10) |
                ((uint32_t)e->note << 14) | ((uint32_t)e->value << 21);
        }
    }
    if (kind==MT_EVENTS && transform && output[0].sequence) {
        kind=MT_MAPPED;
        t->words[4]=output[0].sequence;
        for (i=1;i<count;++i) if (output[i].sequence!=output[0].sequence) {
            mapping_fault(m,track); return 0;
        }
    }
    t->words[1] = kind | (count << 8);
    t->words[7] = mt_check(t->words);
    t->pending = kind; t->count = consumed; t->sent_block = block;
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
            if (e->epoch != epoch || e->sequence != t->source_sequence + i ||
                e->track != t->track || e->generation != t->generation) return 0;
        }
        for (i = 0; i < t->count; ++i) mi_ack(m, epoch, t->source_sequence + i);
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

static int session_reply(uint32_t magic)
{ return magic == MT_SESSION_REPLY_MAGIC || magic == MT_PLAY_REPLY_MAGIC; }

int mt_reply_valid(const uint32_t reply[MT_REPLY_WORDS])
{
    uint32_t i, a = 0, b = 0;
    if (!reply || (reply[0] != MT_REPLY_MAGIC && !session_reply(reply[0]))) return 0;
    if (session_reply(reply[0]) && !reply[12]) return 0;
    for (i = 0; i < MT_REPLY_WORDS-1; ++i) {
        a += reply[i]; b += a;
        if (((i >= 13 && reply[0] != MT_PLAY_REPLY_MAGIC) || (i == 12 && reply[0] == MT_REPLY_MAGIC)) && reply[i]) return 0;
    }
    if (reply[0] == MT_PLAY_REPLY_MAGIC && (reply[13] > 6 || (reply[13] && !reply[14]))) return 0;
    if (reply[15] != (a ^ b)) return 0;
    if (reply[4] == 0x49525231u) return 1; /* registry validates its own identity/status */
    for (i = 4; i < 12; ++i) if (reply[i]) return 0;
    return 1;
}

static int reply_tick(MiTransport *t, MiLedger *m,
                      const uint32_t *reply, uint32_t block, uint32_t session)
{
    uint32_t valid = mt_reply_valid(reply) &&
        (session ? session_reply(reply[0]) && reply[12] == session :
                   reply[0] == MT_REPLY_MAGIC);
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

int mt_reply_tick(MiTransport *t, MiLedger *m,
                  const uint32_t reply[MT_REPLY_WORDS], uint32_t block)
{
    return reply_tick(t, m, reply, block, 0);
}

int mt_reply_tick_guarded(MiTransport *t, MiLedger *m, const uint32_t *reply,
                          uint32_t block, const MtReplyGuard *g)
{
    if (!g || !g->expected) return mt_reply_tick(t, m, reply, block);
    if (g->invalid) {
        if (!t->faults) fault(t, m);
        return -1;
    }
    if (!g->ready) return 0;
    return reply_tick(t, m, reply, block, g->expected);
}


int mt_reply_guard_expect(MtReplyGuard *g, uint32_t token,
                          uint32_t sequence, uint32_t block)
{
    if (!token || !sequence) return -1;
    if (g->invalid || g->expected) return -2;
    g->expected = token; g->sequence = sequence; g->started = block;
    return 0;
}

int mt_reply_reordered(const MtReplyGuard *g, const uint32_t *reply)
{
    uint32_t behind, i, j;
    if (!g->ready || !g->expected || g->invalid || !mt_reply_valid(reply) ||
        !session_reply(reply[0]) || reply[12] != g->expected) return 0;
    behind = g->heartbeat - reply[3];
    if (!behind || behind >= 0x80000000u) return 0;
    for (i = 0; i < g->history_count; ++i) {
        for (j = 0; j < MT_REPLY_WORDS; ++j)
            if (g->history[i][j] != reply[j]) break;
        if (j == MT_REPLY_WORDS) return 1;
    }
    return 0;
}

/* Sticky diagnostic for the first trust failure of a guard. No reply pointer
 * escapes this call. This records why memory was retained; it cannot recover
 * or weaken the session guard. reason, CF block, reply heartbeat, prior heartbeat,
 * reply magic. Physical-reset invalidation is separately recorded by reset_seen. */
volatile uint32_t mt_guard_fault[5];
static void guard_fail(MtReplyGuard *g, const uint32_t *reply,
                       uint32_t block, uint32_t reason)
{
    mt_guard_fault[0] = reason;
    mt_guard_fault[1] = block;
    mt_guard_fault[2] = reply ? reply[3] : 0;
    mt_guard_fault[3] = g->heartbeat;
    mt_guard_fault[4] = reply ? reply[0] : 0;
    g->invalid = 1;
}

void mt_reply_guard_tick(MtReplyGuard *g, const uint32_t *reply, uint32_t block)
{
    uint32_t delta;
    if (g->invalid) return;
    if ((g->expected && !g->ready && block - g->started >= MT_STALL_BLOCKS) ||
        (g->seen && block - g->block >= MT_STALL_BLOCKS)) {
        guard_fail(g, reply, block, 1); return;
    }
    if (mt_reply_valid(reply)) {
        /* An unbound heartbeat is not evidence about this session's lifetime.
         * The exact bind ACK establishes its first trusted heartbeat. Legacy
         * traffic cannot refresh the bind deadline; physical reset still revokes. */
        if (!g->ready && reply[0] == MT_REPLY_MAGIC) return;
        if (session_reply(reply[0])) {
            if (!g->expected || reply[12] != g->expected) { guard_fail(g, reply, block, 2); return; }
            if (!g->ready) {
                if (reply[4] != 0x49525231u || reply[5] != g->sequence ||
                    reply[6] || reply[7] != g->expected || reply[8] != 5 ||
                    reply[9] || reply[10] || reply[11]) { guard_fail(g, reply, block, 3); return; }
            }
        } else if (g->ready) { guard_fail(g, reply, block, 4); return; }

        delta = reply[3] - g->heartbeat;
        if (g->seen && delta >= 0x80000000u) { guard_fail(g, reply, block, 5); return; }
        if (!g->seen || delta) {
            uint32_t i;
            g->seen = 1; g->heartbeat = reply[3]; g->block = block;
            if (session_reply(reply[0])) g->ready = 1;
            for (i = 0; i < MT_REPLY_WORDS; ++i)
                g->history[g->history_next][i] = reply[i];
            g->history_next = (g->history_next + 1) & 7u;
            if (g->history_count < 8) ++g->history_count;
        }
    }
    if (g->seen && block - g->block >= MT_STALL_BLOCKS) guard_fail(g, reply, block, 6);
}
