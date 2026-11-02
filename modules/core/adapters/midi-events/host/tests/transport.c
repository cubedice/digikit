#include "transport.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static void reply_checksum(uint32_t *r)
{
    uint32_t i, a = 0, b = 0;
    for (i = 0; i < 15; ++i) { a += r[i]; b += a; }
    r[15] = a ^ b;
}
static void reply_words(uint32_t *r, uint32_t epoch, uint32_t message, uint32_t heartbeat)
{
    uint32_t i;
    for (i = 0; i < MT_REPLY_WORDS; ++i) r[i] = 0;
    r[0] = MT_REPLY_MAGIC; r[1] = epoch; r[2] = message; r[3] = heartbeat;
    reply_checksum(r);
}
uint32_t mi_test_reply_ack(void)
{
    MiLedger m; MiTransport t;
    uint32_t r[MT_REPLY_WORDS], rev[MI_TRACKS] = {1}, message;
    uint8_t note[3] = {0x90, 60, 100};
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    mi_message(&m, 0, note, 3);
    message = mt_prepare(&t, &m, rev, 0)[3];
    reply_words(r, 2, message, 20);
    CHECK(mt_reply_tick(&t, &m, r, 1) == -1 && m.count == 1);
    reply_words(r, 1, message, 20); r[15] ^= 1;
    CHECK(mt_reply_tick(&t, &m, r, 2) == -1 && !t.heartbeat_seen);
    reply_words(r, 1, message, 20); r[4] = 1;
    reply_checksum(r); /* valid integrity, invalid reserved payload */
    CHECK(mt_reply_tick(&t, &m, r, 3) == -1 && m.count == 1);
    reply_words(r, 1, message+1, 20);
    CHECK(mt_reply_tick(&t, &m, r, 4) == 1 && m.count == 1);
    reply_words(r, 1, message, 20);
    CHECK(mt_reply_tick(&t, &m, r, 5) == 0 && m.count == 1);
    reply_words(r, 1, message, 21);
    r[4] = 0x49525231u; r[5] = 17; r[6] = 2; r[7] = 4;
    r[8] = 4; r[9] = 0; r[10] = 0; r[11] = 7;
    for (uint32_t j = 12; j < 15; ++j) {
        r[j] = 1; reply_checksum(r);
        CHECK(!mt_reply_valid(r));
        r[j] = 0;
    }
    reply_checksum(r);
    CHECK(mt_reply_tick(&t, &m, r, 6) == 2 && !m.count);
    CHECK(mt_reply_tick(&t, &m, r, 7) == 0);
    reply_words(r, 1, message, 19);
    CHECK(mt_reply_tick(&t, &m, r, 8) == -1 && t.faults && m.reset_mask);
    CHECK(!m.routes[0].enabled && m.keys[0].tombstone);
    return 0;
}
static void session_reply(uint32_t *r, uint32_t heartbeat);
uint32_t mi_test_reply_watchdog(void)
{
    MtReplyGuard g = {0};
    uint32_t guard_reply[MT_REPLY_WORDS];
    mt_reply_guard_tick(&g, 0, 10000);
    CHECK(!g.seen && !g.invalid);
    CHECK(mt_reply_guard_expect(&g,0xdeadbeefu,17,UINT32_MAX-10)==0);
    session_reply(guard_reply, UINT32_MAX);
    mt_reply_guard_tick(&g, guard_reply, UINT32_MAX-5);
    CHECK(g.seen && !g.invalid);
    session_reply(guard_reply, 0);
    mt_reply_guard_tick(&g, guard_reply, 2);
    CHECK(!g.invalid && g.heartbeat == 0 && g.block == 2);
    mt_reply_guard_tick(&g, guard_reply, 2+MT_STALL_BLOCKS-1);
    CHECK(!g.invalid && g.block == 2); /* cached replies cannot extend liveness */
    mt_reply_guard_tick(&g, guard_reply, 2+MT_STALL_BLOCKS);
    CHECK(g.invalid);
    session_reply(guard_reply, 1);
    mt_reply_guard_tick(&g, guard_reply, 3+MT_STALL_BLOCKS);
    CHECK(g.invalid && g.heartbeat == 0);
    g.seen = g.invalid = 0;
    session_reply(guard_reply, 50);
    mt_reply_guard_tick(&g, guard_reply, 10);
    guard_reply[3] = 1; /* bad checksum must not be mistaken for a reboot */
    mt_reply_guard_tick(&g, guard_reply, 11);
    CHECK(!g.invalid && g.heartbeat == 50);
    reply_checksum(guard_reply);
    mt_reply_guard_tick(&g, guard_reply, 12);
    CHECK(g.invalid);
    g.seen = g.invalid = 0;
    session_reply(guard_reply, 50);
    mt_reply_guard_tick(&g, guard_reply, 10);
    mt_reply_guard_tick(&g, 0, 10+MT_STALL_BLOCKS);
    CHECK(g.invalid);

    MiLedger m; MiTransport t;
    uint32_t r[MT_REPLY_WORDS];
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    reply_words(r, 1, 0, UINT32_MAX);
    CHECK(mt_reply_tick(&t, &m, r, UINT32_MAX-10) == 1);
    reply_words(r, 1, 0, 0);
    CHECK(mt_reply_tick(&t, &m, r, 2) == 1 && !t.faults);
    CHECK(mt_reply_tick(&t, &m, r, 901) == 0 && !t.faults);
    CHECK(mt_reply_tick(&t, &m, 0, 902) == 0 && t.faults == 1);
    CHECK(!m.routes[0].enabled);
    CHECK(mt_reply_tick(&t, &m, 0, 903) == 0 && t.faults == 1);
    mi_recover(&m);
    reply_words(r, 1, 0, 1);
    CHECK(mt_reply_tick(&t, &m, r, 904) == -1 && !t.heartbeat_seen);
    return 0;
}
uint32_t mi_test_reply_record(void)
{
    uint16_t record[0xabc/2]; uint32_t words[MT_REPLY_WORDS], i;
    for (i = 0; i < 0xabc/2; ++i) record[i] = 0xcccc;
    for (i = 0; i < MT_REPLY_WORDS; ++i) {
        uint32_t h = (0x1cu + 0x54u*(21u+i/2u)+0x44u+(i&1u)*4u)/2u;
        record[h] = (uint16_t)(0x1000+i); record[h+1] = (uint16_t)(0x2000+i);
    }
    mt_reply_read(record, words);
    for (i = 0; i < MT_REPLY_WORDS; ++i)
        CHECK(words[i] == ((0x2000u+i)<<16) + 0x1000u+i);
    CHECK(record[0] == 0xcccc && record[0xabc/2-1] == 0xcccc);
    mt_reply_read(0, words);
    for (i = 0; i < MT_REPLY_WORDS; ++i) CHECK(!words[i]);
    return 0;
}
/* Python supplies the actual SHARC-generated acknowledgement to this target
 * entry point; this is not a host-model packet in the cross-processor test. */
