#include "ownership.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
extern MiLedger mi_ledger;
extern int mi_route_enable(uint32_t, uint32_t, uint32_t, int);
extern void mi_instance_reset(uint32_t);
extern int dkrt_midi_dispatch(const uint8_t *, uint32_t, uint32_t);
static uint32_t ready, upload_calls, masks, restores;
uint32_t dkrt_irq_mask(void) { ++masks; return 0x2300; }
void dkrt_irq_restore(uint32_t sr) { if (sr == 0x2300) ++restores; }
int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{ return ready && track < 16 && epoch == 1 && generation == 1; }
int dkrt_midi_rx(const uint8_t *msg, uint32_t len, uint32_t source)
{
    (void)source;
    ++upload_calls;
    return len >= 4 && msg[0] == 0xf0 && msg[1] == 0x7d && msg[2] == 0x64 && msg[3] == 0x6b;
}
uint32_t mi_test_dispatch(void)
{
    uint8_t note[3] = {0x90, 60, 90};
    uint8_t off[3] = {0x80, 60, 0};
    uint8_t sysex[8] = {0xf0, 0x7d, 0x64, 0x6b, 1, 0, 0, 0xf7};
    uint8_t clock = 0xf8;
    MiEvent e;
    CHECK(!dkrt_midi_dispatch(note, 3, 2));
    CHECK(mi_route_enable(0, 0, 0, 0) == -2);
    CHECK(!dkrt_midi_dispatch(note, 3, 2));
    ready = 1;
    CHECK(mi_route_enable(0, 0, 0, 0) == 0);
    CHECK(mi_route_enable(1, 0, 15, 0) == 0);
    upload_calls = 0;
    CHECK(dkrt_midi_dispatch(note, 3, 2));
    CHECK(dkrt_midi_dispatch(note, 3, 16));
    CHECK(!upload_calls);
    CHECK(mi_peek(&mi_ledger, &e) && e.track == 0 && e.port == 0);
    CHECK(mi_ack(&mi_ledger, e.epoch, e.sequence));
    CHECK(mi_peek(&mi_ledger, &e) && e.track == 15 && e.port == 1);
    CHECK(mi_ack(&mi_ledger, e.epoch, e.sequence));
    CHECK(dkrt_midi_dispatch(off, 3, 4));
    CHECK(dkrt_midi_dispatch(off, 3, 8));
    CHECK(!upload_calls);
    CHECK(dkrt_midi_dispatch(sysex, sizeof(sysex), 2));
    CHECK(!dkrt_midi_dispatch(&clock, 1, 2));
    CHECK(!dkrt_midi_dispatch(note, 3, 32));
    CHECK(upload_calls == 3);
    mi_instance_reset(0);
    CHECK(mi_ledger.reset_mask == 1 && masks == restores);
    return 0;
}
