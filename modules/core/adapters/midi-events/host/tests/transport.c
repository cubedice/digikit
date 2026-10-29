#include "transport.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
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