uint32_t mi_test_reply_from_dsp(const uint32_t *r)
{
    MiLedger m; MiTransport t;
    uint32_t rev[MI_TRACKS] = {1};
    uint8_t note[3] = {0x90, 60, 100};
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    mi_message(&m, 0, note, 3);
    CHECK(mt_prepare(&t, &m, rev, 0)[3] == 1);
    CHECK(mt_reply_tick(&t, &m, r, 1) == 2 && !m.count);
    CHECK(t.heartbeat == 42);
    return 0;
}
uint32_t mi_test_config_wire[3 * MT_WORDS];
uint32_t mi_test_config_transaction(void)
{
    MiLedger m; MiTransport t;
    uint32_t data[17], i, j, old;
    uint8_t note_message[3] = {0x90, 60, 100};
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    for (i = 0; i < 17; ++i) data[i] = i * 19;
    CHECK(mt_config_begin(&t, &m, 0, 1, 1, data, 0) == -1);
    CHECK(mt_config_begin(&t, &m, 0, 1, 1, data, 65) == -1);
    CHECK(mt_config_begin(&t, &m, 0, 1, 1, data, 17) == 0);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    CHECK(mi_message(&m, 0, note_message, 3) == 1);
    CHECK(mt_config_begin(&t, &m, 1, 1, 1, data, 17) == -2);
    data[0] = 999; /* UI edits cannot mutate the queued snapshot */
    for (i = 0; i < 3; ++i) {
        p = mt_prepare(&t, &m, t.applied, i);
        CHECK(p && p[1] == (i == 2 ? MT_CONFIG_COMMIT : MT_CONFIG_CHUNK));
        CHECK(p[11] == i && p[12] == 68 && p[13] == 1 && !t.applied[0]);
        CHECK(m.count == 1);
        CHECK(p[15] == (i == 0 ? 16u : i == 1 ? 1u : 0u));
        if (!i) CHECK(!p[16] && p[31] == 285);
        if (i == 1) CHECK(p[16] == 304 && !p[17]);
        for (j = 0; j < MT_WORDS; ++j) mi_test_config_wire[i*MT_WORDS+j] = p[j];
        old = p[3];
        CHECK(mt_prepare(&t, &m, t.applied, i+1)[3] == old);
        CHECK(mt_ack(&t, &m, m.epoch, old));
        CHECK(!mt_ack(&t, &m, m.epoch, old));
    }
    CHECK(t.applied[0] == 1 && !t.config_active);
    p = mt_prepare(&t, &m, t.applied, 4);
    CHECK(p && p[1] == (MT_EVENTS | 256u) && p[5] == 1);
    CHECK(mt_ack(&t, &m, m.epoch, p[3]) && !m.count);
    CHECK(mt_config_begin(&t, &m, 0, 1, 1, data, 17) == -1);
    CHECK(mt_config_begin(&t, &m, 0, 2, 1, data, 17) == 0);
    p = mt_prepare(&t, &m, t.applied, 20); old = p[3];
    mi_reset_track(&m, 0);
    CHECK(!mt_ack(&t, &m, m.epoch, old));
    p = mt_prepare(&t, &m, t.applied, 21);
    CHECK(p[1] == MT_RESET && !t.config_active && !t.applied[0]);
    CHECK(mt_ack(&t, &m, m.epoch, p[3]));
    CHECK(!mt_prepare(&t, &m, t.applied, 22));
    CHECK(mt_config_begin(&t, &m, 0, 3, 1, data, 1) == 0);
    p = mt_prepare(&t, &m, t.applied, 23);
    CHECK(mt_ack(&t, &m, m.epoch, p[3]));
    p = mt_prepare(&t, &m, t.applied, 24);
    CHECK(p[1] == MT_CONFIG_COMMIT);
    mi_reset_track(&m, 1); /* commit could have applied, but its ACK was lost */
    CHECK(!mt_ack(&t, &m, m.epoch, p[3]));
    p = mt_prepare(&t, &m, t.applied, 25);
    CHECK(p[1] == MT_RESET && m.reset_mask == 3 && !t.applied[0]);
    return 0;
}
uint32_t mi_test_config_maximum(void)
{
    MiLedger m; MiTransport t;
    uint32_t data[MT_CONFIG_WORDS], i;
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    for (i = 0; i < MT_CONFIG_WORDS; ++i) data[i] = UINT32_MAX-i;
    CHECK(mt_config_begin(&t, &m, 15, UINT32_MAX, 1, data, MT_CONFIG_WORDS) == 0);
    for (i = 0; i < 5; ++i) {
        p = mt_prepare(&t, &m, t.applied, i);
        CHECK(p && p[8] == 15 && p[10] == UINT32_MAX && p[12] == 256);
        CHECK(p[1] == (i < 4 ? MT_CONFIG_CHUNK : MT_CONFIG_COMMIT));
        CHECK(mt_ack(&t, &m, 1, p[3]));
    }
    CHECK(t.applied[15] == UINT32_MAX);
    CHECK(mt_config_begin(&t, &m, 15, 1, 1, data, MT_CONFIG_WORDS) == -1);
    mi_recover(&m);
    CHECK(mt_config_begin(&t, &m, 15, 1, 1, data, MT_CONFIG_WORDS) == 0);
    p = mt_prepare(&t, &m, t.applied, 100);
    CHECK(p && p[2] == 2);
    p = mt_prepare(&t, &m, t.applied, 1000);
    CHECK(p && p[1] == MT_RESET && t.faults && !t.config_active);
    return 0;
}
static void note(MiLedger *m, uint8_t channel, uint8_t key)
{
    uint8_t msg[3] = {(uint8_t)(0x90 | channel), key, 100};
    mi_message(m, 0, msg, 3);
}
uint32_t mi_test_wire_words[2 * MT_WORDS];
uint32_t mi_test_transport_wire(void)
{
    MiLedger m; MiTransport t;
    uint32_t revisions[MI_TRACKS] = {1}, i;
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    mi_reset_track(&m, 0);
    p = mt_prepare(&t, &m, revisions, 0);
    CHECK(p && p[1] == MT_RESET);
    for (i = 0; i < MT_WORDS; ++i) mi_test_wire_words[i] = p[i];
    CHECK(mt_ack(&t, &m, 1, p[3]));
    CHECK(mi_route(&m, 0, 0, 0, m.generations[0], 0) == 0);
    for (i = 0; i < 8; ++i) note(&m, 0, (uint8_t)(60+i));
    p = mt_prepare(&t, &m, revisions, 1);
    CHECK(p && p[1] == (MT_EVENTS | (8u << 8)));
    for (i = 0; i < MT_WORDS; ++i) mi_test_wire_words[MT_WORDS+i] = p[i];
    return 0;
}
uint32_t mi_test_transport_batch(void)
{
    MiLedger m; MiTransport t;
    uint32_t revisions[MI_TRACKS] = {7}, saved[MT_WORDS], i, seq;
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    for (i = 0; i < 10; ++i) note(&m, 0, (uint8_t)(60+i));
    p = mt_prepare(&t, &m, revisions, 42);
    CHECK(p && p[0] == MT_MAGIC && p[1] == (MT_EVENTS | (8u << 8)));
    CHECK(p[2] == 1 && p[4] == 1 && p[5] == 7 && p[6] == 42);
    CHECK(p[7] == mt_check(p) && m.count == 10);
    CHECK(p[8] == 1 && p[9] == 1 && ((p[10] >> 14) & 127) == 60);
    seq = p[3];
    for (i = 0; i < MT_WORDS; ++i) saved[i] = p[i];
    revisions[0] = 8;
    p = mt_prepare(&t, &m, revisions, 43);
    for (i = 0; i < MT_WORDS; ++i) CHECK(saved[i] == p[i]);
    CHECK(!mt_ack(&t, &m, 2, seq) && !mt_ack(&t, &m, 1, seq+1));
    CHECK(mt_ack(&t, &m, 1, seq) && m.count == 2);
    CHECK(!mt_ack(&t, &m, 1, seq));
    p = mt_prepare(&t, &m, revisions, 44);
    CHECK(p[1] == (MT_EVENTS | (2u << 8)) && p[4] == 9 && p[5] == 8);
    for (i = 14; i < MT_WORDS; ++i) CHECK(p[i] == 0);
    CHECK(mt_ack(&t, &m, 1, p[3]) && m.count == 0);
    CHECK(!mt_prepare(&t, &m, revisions, 45));
    return 0;
}
uint32_t mi_test_transport_reset(void)
{
    MiLedger m; MiTransport t;
    uint32_t revisions[MI_TRACKS] = {1}, old;
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    note(&m, 0, 60);
    p = mt_prepare(&t, &m, revisions, 0); old = p[3];
    mi_reset_track(&m, 0);
    CHECK(!mt_ack(&t, &m, 1, old));
    p = mt_prepare(&t, &m, revisions, 1);
    CHECK(p[1] == MT_RESET && p[3] != old && p[8] == 0 && p[9] == 2);
    CHECK(!mt_ack(&t, &m, 1, old) && m.reset_mask == 1);
    CHECK(mt_ack(&t, &m, 1, p[3]) && !m.reset_mask);
    CHECK(!mt_prepare(&t, &m, revisions, 2) && !m.count);
    CHECK(m.keys[0].used && m.keys[0].tombstone && !m.routes[0].enabled);
    return 0;
}
uint32_t mi_test_transport_partition(void)
{
    MiLedger m; MiTransport t;
    uint32_t revisions[MI_TRACKS] = {0, 2};
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    CHECK(mi_route(&m, 0, 1, 1, 1, 0) == 0);
    note(&m, 0, 60); note(&m, 1, 64); note(&m, 0, 67);
    CHECK(!mt_prepare(&t, &m, revisions, 0) && m.count == 3);
    revisions[0] = 3;
    p = mt_prepare(&t, &m, revisions, 1);
    CHECK(p[1] == (MT_EVENTS | 256) && p[5] == 3);
    CHECK(mt_ack(&t, &m, 1, p[3]));
    p = mt_prepare(&t, &m, revisions, 2);
    CHECK(p[1] == (MT_EVENTS | 256) && p[5] == 2);
    CHECK(((p[10] >> 10) & 15) == 1 && ((p[10] >> 6) & 15) == 1);
    CHECK(mt_ack(&t, &m, 1, p[3]));
    p = mt_prepare(&t, &m, revisions, 3);
    CHECK(p[4] == 3 && p[5] == 3);
    return 0;
}
uint32_t mi_test_transport_timeout(void)
{
    MiLedger m; MiTransport t;
    uint32_t revisions[MI_TRACKS] = {1}, old, i;
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    note(&m, 0, 60);
    p = mt_prepare(&t, &m, revisions, UINT32_MAX-100); old = p[3];
    p = mt_prepare(&t, &m, revisions, 800);
    CHECK(t.faults == 1 && p[1] == MT_RESET && !m.routes[0].enabled);
    CHECK(!mt_ack(&t, &m, 1, old));
    for (i = 0; i < MI_TRACKS; ++i) {
        p = mt_prepare(&t, &m, revisions, 801+i);
        CHECK(p && p[1] == MT_RESET && p[8] == i);
        CHECK(mt_ack(&t, &m, 1, p[3]));
    }
    CHECK(!mt_prepare(&t, &m, revisions, 900));
    mi_recover(&m);
    p = mt_prepare(&t, &m, revisions, 901);
    CHECK(p && p[2] == 2 && p[3] == 1 && !t.faults);
    CHECK(!mt_ack(&t, &m, 1, 1));
    return 0;
}
uint32_t mi_test_transport_wrap(void)
{
    MiLedger m; MiTransport t;
    uint32_t revisions[MI_TRACKS] = {1};
    const uint32_t *p;
    mi_init(&m); mt_init(&t);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    note(&m, 0, 60);
    mt_prepare(&t, &m, revisions, 0);
    t.pending = 0; t.next_message = UINT32_MAX;
    p = mt_prepare(&t, &m, revisions, 1);
    CHECK(p && p[3] == UINT32_MAX);
    CHECK(mt_ack(&t, &m, 1, p[3]));
    CHECK(!mt_prepare(&t, &m, revisions, 2));
    CHECK(t.faults && m.reset_mask && m.keys[0].tombstone);
    return 0;
}


