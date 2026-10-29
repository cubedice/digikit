#include "transport.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
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
