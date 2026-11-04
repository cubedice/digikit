/* Firmware-task boundary. The pure ledger is shared with host behavior tests.
 * One short interrupt mask serializes MIDI, UI configuration and audio sender.
 * A route can only be enabled by the future V2 instance owner after readiness. */
#include "bridge.h"
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern int instrument_keyboard_events(uint32_t, uint32_t, uint32_t,
    const MiEvent *, uint32_t, MiEvent *) __attribute__((weak));
extern void dl_reset_begin(void) __attribute__((weak));
extern int dl_reset_quiesce(void) __attribute__((weak));
extern int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
    __attribute__((weak));

MiLedger mi_ledger;
MiTransport mi_transport;
static uint32_t initialized;
static uint32_t reset_seen;
static uint32_t frame_block;
static uint32_t registry_reply[MT_REPLY_WORDS];
static uint32_t registry_reply_valid;
static MtReplyGuard registry_guard;
enum { STATUS_QUEUE=64, STATUS_WORDS=12 };
static uint32_t status_words[STATUS_QUEUE][STATUS_WORDS],status_at[STATUS_QUEUE];
static uint32_t status_head,status_count,status_lost;

static void initialize(void)
{
    if (!initialized) {
        mi_init(&mi_ledger);
        mt_init(&mi_transport);
        mi_transport.epoch = mi_ledger.epoch;
        initialized = 1;
    }
}

/* Called only inside the existing critical section. Readiness providers must
 * be bounded, read-only and IRQ-safe: no I/O, allocation or UI calls. Revocation
 * retains physical-key tombstones and uses the reserved reset path. */
static void revalidate_track(uint32_t track)
{
    if (track < MI_TRACKS && (!instrument_midi_ready ||
        !mi_transport.applied[track] ||
        !instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track])))
        mi_reset_track(&mi_ledger, track);
}

static void revalidate_routes(void)
{
    uint32_t i, seen = 0;
    for (i = 0; i < MI_ROUTES; ++i) {
        MiRoute *route = &mi_ledger.routes[i];
        if (route->enabled && route->track < MI_TRACKS &&
            !(seen & (1u << route->track))) {
            seen |= 1u << route->track;
            revalidate_track(route->track);
        }
    }
}

void instrument_write_frame(uintptr_t tx, uint32_t entry, const volatile uint16_t *record)
{
    uint32_t reply[MT_REPLY_WORDS], i, sr;
    const uint32_t *packet, *observed;
    volatile uint16_t *queue = (volatile uint16_t *)tx;
    if (!tx || (tx & 3u) || entry != 0x9fcu/2u) return;
    sr = dkrt_irq_mask();
    initialize();
    if (reset_seen) {
        for (i = 0; i < MT_WORDS; ++i) {
            queue[2u*(entry+2u*i)+1u] = 0;
            queue[2u*(entry+2u*i+1u)+1u] = 0;
        }
        dkrt_irq_restore(sr);
        return;
    }
    mt_reply_read(record, reply);
    observed = mt_reply_reordered(&registry_guard, reply) ? 0 : reply;
    mt_reply_guard_tick(&registry_guard, observed, frame_block);
    /* A valid UI frame does not replace the last ordinary ACK. Otherwise a
     * UI task polling on the status half of the cadence could never see ACKs.
     * Preserve its original heartbeat/checksum; never synthesize progress.
     * Missing, reordered or invalid traffic retains the existing invalidation
     * behavior, and the bound-session guard remains authoritative. */
    if (registry_guard.invalid || !mt_reply_valid(observed) ||
        (registry_guard.expected && !registry_guard.ready)) registry_reply_valid=0;
    else if (reply[0]!=MT_STATUS_REPLY_MAGIC) {
        registry_reply_valid=reply[4]==0x49525231u;
        if (registry_reply_valid)
            for (i = 0; i < MT_REPLY_WORDS; ++i) registry_reply[i] = reply[i];
    }
    int progress=mt_reply_tick_guarded(&mi_transport, &mi_ledger, record ? observed : 0, frame_block, &registry_guard);
    if(registry_guard.invalid || mi_transport.faults) status_head=status_count=status_lost=0;
    else if(progress>0 && observed && observed[0]==MT_STATUS_REPLY_MAGIC && registry_guard.ready) {
        if(status_count==STATUS_QUEUE) {
            /* Discard the interrupted transfer, not arbitrary fragment bytes.
             * Report loss once before exposing any newer message. */
            status_head=status_count=0; status_lost=1;
        }
        uint32_t slot=(status_head+status_count)&(STATUS_QUEUE-1);
        for(i=0;i<STATUS_WORDS;++i) status_words[slot][i]=observed[3+i];
        status_at[slot]=frame_block; ++status_count;
    }
    revalidate_routes();
    packet = registry_guard.expected && (!registry_guard.ready || registry_guard.invalid) ? 0 :
        mt_prepare_mapped(&mi_transport, &mi_ledger, mi_transport.applied, frame_block, instrument_keyboard_events);
    ++frame_block;
    /* Queue entries contain command/data halfwords. Only data halves belong to
     * this callback. Write zeros when idle so old accepted packets do not recur. */
    for (i = 0; i < MT_WORDS; ++i) {
        uint32_t word = packet ? packet[i] : 0;
        queue[2u*(entry+2u*i)+1u] = (uint16_t)word;
        queue[2u*(entry+2u*i+1u)+1u] = (uint16_t)(word >> 16);
    }
    dkrt_irq_restore(sr);
}