static void session_reply(uint32_t *r, uint32_t heartbeat)
{
    reply_words(r, 0, 0, heartbeat);
    r[0] = MT_SESSION_REPLY_MAGIC;
    r[4] = 0x49525231u; r[5] = 17; r[7] = 0xdeadbeefu;
    r[8] = 5; r[12] = 0xdeadbeefu;
    reply_checksum(r);
}
uint32_t mi_test_session_guard(void)
{
    MtReplyGuard g = {0}; uint32_t r[MT_REPLY_WORDS], i;
    CHECK(mt_reply_guard_expect(&g, 0, 17, 0) == -1 && !g.expected);
    CHECK(mt_reply_guard_expect(&g, 1, 0, 0) == -1 && !g.expected);
    CHECK(mt_reply_guard_expect(&g, 0xdeadbeefu, 17, UINT32_MAX-5) == 0);
    CHECK(mt_reply_guard_expect(&g, 0xdeadbeefu, 17, 2) == -2);
    CHECK(g.started == UINT32_MAX-5);
    reply_words(r, 0, 0, 1);
    mt_reply_guard_tick(&g, r, 0);
    CHECK(!g.ready && !g.invalid);
    session_reply(r, 2);
    mt_reply_guard_tick(&g, r, 1);
    CHECK(g.ready && !g.invalid);
    /* Later query/lifecycle ACK identity does not replace the session token. */
    r[5] = 18; r[7] = 4; r[8] = 1; r[3] = 3; reply_checksum(r);
    mt_reply_guard_tick(&g, r, 2);
    CHECK(g.ready && !g.invalid);
    r[12]++; r[3]++; reply_checksum(r);
    mt_reply_guard_tick(&g, r, 3);
    CHECK(g.invalid);
    session_reply(r, 5); mt_reply_guard_tick(&g, r, 4);
    CHECK(g.invalid && g.heartbeat == 3);
    CHECK(mt_reply_guard_expect(&g, 1, 19, 4) == -2);
    /* Every bind identifier, status/capability and token must match. */
    for (i = 4; i <= 12; ++i) {
        MtReplyGuard bad = {0};
        CHECK(mt_reply_guard_expect(&bad, 0xdeadbeefu, 17, 0) == 0);
        session_reply(r, 1); r[i]++; reply_checksum(r);
        mt_reply_guard_tick(&bad, r, 1);
        CHECK(!bad.ready);
        if (i == 4) mt_reply_guard_tick(&bad, 0, MT_STALL_BLOCKS);
        CHECK(bad.invalid);
    }
    {
        MtReplyGuard unexpected = {0};
        session_reply(r, 1); mt_reply_guard_tick(&unexpected, r, 0);
        CHECK(unexpected.invalid && !unexpected.ready);
    }
    {
        MtReplyGuard downgrade = {0};
        CHECK(mt_reply_guard_expect(&downgrade, 0xdeadbeefu, 17, 0) == 0);
        session_reply(r, 1); mt_reply_guard_tick(&downgrade, r, 1);
        reply_words(r, 0, 0, 2); mt_reply_guard_tick(&downgrade, r, 2);
        CHECK(downgrade.invalid);
    }
    {
        MtReplyGuard timeout = {0};
        CHECK(mt_reply_guard_expect(&timeout, 0xdeadbeefu, 17, UINT32_MAX-5) == 0);
        reply_words(r, 0, 0, 1); mt_reply_guard_tick(&timeout, r, 0);
        session_reply(r, 2);
        mt_reply_guard_tick(&timeout, r, MT_STALL_BLOCKS-6);
        CHECK(timeout.invalid && !timeout.ready); /* exact wrapped deadline */
    }
    {
        MtReplyGuard stale = {0};
        CHECK(mt_reply_guard_expect(&stale, 0xdeadbeefu, 17, 0) == 0);
        session_reply(r, 1); mt_reply_guard_tick(&stale, r, 1);
        mt_reply_guard_tick(&stale, r, MT_STALL_BLOCKS);
        CHECK(!stale.invalid && stale.block == 1);
        mt_reply_guard_tick(&stale, r, MT_STALL_BLOCKS+1);
        CHECK(stale.invalid);
    }
    {
        MtReplyGuard corrupt = {0};
        CHECK(mt_reply_guard_expect(&corrupt, 0xdeadbeefu, 17, 0) == 0);
        session_reply(r, 1); r[15] ^= 1;
        mt_reply_guard_tick(&corrupt, r, 1);
        CHECK(!corrupt.ready && !corrupt.invalid);
        mt_reply_guard_tick(&corrupt, 0, MT_STALL_BLOCKS);
        CHECK(corrupt.invalid && !corrupt.ready);
    }
    return 0;
}
uint32_t mi_test_session_from_dsp(const uint32_t *r)
{
    MtReplyGuard g = {0}; MiTransport t; MiLedger m;
    CHECK(mt_reply_guard_expect(&g, 0xdeadbeefu, 17, 0) == 0);
    CHECK(mt_reply_valid(r));
    mt_reply_guard_tick(&g, r, 1);
    CHECK(g.ready && !g.invalid && g.heartbeat == r[3]);
    mi_init(&m); mt_init(&t);
    CHECK(mt_reply_tick(&t, &m, r, 1) == -1 && !t.heartbeat_seen);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 1, &g) == 1 && t.heartbeat_seen);
    return 0;
}

