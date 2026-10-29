#include "ownership.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)

static int message(MiLedger *m, uint32_t port, uint8_t status, uint8_t a, uint8_t b)
{
    uint8_t bytes[3] = {status, a, b};
    return mi_message(m, port, bytes, 3);
}
static MiEvent take(MiLedger *m)
{
    MiEvent e = {0};
    if (mi_peek(m, &e)) mi_ack(m, e.epoch, e.sequence);
    return e;
}

uint32_t mi_test_fifo(void)
{
    MiLedger m;
    MiEvent a, b, again, off;
    mi_init(&m);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    CHECK(message(&m, 0, 0x90, 60, 30) == 1);
    CHECK(message(&m, 0, 0x90, 60, 120) == 1);
    CHECK(mi_peek(&m, &a));
    CHECK(mi_peek(&m, &again) && again.token == a.token);
    CHECK(!mi_ack(&m, a.epoch + 1, a.sequence));
    CHECK(mi_ack(&m, a.epoch, a.sequence));
    CHECK(!mi_ack(&m, a.epoch, a.sequence));
    b = take(&m);
    CHECK(b.token != a.token && a.value == 30 && b.value == 120);
    CHECK(message(&m, 0, 0x80, 60, 0));
    off = take(&m);
    CHECK(off.kind == MI_NOTE_OFF && off.token == a.token);
    CHECK(message(&m, 0, 0x90, 60, 0));
    off = take(&m);
    CHECK(off.kind == MI_NOTE_OFF && off.token == b.token);
    CHECK(!mi_peek(&m, &off));
    return 0;
}

uint32_t mi_test_reroute(void)
{
    MiLedger m;
    MiEvent old, fresh, off;
    mi_init(&m);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    message(&m, 0, 0x90, 60, 90); old = take(&m);
    mi_reset_track(&m, 0);
    CHECK(m.reset_mask == 1 && m.generations[0] == 2);
    CHECK(mi_route(&m, 0, 0, 0, 2, 0) == -2);
    CHECK(!mi_reset_ack(&m, m.epoch, 0, 1));
    CHECK(mi_reset_ack(&m, m.epoch, 0, 2));
    CHECK(mi_route(&m, 0, 0, 15, 1, 0) == 0);
    message(&m, 0, 0x90, 60, 90); fresh = take(&m);
    CHECK(fresh.track == 15 && fresh.token != old.token);
    message(&m, 0, 0x80, 60, 0);
    CHECK(!mi_peek(&m, &off));
    message(&m, 0, 0x80, 60, 0); off = take(&m);
    CHECK(off.token == fresh.token && off.track == 15);
    return 0;
}

uint32_t mi_test_overflow(void)
{
    MiLedger m;
    MiEvent e;
    uint32_t i, epoch;
    mi_init(&m);
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    CHECK(mi_route(&m, 1, 15, 15, 1, 0) == 0);
    for (i = 0; i < MI_EVENTS; ++i) CHECK(message(&m, 0, 0xb0, 64, 127));
    CHECK(m.count == MI_EVENTS);
    CHECK(message(&m, 0, 0x90, 60, 90));
    CHECK(m.overruns == 1 && m.reset_mask == 0x8001 && !m.count);
    CHECK(m.routes[0].quarantined && m.routes[31].quarantined);
    CHECK(!mi_peek(&m, &e));
    CHECK(mi_reset_ack(&m, m.epoch, 0, 2) && mi_reset_ack(&m, m.epoch, 15, 2));
    CHECK(mi_route(&m, 0, 0, 0, 2, 0) == -2);
    epoch = m.epoch;
    mi_recover(&m);
    CHECK(m.epoch == epoch + 1 && m.reset_mask == 0x8001);
    CHECK(mi_reset_ack(&m, m.epoch, 0, 1) && mi_reset_ack(&m, m.epoch, 15, 1));
    CHECK(mi_route(&m, 0, 0, 0, 1, 0) == 0);
    for (i = 0; i < MI_KEYS; ++i) { message(&m, 0, 0x90, 60, 90); take(&m); }
    CHECK(message(&m, 0, 0x90, 60, 90));
    CHECK(m.overruns == 2 && m.routes[0].quarantined);
    return 0;
}