/* Reset hook boundary: expansion data/BSS must already be initialized.
 * No UI coordinator access, allocation or transport writes. The initial stock
 * assertion before any adapter use is harmless. Once used, reset is terminal
 * for this CF boot; neither release nor MIDI recovery may resume old traffic. */
void mi_dsp_reset_begin(void)
{
    uint32_t sr = dkrt_irq_mask();
    if (dl_reset_begin) dl_reset_begin();
    if (dl_reset_quiesce) (void)dl_reset_quiesce();
    if (initialized && !reset_seen) {
        reset_seen = 1;
        registry_reply_valid = 0;
        status_head=status_count=status_lost=0;
        registry_guard.invalid = 1;
        mi_recover(&mi_ledger);
        mt_init(&mi_transport);
        mi_transport.epoch = mi_ledger.epoch;
        mi_transport.faults = 1;
    }
    dkrt_irq_restore(sr);
}

/* No retry after a timeout: stale FIFO/DMA work must not cross reset release. */
int mi_dsp_reset_release(void)
{
    return !dl_reset_quiesce || dl_reset_quiesce() != 2;
}

/* UI-task snapshot, copied under the same lock as the frame publisher. No record
 * pointer escapes the ISR. Identity/freshness remain the loader's responsibility. */
int mi_registry_reply(uint32_t *out, uint32_t words)
{
    uint32_t sr, i;
    int result = 0;
    if (!out || words < MT_REPLY_WORDS) return -1;
    sr = dkrt_irq_mask();
    if (registry_guard.invalid) result = -2;
    else if (registry_reply_valid) {
        for (i = 0; i < MT_REPLY_WORDS; ++i) out[i] = registry_reply[i];
        result = MT_REPLY_WORDS;
    }
    dkrt_irq_restore(sr);
    return result;
}

int mi_status_reply(uint32_t *out,uint32_t words,uint32_t *age_blocks)
{
    if(!out || words<STATUS_WORDS || !age_blocks) return -1;
    uint32_t sr=dkrt_irq_mask(); int result=0;
    if(reset_seen || registry_guard.invalid) result=-2;
    else if(status_lost) { status_lost=0; result=-3; }
    else if(status_count) {
        for(uint32_t i=0;i<STATUS_WORDS;++i) out[i]=status_words[status_head][i];
        *age_blocks=frame_block-status_at[status_head];
        status_head=(status_head+1)&(STATUS_QUEUE-1); --status_count; result=STATUS_WORDS;
    }
    dkrt_irq_restore(sr); return result;
}