uint32_t mi_test_session_midi(void)
{
    MtReplyGuard g = {0}; MiTransport t; MiLedger m;
    uint32_t r[MT_REPLY_WORDS], rev[MI_TRACKS] = {1}, message, i;
    uint32_t config[1] = {7};
    mi_init(&m); mt_init(&t);
    CHECK(mt_reply_guard_expect(&g, 0xdeadbeefu, 17, 0) == 0);
    reply_words(r, m.epoch, 0, 1);
    mt_reply_guard_tick(&g, r, 1);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 1, &g) == 0 && !t.heartbeat_seen);
    session_reply(r, 2); r[1] = m.epoch; reply_checksum(r);
    mt_reply_guard_tick(&g, r, 2);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 2, &g) == 1);
    CHECK(g.ready && t.heartbeat_seen);
    CHECK(mt_config_begin(&t, &m, 0, 1, 1, config, 1) == 0);
    for (i = 0; i < 2; ++i) {
        message = mt_prepare(&t, &m, rev, 3+i)[3];
        r[2] = message; r[3]++; reply_checksum(r);
        mt_reply_guard_tick(&g, r, 3+i);
        CHECK(mt_reply_tick_guarded(&t, &m, r, 3+i, &g) == 2);
        CHECK(t.applied[0] == (i ? 1u : 0u));
    }
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    note(&m, 0, 60);
    message = mt_prepare(&t, &m, rev, 5)[3];
    r[2] = message; r[3]++; reply_checksum(r);
    mt_reply_guard_tick(&g, r, 5);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 5, &g) == 2 && !m.count);
    note(&m, 0, 64);
    message = mt_prepare(&t, &m, rev, 6)[3];
    /* Valid checksum and exact event ACK from a different session must not
     * retire this batch, refresh trust, or let held notes survive revocation. */
    r[2] = message; r[3]++; r[12]++; reply_checksum(r);
    mt_reply_guard_tick(&g, r, 6);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 6, &g) == -1);
    CHECK(g.invalid && t.faults && !t.applied[0] && m.reset_mask);
    CHECK(m.keys[0].tombstone);
    r[12]--; r[3]++; reply_checksum(r);
    mt_reply_guard_tick(&g, r, 7);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 7, &g) == -1 && t.faults == 1);
    return 0;
}