uint32_t mi_test_messages(void)
{
    MiLedger m;
    MiEvent e;
    uint8_t sysex[] = {0xf0, 0x7d, 0x64, 0x6b, 1, 0xf7};
    uint8_t realtime = 0xf8;
    mi_init(&m);
    CHECK(mi_route(&m, 1, 15, 15, 1, 0) == 0);
    CHECK(!message(&m, 0, 0x9f, 0, 127));
    CHECK(!message(&m, 1, 0x90, 0, 127));
    CHECK(!message(&m, 2, 0x9f, 0, 127));
    CHECK(!message(&m, 1, 0x9f, 128, 127));
    CHECK(!mi_message(&m, 1, sysex, sizeof(sysex)));
    CHECK(!mi_message(&m, 1, &realtime, 1));
    CHECK(!mi_message(&m, 1, 0, 3));
    CHECK(!message(&m, 1, 0xbf, 1, 127));
    CHECK(message(&m, 1, 0x9f, 0, 127)); e = take(&m);
    CHECK(e.note == 0 && e.track == 15 && e.port == 1 && e.channel == 15);
    CHECK(message(&m, 1, 0xef, 127, 127)); e = take(&m);
    CHECK(e.kind == MI_BEND && e.token == 0 && ((e.value << 7) | e.note) == 16383);
    CHECK(message(&m, 1, 0xbf, 64, 127)); e = take(&m);
    CHECK(e.kind == MI_CC && e.note == 64 && e.value == 127);
    return 0;
}

uint32_t mi_test_panic(void)
{
    MiLedger m;
    MiEvent fresh, e;
    uint32_t cc;
    for (cc = 120; cc <= 123; cc += 3) {
        mi_init(&m); mi_route(&m, 0, 0, 0, 1, 0);
        message(&m, 0, 0x90, 60, 90); take(&m);
        message(&m, 0, 0xb0, (uint8_t)cc, 0); e = take(&m);
        CHECK(e.kind == MI_CC && e.note == cc);
        message(&m, 0, 0x90, 60, 90); fresh = take(&m);
        message(&m, 0, 0x80, 60, 0); CHECK(!mi_peek(&m, &e));
        message(&m, 0, 0x80, 60, 0); e = take(&m);
        CHECK(e.token == fresh.token && e.kind == MI_NOTE_OFF);
    }
    return 0;
}

uint32_t mi_test_reset_presses(void)
{
    MiLedger m;
    MiEvent fresh, e;
    mi_init(&m); mi_route(&m, 0, 0, 0, 1, 0);
    message(&m, 0, 0x90, 55, 90); /* queued event must be discarded after reset */
    mi_reset_track(&m, 0);
    message(&m, 0, 0x90, 60, 90); /* ignored while waiting for reset */
    CHECK(!mi_peek(&m, &e));
    mi_reset_ack(&m, m.epoch, 0, 2); mi_route(&m, 0, 0, 0, 2, 0);
    CHECK(!mi_peek(&m, &e));
    message(&m, 0, 0x90, 60, 90); fresh = take(&m);
    message(&m, 0, 0x80, 60, 0); CHECK(!mi_peek(&m, &e));
    message(&m, 0, 0x80, 60, 0); e = take(&m);
    CHECK(e.token == fresh.token);
    mi_reset_track(&m, 0); mi_reset_ack(&m, m.epoch, 0, 3);
    mi_route(&m, 0, 0, 0, 3, 1);
    message(&m, 0, 0x90, 60, 90);
    CHECK(!mi_peek(&m, &e));
    mi_reset_track(&m, 0); mi_reset_ack(&m, m.epoch, 0, 4);
    mi_route(&m, 0, 0, 0, 4, 0);
    message(&m, 0, 0x90, 60, 90); fresh = take(&m);
    message(&m, 0, 0x80, 60, 0); CHECK(!mi_peek(&m, &e));
    message(&m, 0, 0x80, 60, 0); e = take(&m);
    CHECK(e.token == fresh.token);
    return 0;
}

uint32_t mi_test_token_wrap(void)
{
    MiLedger m;
    MiEvent e;
    mi_init(&m); mi_route(&m, 0, 0, 0, 1, 0);
    m.next_token = UINT32_MAX;
    message(&m, 0, 0x90, 60, 90); e = take(&m);
    CHECK(e.token == UINT32_MAX);
    message(&m, 0, 0x90, 64, 90);
    CHECK(m.reset_mask == 1 && m.routes[0].quarantined);
    CHECK(!mi_peek(&m, &e));
    return 0;
}

uint32_t mi_test_ring_wrap(void)
{
    MiLedger m;
    MiEvent e;
    uint32_t i, last = 0;
    mi_init(&m); mi_route(&m, 0, 0, 0, 1, 0);
    for (i = 0; i < 1024; ++i) {
        message(&m, 0, 0x90, (uint8_t)(i % 128), 90);
        CHECK(!mi_ack(&m, m.epoch, last));
        e = take(&m); CHECK(e.kind == MI_NOTE_ON && e.sequence > last);
        last = e.sequence;
        message(&m, 0, 0x80, (uint8_t)(i % 128), 0);
        CHECK(!mi_ack(&m, m.epoch, last));
        e = take(&m); CHECK(e.kind == MI_NOTE_OFF && e.sequence > last);
        last = e.sequence;
    }
    CHECK(!m.count && !m.overruns);
    return 0;
}