/* UI-task preparation only; does not send a bind or advertise readiness. */
int mi_registry_expect_session(uint32_t token, uint32_t sequence)
{
    uint32_t sr = dkrt_irq_mask();
    int result;
    initialize();
    result = mt_reply_guard_expect(&registry_guard, token, sequence, frame_block);
    if (!result) {
        registry_reply_valid = 0;
        status_head=status_count=status_lost=0;
        /* Never carry legacy MIDI ACK/configuration identity into the new
         * session. Retain physical-key tombstones through the epoch change. */
        if (mi_session_begin(&mi_ledger)) {
            registry_guard.invalid = 1;
            result = -2;
        }
        mt_init(&mi_transport);
        mi_transport.epoch = mi_ledger.epoch;
    }
    dkrt_irq_restore(sr);
    return result;
}

int mi_config_submit(uint32_t track, uint32_t revision, uint32_t schema,
                     const uint32_t *values, uint32_t count)
{
    uint32_t sr = dkrt_irq_mask();
    int result;
    initialize();
    result = reset_seen || (registry_guard.expected && (!registry_guard.ready || registry_guard.invalid)) ||
        !mi_transport.heartbeat_seen || mi_transport.faults ? -2 :
        mt_config_begin(&mi_transport, &mi_ledger, track, revision, schema, values, count);
    dkrt_irq_restore(sr);
    return result;
}

void mi_transport_recover(void)
{
    uint32_t sr = dkrt_irq_mask();
    initialize();
    if (reset_seen) { dkrt_irq_restore(sr); return; }
    registry_reply_valid = 0;
    registry_guard.invalid = 1; /* recovery cannot establish a new DSP boot */
    mi_recover(&mi_ledger);
    mt_init(&mi_transport);
    mi_transport.epoch = mi_ledger.epoch;
    dkrt_irq_restore(sr);
}

int mi_keyboard_recover(void)
{
    uint32_t sr=dkrt_irq_mask();
    initialize();
    if (reset_seen || !registry_guard.ready || registry_guard.invalid || mi_ledger.epoch==UINT32_MAX) {
        dkrt_irq_restore(sr); return -1;
    }
    mi_recover(&mi_ledger);
    mt_init(&mi_transport); mi_transport.epoch=mi_ledger.epoch;
    dkrt_irq_restore(sr); return 0;
}

int mi_route_enable(uint32_t port, uint32_t channel, uint32_t track, int sequencer)
{
    uint32_t sr;
    int result = -2;
    if (port >= 2 || channel >= 16 || track >= MI_TRACKS || !instrument_midi_ready) return -1;
    sr = dkrt_irq_mask();
    initialize();
    if (!reset_seen && (!registry_guard.expected || (registry_guard.ready && !registry_guard.invalid)) &&
        mi_transport.heartbeat_seen && !mi_transport.faults && mi_transport.applied[track] &&
        instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track]))
        result = mi_route(&mi_ledger, port, channel, track,
                          mi_ledger.generations[track], sequencer);
    dkrt_irq_restore(sr);
    return result;
}

void mi_instance_reset(uint32_t track)
{
    uint32_t sr = dkrt_irq_mask();
    initialize();
    mi_reset_track(&mi_ledger, track);
    dkrt_irq_restore(sr);
}

int mi_channel_message(const uint8_t *msg, uint32_t len, uint32_t source)
{
    uint32_t sr, port;
    int consumed;
    if (!initialized || reset_seen || len != 3 || !msg) return 0;
    if (source == 2 || source == 4) port = 0;      /* USB cable 0 */
    else if (source == 8 || source == 16) port = 1; /* DIN */
    else return 0;
    sr = dkrt_irq_mask();
    if (msg[0] >= 0x80u && msg[0] < 0xf0u) {
        MiRoute *route = &mi_ledger.routes[port * 16u + (msg[0] & 15u)];
        if (route->enabled) revalidate_track(route->track);
    }
    consumed = mi_message(&mi_ledger, port, msg, len);
    dkrt_irq_restore(sr);
    return consumed;
}