uint32_t mi_test_reply_reordering(void)
{
    {
        MtReplyGuard unbound={0}; uint32_t before[16];
        reply_words(before,1,0,100); mt_reply_guard_tick(&unbound,before,1);
        before[3]=1; reply_checksum(before); mt_reply_guard_tick(&unbound,before,2);
        CHECK(!unbound.invalid && !unbound.seen && !unbound.ready);
        CHECK(mt_reply_guard_expect(&unbound,0xdeadbeefu,17,3)==0);
        mt_reply_guard_tick(&unbound,before,4);
        CHECK(!unbound.invalid && !unbound.seen && !unbound.ready);
        mt_reply_guard_tick(&unbound,before,3+MT_STALL_BLOCKS);
        CHECK(unbound.invalid && !unbound.ready);
    }
    MtReplyGuard g = {0}; MiTransport t; MiLedger m;
    uint32_t r[MT_REPLY_WORDS], rev[MI_TRACKS] = {1}, message, i;
    mi_init(&m); mt_init(&t);
    CHECK(mt_reply_guard_expect(&g, 0xdeadbeefu, 17, 0) == 0);
    for (i = 93; i <= 100; ++i) {
        session_reply(r, i); r[1] = m.epoch; reply_checksum(r);
        CHECK(!mt_reply_reordered(&g, r));
        mt_reply_guard_tick(&g, r, i);
        CHECK(mt_reply_tick_guarded(&t, &m, r, i, &g) == 1);
    }
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    note(&m, 0, 60);
    message = mt_prepare(&t, &m, rev, 101)[3];
    for (i = 1; i <= 7; ++i) {
        r[3] = 100-i; reply_checksum(r);
        CHECK(mt_reply_reordered(&g, r));
        mt_reply_guard_tick(&g, 0, 101+i);
        CHECK(mt_reply_tick_guarded(&t, &m, 0, 101+i, &g) == 0);
        CHECK(m.count == 1 && !g.invalid && g.heartbeat == 100 && g.block == 100);
        /* Same heartbeat with a new ACK is not a known copy. */
        r[2] = message; reply_checksum(r); CHECK(!mt_reply_reordered(&g, r));
        r[2] = 0;
    }
    r[3] = 92; reply_checksum(r); CHECK(!mt_reply_reordered(&g, r));
    r[3] = 99; r[12]++; reply_checksum(r); CHECK(!mt_reply_reordered(&g, r));
    r[12]--; r[0] = MT_REPLY_MAGIC; r[12] = 0; reply_checksum(r);
    CHECK(!mt_reply_reordered(&g, r));
    session_reply(r, 101); r[1] = m.epoch; r[2] = message; reply_checksum(r);
    CHECK(!mt_reply_reordered(&g, r));
    mt_reply_guard_tick(&g, r, 109);
    CHECK(mt_reply_tick_guarded(&t, &m, r, 109, &g) == 2 && !m.count);
    session_reply(r, 100); r[1] = m.epoch; reply_checksum(r);
    CHECK(mt_reply_reordered(&g, r));
    r[3] = 93; reply_checksum(r); CHECK(!mt_reply_reordered(&g, r)); /* evicted */
    mt_reply_guard_tick(&g, 0, 109+MT_STALL_BLOCKS);
    CHECK(g.invalid); /* repeated stale copies never extend freshness */
    return 0;
}

