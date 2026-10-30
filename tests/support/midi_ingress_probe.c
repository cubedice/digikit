/* Instruction-core boundary proof. Readiness/ACKs are injected, not a V2 API. */
#include "bridge.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
extern MiLedger mi_ledger;
extern int mi_route_enable(uint32_t, uint32_t, uint32_t, int);
extern void mi_instance_reset(uint32_t);
extern void mi_probe_hook(const uint8_t *, uint32_t, uint32_t, uint32_t);
uint32_t mi_probe_regs[6], mi_probe_args[3];
uint32_t mi_probe_sp, mi_probe_return_sp, mi_probe_stock;
static uint32_t uploads;

int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{ return track < 16 && epoch == 1 && generation == 1; }
int dkrt_midi_rx(const uint8_t *msg, uint32_t len, uint32_t source)
{
    (void)source;
    ++uploads;
    return len >= 4 && msg[0] == 0xf0 && msg[1] == 0x7d && msg[2] == 0x64 && msg[3] == 0x6b;
}
void *memset(void *dst, int c, unsigned n)
{ volatile uint8_t *p=dst; while(n--) *p++=(uint8_t)c; return dst; }
void *memcpy(void *dst, const void *src, unsigned n)
{ volatile uint8_t *p=dst; const volatile uint8_t *q=src; while(n--) *p++=*q++; return dst; }

static uint32_t invoke(uint8_t *msg, uint32_t len, uint32_t source, uint32_t stock)
{
    uint32_t din = source == 8 || source == 16;
    mi_probe_hook(msg, len, source, din);
    CHECK(mi_probe_stock == stock);
    CHECK(mi_probe_sp == mi_probe_return_sp);
    CHECK(mi_probe_regs[0] == 0x12345678 && mi_probe_regs[1] == 0x23456789);
    CHECK(mi_probe_regs[2] == din && mi_probe_regs[3] == 0x3456789a);
    CHECK(mi_probe_regs[4] == 0x456789ab && mi_probe_regs[5] == 0x56789abc);
    CHECK(mi_probe_args[0] == (uintptr_t)msg && mi_probe_args[1] == len);
    CHECK(mi_probe_args[2] == source);
    return 0;
}

uint32_t mi_probe_ingress(uint32_t source)
{
    uint32_t port = source == 8 || source == 16, r;
    uint8_t note[3] = {0x90,60,90}, off[3] = {0x80,60,0};
    uint8_t other[3] = {0x91,60,90}, clock[1] = {0xf8};
    uint8_t sysex[5] = {0xf0,0x7d,0x64,0x6b,0xf7};
    MiEvent e;
    /* Initialize the adapter, then start each source with a fresh proof ledger. */
    (void)mi_route_enable(port,0,0,0);
    mi_init(&mi_ledger); mt_init(&mi_transport);
    mi_transport.epoch = mi_ledger.epoch;
    r=invoke(note,3,source,1); CHECK(!r);
    CHECK(mi_ledger.count == 0);
    mi_transport.heartbeat_seen=1; mi_transport.applied[0]=1;
    CHECK(mi_route_enable(port,0,0,0) == 0);
    r=invoke(note,3,source,0); CHECK(!r);
    CHECK(mi_peek(&mi_ledger,&e) && e.kind == MI_NOTE_ON && e.port == port);
    CHECK(mi_ack(&mi_ledger,e.epoch,e.sequence));
    r=invoke(other,3,source,1); CHECK(!r);
    CHECK(mi_ledger.count == 0);
    r=invoke(off,3,source,0); CHECK(!r);
    CHECK(mi_peek(&mi_ledger,&e) && e.kind == MI_NOTE_OFF);
    CHECK(mi_ack(&mi_ledger,e.epoch,e.sequence));
    r=invoke(note,3,source,0); CHECK(!r);
    CHECK(mi_peek(&mi_ledger,&e) && e.kind == MI_NOTE_ON);
    CHECK(mi_ack(&mi_ledger,e.epoch,e.sequence));
    note[2]=0; /* A velocity-zero NOTE ON is a release, also consumed. */
    r=invoke(note,3,source,0); CHECK(!r);
    CHECK(mi_peek(&mi_ledger,&e) && e.kind == MI_NOTE_OFF);
    CHECK(mi_ack(&mi_ledger,e.epoch,e.sequence));
    note[2]=90;
    /* Reset retains release pairing and consumes fresh presses while disabled. */
    r=invoke(note,3,source,0); CHECK(!r);
    mi_instance_reset(0);
    r=invoke(off,3,source,0); CHECK(!r);
    r=invoke(note,3,source,0); CHECK(!r);
    r=invoke(clock,1,source,1); CHECK(!r);
    uploads=0;
    r=invoke(sysex,5,source,port ? 1 : 0); CHECK(!r);
    CHECK(uploads == (port ? 0u : 1u));
    return 0;
}
