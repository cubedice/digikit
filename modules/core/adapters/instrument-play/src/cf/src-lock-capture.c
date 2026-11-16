/* ISR-safe handoff from the DT2 due-event consumer to the frame sender. */
#include "src-lock-event.h"
#include "instances.h"
#include "../../../midi-events/src/cf/bridge.h"
#include "../../../midi-events/src/cf/lock-wire.h"
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern int instrument_midi_ready(uint32_t, uint32_t, uint32_t);

typedef struct {
    uint32_t serial, count;
    IlpPair pair[ILP_LANES];
} IlpPending;
static volatile IlpPending pending[16];
static uint32_t serial;
volatile uint32_t ip_lock_enabled = 1;
volatile uint32_t ip_lock_capture_error;
volatile uint32_t ip_lock_capture_debug[12];

/* Called for each due event, including one with no lock list. Never filter a
 * non-admitted track. An empty later event must supersede an earlier event
 * on the same track before the block's single frame trig is emitted. */
void ip_lock_capture(uint32_t track, IlpEvent *event)
{
    ++ip_lock_capture_debug[0];
    if (!ip_lock_enabled)
        return;
    ++ip_lock_capture_debug[1];
    if (track >= 16 || !instrument_sound_admitted(track) ||
        !instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track]))
        return;
    ++ip_lock_capture_debug[2];
    ip_lock_capture_debug[3] = track;
    ip_lock_capture_debug[4] = event ? event->count : 0;
    if (event && event->count)
        ++ip_lock_capture_debug[8];
    IlpSnapshot snapshot;
    snapshot.count = 0;
    int result = event ? ilp_filter_event(event, 1, &snapshot) : 1;
    ip_lock_capture_debug[5] = (uint32_t)result;
    if (result == 1) {
        ip_lock_capture_debug[6] = snapshot.count;
        if (snapshot.count)
            ++ip_lock_capture_debug[9];
    }
    uint32_t sr = dkrt_irq_mask();
    if (result != 1 || serial == UINT32_MAX) {
        pending[track].serial = 0;
        if (ip_lock_capture_error != UINT32_MAX)
            ++ip_lock_capture_error;
    } else {
        pending[track].serial = 0;
        pending[track].count = snapshot.count;
        for (uint32_t i = 0; i < snapshot.count; ++i) {
            pending[track].pair[i].id = snapshot.pair[i].id;
            pending[track].pair[i].value = snapshot.pair[i].value;
        }
        pending[track].serial = ++serial; /* publish last */
    }
    dkrt_irq_restore(sr);
}

/* The caller supplies the common IRQ serialization used by the MIDI ledger.
 * A snapshot can be taken once; a new unlocked trig carries an empty snapshot. */
int ip_lock_take(uint32_t track, IlpSnapshot *out)
{
    if (track >= 16 || !out || !pending[track].serial)
        return 0;
    ++ip_lock_capture_debug[7];
    uint32_t n = pending[track].count;
    if (n)
        ++ip_lock_capture_debug[10];
    if (n > ILP_LANES) {
        pending[track].serial = 0;
        return 0;
    }
    out->count = n;
    for (uint32_t i = 0; i < n; ++i) {
        out->pair[i].id = pending[track].pair[i].id;
        out->pair[i].value = pending[track].pair[i].value;
    }
    pending[track].serial = 0;
    return 1;
}

/* Generic MIDI bridge shape: each pair fits id:6/value:10. -1 means that
 * capture is disabled or no matching sequencer event was published. */
int instrument_lock_take(uint32_t track, uint16_t out[ILP_LANES])
{
    IlpSnapshot snapshot;
    if (!out || !ip_lock_take(track, &snapshot))
        return -1;
    for (uint32_t i = 0; i < snapshot.count; ++i) {
        if (snapshot.pair[i].id >= 64 || snapshot.pair[i].value >= 1024)
            return -1;
        out[i] = (uint16_t)((snapshot.pair[i].id << 10) | snapshot.pair[i].value);
    }
    return (int)snapshot.count;
}

/* A lock-only event does not necessarily set a note in this frame. Do not
 * allow such a snapshot to attach to a later unrelated note. Caller holds the
 * MIDI ledger's interrupt mask and calls this once after reading each frame. */
void instrument_lock_discard(void)
{
    for (uint32_t i = 0; i < 16; ++i)
        pending[i].serial = 0;
}

/* Frame tail immediately preceding the ordinary instrument packet. It is
 * always cleared when no snapshot is paired with the retained packet. */
void instrument_write_lock_frame(uint32_t tx, uint32_t entry)
{
    if (!tx || (tx & 3u) || entry != 0x9b4u / 2u)
        return;
    uint32_t words[ML_WORDS];
    for (uint32_t i = 0; i < ML_WORDS; ++i)
        words[i] = 0;
    if (ip_lock_enabled)
        (void)ml_encode(&mi_transport, mi_transport.words, words);
    volatile uint16_t *queue = (volatile uint16_t *)(uintptr_t)tx;
    for (uint32_t i = 0; i < ML_WORDS; ++i) {
        queue[2u * (entry + 2u * i) + 1u] = (uint16_t)words[i];
        queue[2u * (entry + 2u * i + 1u) + 1u] = (uint16_t)(words[i] >> 16);
    }
}