uint32_t mi_test_play_reply(void)
{
    uint32_t r[16]; MtReplyGuard g={0};
    CHECK(mt_reply_guard_expect(&g,0xdeadbeefu,17,0)==0);
    session_reply(r,1); r[0]=MT_PLAY_REPLY_MAGIC; reply_checksum(r);
    CHECK(mt_reply_valid(r)); mt_reply_guard_tick(&g,r,1);
    CHECK(g.ready && !g.invalid);
    r[3]=2; r[13]=3; r[14]=9; reply_checksum(r);
    CHECK(mt_reply_valid(r)); mt_reply_guard_tick(&g,r,2);
    CHECK(!g.invalid && g.heartbeat==2);
    r[13]=5; reply_checksum(r); CHECK(mt_reply_valid(r));
    r[13]=7; reply_checksum(r); CHECK(!mt_reply_valid(r));
    r[13]=3; r[14]=0; reply_checksum(r); CHECK(!mt_reply_valid(r));
    r[14]=9; r[0]=MT_SESSION_REPLY_MAGIC; reply_checksum(r); CHECK(!mt_reply_valid(r));
    return 0;
}

static void status_reply(uint32_t *r,uint32_t heartbeat)
{
    for(uint32_t i=0;i<16;++i) r[i]=0;
    r[0]=MT_STATUS_REPLY_MAGIC; r[1]=0xdeadbeefu; r[2]=heartbeat;
    /* Arbitrary normalized payload, including values that resemble ACKs. */
    for(uint32_t i=3;i<15;++i) r[i]=0x49525231u+i;
    reply_checksum(r);
}
uint32_t mi_test_status_reply(void)
{
    MtReplyGuard g={0}; MiTransport t; MiLedger m;
    uint32_t r[16],older[16],rev[MI_TRACKS]={1},message;
    mi_init(&m); mt_init(&t);
    CHECK(mt_reply_guard_expect(&g,0xdeadbeefu,17,0)==0);
    session_reply(r,100); r[1]=m.epoch; reply_checksum(r);
    mt_reply_guard_tick(&g,r,1);
    CHECK(mt_reply_tick_guarded(&t,&m,r,1,&g)==1 && g.ready);
    CHECK(mi_route(&m,0,0,0,1,0)==0); note(&m,0,60);
    message=mt_prepare(&t,&m,rev,2)[3];
    status_reply(r,101); CHECK(mt_reply_valid(r));
    mt_reply_guard_tick(&g,r,2);
    CHECK(mt_reply_tick_guarded(&t,&m,r,2,&g)==1 && m.count==1 && !g.invalid);
    for(uint32_t i=0;i<16;++i) older[i]=r[i];
    status_reply(r,102); mt_reply_guard_tick(&g,r,3);
    CHECK(mt_reply_tick_guarded(&t,&m,r,3,&g)==1 && m.count==1);
    CHECK(mt_reply_reordered(&g,older));
    mt_reply_guard_tick(&g,0,4);
    CHECK(mt_reply_tick_guarded(&t,&m,0,4,&g)==0 && g.block==3);
    session_reply(r,103); r[1]=m.epoch; r[2]=message; reply_checksum(r);
    mt_reply_guard_tick(&g,r,5);
    CHECK(mt_reply_tick_guarded(&t,&m,r,5,&g)==2 && !m.count && !g.invalid);
    status_reply(r,104); r[1]++; reply_checksum(r);
    mt_reply_guard_tick(&g,r,6); CHECK(g.invalid);
    return 0;
}
uint32_t mi_test_status_cannot_bind(void)
{
    MtReplyGuard g={0}; uint32_t r[16]; MiTransport t; MiLedger m;
    mi_init(&m); mt_init(&t);
    CHECK(mt_reply_guard_expect(&g,0xdeadbeefu,17,0)==0);
    status_reply(r,1); CHECK(mt_reply_valid(r));
    CHECK(mt_reply_tick(&t,&m,r,1)==-1 && !t.heartbeat_seen);
    mt_reply_guard_tick(&g,r,1); CHECK(g.invalid && !g.ready);
    r[1]=0; reply_checksum(r); CHECK(!mt_reply_valid(r));
    status_reply(r,1); r[15]^=1; CHECK(!mt_reply_valid(r));
    MtReplyGuard bound={0}; CHECK(mt_reply_guard_expect(&bound,0xdeadbeefu,17,0)==0);
    session_reply(r,100); mt_reply_guard_tick(&bound,r,1);
    status_reply(r,99); CHECK(!mt_reply_reordered(&bound,r));
    mt_reply_guard_tick(&bound,r,2); CHECK(bound.invalid);
    return 0;
}
