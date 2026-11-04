#include "bridge.h"
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
static void publish_reply(uint16_t *queue, uint16_t *record, uint32_t *r, int corrupt)
{
    uint32_t i, a = 0, b = 0;
    for (i = 0; i < 15; ++i) { a += r[i]; b += a; }
    r[15] = (a ^ b) ^ (uint32_t)corrupt;
    for (i = 0; i < MT_REPLY_WORDS; ++i) {
        uint32_t h = (0x1cu + 0x54u*(21u+i/2u)+0x44u+(i&1u)*4u)/2u;
        record[h] = (uint16_t)r[i]; record[h+1] = (uint16_t)(r[i]>>16);
    }
    instrument_write_frame((uintptr_t)queue, 0x9fc/2, record);
}
uint32_t mi_test_dispatch(void)
{
    uint8_t note[3] = {0x90, 60, 90};
    uint8_t off[3] = {0x80, 60, 0};
    uint8_t sysex[8] = {0xf0, 0x7d, 0x64, 0x6b, 1, 0, 0, 0xf7};
    uint8_t clock = 0xf8;
    MiEvent e;
    uint16_t queue[0x55e*2], record[0xabc/2];
    uint32_t snapshot[MT_REPLY_WORDS+2];
    uint32_t i, seq, r[MT_REPLY_WORDS], a = 0, b = 0;
    CHECK(!dkrt_midi_dispatch(note, 3, 2));
    CHECK(mi_route_enable(0, 0, 0, 0) == -2);
    CHECK(mi_keyboard_recover() == -1); /* no trusted bound session */
    CHECK(!dkrt_midi_dispatch(note, 3, 2));
    ready = 1;
    CHECK(mi_route_enable(0, 0, 0, 0) == -2);
    mi_transport.heartbeat_seen = 1;
    mi_transport.applied[0] = mi_transport.applied[15] = 1;
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
    /* An existing route must lose admission before new input, even if the
     * owning module did not explicitly disable it. Keep old-key pairing. */
    CHECK(dkrt_midi_dispatch(note, 3, 2));
    ready = 0;
    CHECK(dkrt_midi_dispatch(off, 3, 2));
    CHECK(mi_ledger.reset_mask == 1 && !mi_ledger.routes[0].enabled);
    CHECK(mi_ledger.routes[16].enabled); /* unrelated DIN track not touched */
    CHECK(mi_ledger.generations[0] == 2);
    CHECK(dkrt_midi_dispatch(note, 3, 2)); /* held while disabled: tombstone */
    CHECK(dkrt_midi_dispatch(off, 3, 2));
    CHECK(mi_ledger.generations[0] == 2); /* no repeated generation churn */
    /* Frame-time revocation catches a quiet keyboard and queued old events. */
    for (i = 0; i < 0x55e*2; ++i) queue[i] = 0;
    instrument_write_frame((uintptr_t)queue, 0x9fc/2, 0);
    CHECK(mi_ledger.reset_mask == ((1u << 15) | 1u));
    CHECK(!mi_ledger.routes[16].enabled);
    CHECK(mi_transport.pending == MT_RESET);
    CHECK(masks == restores);
    ready = 1;
    /* Exercise the actual frame callback after the dispatch case, in this same
     * test to avoid concurrent host tests mutating the adapter's globals. */
    mi_init(&mi_ledger); mt_init(&mi_transport);
    mi_transport.epoch = mi_ledger.epoch;
    mi_instance_reset(0);
    for (i = 0; i < 0x55e*2; ++i) queue[i] = 0xcccc;
    instrument_write_frame((uintptr_t)queue, 1, 0);
    for (i = 0; i < 0x55e*2; ++i) CHECK(queue[i] == 0xcccc);
    instrument_write_frame((uintptr_t)queue, 0x9fc/2, 0);
    CHECK(mi_transport.pending == MT_RESET);
    for (i = 0; i < 0x55e*2; ++i) {
        uint32_t entry = i/2;
        if (!(i & 1) || entry < 0x9fc/2 || entry >= 0xa7c/2) CHECK(queue[i] == 0xcccc);
    }
    for (i = 0; i < MT_WORDS; ++i) {
        uint32_t at = 2u*(0x9fc/2+2*i)+1;
        CHECK(((uint32_t)queue[at] | ((uint32_t)queue[at+2]<<16)) == mi_transport.words[i]);
    }
    seq = mi_transport.words[3];
    for (i = 0; i < MT_REPLY_WORDS; ++i) r[i] = 0;
    r[0] = MT_REPLY_MAGIC; r[1] = 1; r[2] = seq; r[3] = 1;
    for (i = 0; i < 15; ++i) { a += r[i]; b += a; }
    r[15] = a ^ b;
    for (i = 0; i < 0xabc/2; ++i) record[i] = 0;
    for (i = 0; i < MT_REPLY_WORDS; ++i) {
        uint32_t h = (0x1cu + 0x54u*(21u+i/2u)+0x44u+(i&1u)*4u)/2u;
        record[h] = (uint16_t)r[i]; record[h+1] = (uint16_t)(r[i]>>16);
    }
    instrument_write_frame((uintptr_t)queue, 0x9fc/2, record);
    CHECK(!mi_ledger.reset_mask && !mi_transport.pending && mi_transport.heartbeat_seen);
    for (i = 0; i < MT_WORDS; ++i) {
        uint32_t at = 2u*(0x9fc/2+2*i)+1;
        CHECK(!queue[at] && !queue[at+2]);
    }
    CHECK(mi_route_enable(0, 0, 0, 0) == -2); /* reset ACK alone is not configured */
    for (i = 0; i < MT_REPLY_WORDS+2; ++i) snapshot[i] = 0xdeadbeef;
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == 0);
    r[1] = 99; /* Registry bootstrap is independent of MIDI epoch admission. */
    r[3] = 2; r[4] = 0x49525231u; r[5] = 17; r[6] = 2; r[7] = 4;
    r[8] = 4; r[9] = 0; r[10] = 0; r[11] = 7;
    publish_reply(queue, record, r, 0);
    CHECK(mi_registry_reply(0, MT_REPLY_WORDS) == -1);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS-1) == -1);
    for (i = 0; i < MT_REPLY_WORDS+2; ++i) CHECK(snapshot[i] == 0xdeadbeef);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS+2) == MT_REPLY_WORDS);
    for (i = 0; i < MT_REPLY_WORDS; ++i) CHECK(snapshot[i] == r[i]);
    CHECK(snapshot[16] == 0xdeadbeef && snapshot[17] == 0xdeadbeef);
    CHECK(mi_route_enable(0, 0, 0, 0) == -2);
    publish_reply(queue, record, r, 1);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == 0);
    for (i = 0; i < 15; ++i) CHECK(snapshot[i] == r[i]);
    CHECK(snapshot[15] == (r[15] ^ 1));
    publish_reply(queue, record, r, 0);
    CHECK(mi_registry_expect_session(0, 18) == -1);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == MT_REPLY_WORDS);
    CHECK(mi_registry_expect_session(0xdeadbeefu, 18) == 0);
    CHECK(mi_ledger.epoch == 2 && !mi_transport.heartbeat_seen);
    CHECK(mi_route_enable(0, 0, 0, 0) == -2);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == 0);
    CHECK(mi_registry_expect_session(0xdeadbeefu, 18) == -2);
    r[3] = 3; publish_reply(queue, record, r, 0);
    for (i = 0; i < MT_WORDS; ++i) {
        uint32_t at = 2u*(0x9fc/2+2*i)+1;
        CHECK(!queue[at] && !queue[at+2]);
    }
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == 0); /* still old version */
    r[0] = MT_SESSION_REPLY_MAGIC; r[3] = 4; r[5] = 18;
    r[6] = 0; r[7] = 0xdeadbeefu; r[8] = 5; r[11] = 0; r[12] = 0xdeadbeefu;
    publish_reply(queue, record, r, 0);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == MT_REPLY_WORDS);
    CHECK(snapshot[0] == MT_SESSION_REPLY_MAGIC && snapshot[12] == 0xdeadbeefu);
    /* Only an exact previously accepted reply is a known stale copy. */
    r[3] = 7; publish_reply(queue, record, r, 0);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == MT_REPLY_WORDS);
    r[3] = 4; publish_reply(queue, record, r, 0);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == 0 && snapshot[3] == 7);
    r[3] = 8; publish_reply(queue, record, r, 0);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == MT_REPLY_WORDS && snapshot[3] == 8);
    /* Explicit keys-up recovery advances MIDI epoch without revoking a healthy
     * DSP bind. It cannot be used after the trust failure below. */
    mi_ledger.routes[0].quarantined=1;
    CHECK(mi_keyboard_recover() == 0);
    CHECK(mi_ledger.epoch == 3 && !mi_ledger.routes[0].quarantined);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == MT_REPLY_WORDS);
    {
        uint32_t legacy[16],ui[14],age=0xdeadbeef;
        for(i=0;i<16;++i) legacy[i]=r[i];
        for(i=0;i<14;++i) ui[i]=0xdeadbeef;
        r[0]=MT_STATUS_REPLY_MAGIC; r[1]=0xdeadbeef; r[2]=9;
        for(i=3;i<15;++i) r[i]=100+i;
        r[4]=0x49525231; /* Payload resemblance cannot create a registry ACK. */
        publish_reply(queue,record,r,0);
        CHECK(mi_registry_reply(snapshot,16)==16);
        /* A status payload must neither replace nor refresh the ordinary ACK. */
        for(i=0;i<16;++i) CHECK(snapshot[i]==legacy[i]);
        CHECK(mi_status_reply(ui,11,&age)==-1 && age==0xdeadbeef);
        CHECK(mi_status_reply(ui,14,&age)==12 && age==1);
        for(i=0;i<12;++i) CHECK(ui[i]==r[3+i]);
        CHECK(ui[12]==0xdeadbeef && ui[13]==0xdeadbeef);
        CHECK(mi_status_reply(ui,14,&age)==0);
        /* Identical wire heartbeat is not published twice. */
        publish_reply(queue,record,r,0); CHECK(mi_status_reply(ui,14,&age)==0);
        for(uint32_t h=10;h<75;++h) { r[2]=h; publish_reply(queue,record,r,0); }
        CHECK(mi_status_reply(ui,14,&age)==-3);
        CHECK(mi_status_reply(ui,14,&age)==12 && age==1);
        CHECK(mi_status_reply(ui,14,&age)==0);
        r[2]=75; publish_reply(queue,record,r,0); /* queued at trust loss below */
        for(i=0;i<16;++i) r[i]=legacy[i];
    }
    /* A backwards, otherwise valid heartbeat invalidates registry trust even
     * when its MIDI epoch is not admitted. The caller buffer stays unchanged. */
    r[3] = 1;
    publish_reply(queue, record, r, 0);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == -2);
    { uint32_t age=0; CHECK(mi_status_reply(snapshot,16,&age)==-2 && !age); }
    CHECK(snapshot[3] == 8);
    CHECK(mi_keyboard_recover() == -1);
    CHECK(mi_transport.faults && mi_route_enable(0, 0, 0, 0) == -2);
    for (i = 0; i < MT_WORDS; ++i) {
        uint32_t at = 2u*(0x9fc/2+2*i)+1;
        CHECK(!queue[at] && !queue[at+2]);
    }
    mi_transport_recover();
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == -2);
    publish_reply(queue, record, r, 0);
    instrument_write_frame((uintptr_t)queue, 0x9fc/2, 0);
    CHECK(mi_registry_reply(snapshot, MT_REPLY_WORDS) == -2);
    CHECK(masks == restores);
    return 0;
}
