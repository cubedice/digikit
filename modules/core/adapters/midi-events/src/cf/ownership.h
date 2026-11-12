/* Internal CF contract. No firmware addresses or DSP pointers in the ledger.
 * Caller serializes MIDI, configuration and consumer access under one critical
 * section. Runtime routing stays disabled until the V2 consumer is ready. */
#ifndef DK_MIDI_OWNERSHIP_H
#define DK_MIDI_OWNERSHIP_H
#include <stdint.h>

#define MI_PANEL 2u
#define MI_ROUTES 33u /* USB/DIN channels, then the local panel (channel zero). */
#define MI_KEYS 128u
#define MI_EVENTS 128u
#define MI_TRACKS 16u
#define MI_NONE 255u
#define MI_NOTE_ON 1u
#define MI_NOTE_OFF 2u
#define MI_CC 3u
#define MI_BEND 4u
/* Mapper-only phrase commit; never accepted from physical MIDI. */
#define MI_COMMIT 5u

typedef struct {
    uint32_t epoch, sequence, token, generation;
    uint8_t kind, port, channel, track, note, value;
} MiEvent;
typedef struct {
    uint32_t token, generation, identity;
    uint8_t used, port, channel, note, track, tombstone;
} MiKey;
typedef struct {
    uint32_t generation;
    uint8_t track, enabled, sequencer, quarantined;
} MiRoute;
typedef struct {
    MiRoute routes[MI_ROUTES];
    MiKey keys[MI_KEYS];
    MiEvent events[MI_EVENTS];
    uint32_t head, count, next_token, next_sequence;
    uint32_t reset_mask, overruns, epoch, generations[MI_TRACKS];
    /* Tracks whose instrument accepts local panel keys (route 32 mirrors the
     * lowest one). Each panel key carries its own track. */
    uint32_t panel_mask;
    /* One-shot instance slot hint (slot + 1) for the next reset of a track,
     * cleared by its ACK. Zero leaves the DSP's existing binding. */
    uint8_t reset_slot[MI_TRACKS];
} MiLedger;

/* Volatile byte copy keeps freestanding CF code independent of libc helpers. */
static inline void mi_event_copy(MiEvent *to, const MiEvent *from)
{
    volatile uint8_t *d=(volatile uint8_t *)to;
    const volatile uint8_t *s=(const volatile uint8_t *)from;
    for (uint32_t i=0;i<sizeof(*to);++i) d[i]=s[i];
}
void mi_init(MiLedger *m);
/* Disable/reset the old track before publishing a route. Returns -1 for invalid
 * input, -2 while quarantined. Caller must obtain DSP reset ACK before enabling
 * the new generation. track=MI_NONE removes a route. */
int mi_route(MiLedger *m, uint32_t port, uint32_t channel, uint32_t track,
             uint32_t generation, int sequencer);
/* Invalidate an instance, retaining FIFO tombstones. Disables its routes and
 * requests a priority reset, independent of ordinary ring capacity. */
void mi_reset_track(MiLedger *m, uint32_t track);
/* Reset a track and bind DSP instance slot `slot` (0-based) to it. */
void mi_reset_bind(MiLedger *m, uint32_t track, uint32_t slot);
int mi_reset_ack(MiLedger *m, uint32_t epoch, uint32_t track, uint32_t generation);
/* Explicit panic/recovery with physical keys released; no automatic recovery
 * from lost ownership. Reset ACK and a subsequent route call are still needed. */
void mi_recover(MiLedger *m);
/* Session boundary with keys possibly held: retain tokens/tombstones and route
 * quarantine, discard queued events, and require reset ACKs in a fresh epoch. */
int mi_session_begin(MiLedger *m);
/* Fully normalized channel messages only. 1 consumed, 0 stock fallthrough.
 * Unsupported controllers, system messages and malformed messages fall through. */
int mi_message(MiLedger *m, uint32_t port, const uint8_t *msg, uint32_t len);
/* Resolved local keys have an identity independent of pitch. Key-up releases
 * exactly the pitches captured at key-down, including across scale changes. */
int mi_panel_down(MiLedger *m, uint32_t key, uint32_t note, uint32_t velocity, uint32_t track);
void mi_panel_up(MiLedger *m, uint32_t key);
void mi_panel_cancel(MiLedger *m);
/* Sequencer trigs on a local-key track (panel_mask): one note per track, as
 * stock audio tracks are monophonic. A trig releases the track's previous
 * sequencer note first; panel cancel leaves sequencer notes sounding. */
#define MI_SEQ_KEY 0x100u
int mi_seq_trig(MiLedger *m, uint32_t track, uint32_t note, uint32_t velocity);
void mi_seq_release(MiLedger *m, uint32_t track);
/* Peek leaves the ring intact for retransmission. ACK must match its exact head
 * sequence; a duplicate or stale ACK cannot consume a different message. */
int mi_peek(MiLedger *m, MiEvent *out);
int mi_ack(MiLedger *m, uint32_t epoch, uint32_t sequence);
#endif
