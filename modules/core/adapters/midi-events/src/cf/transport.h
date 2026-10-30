/* Internal DTII instrument stream, version 1. Serialized by the caller with the
 * MIDI ledger. This sender is not connected to the ISR until a V2 receiver exists. */
#ifndef DK_MIDI_TRANSPORT_H
#define DK_MIDI_TRANSPORT_H
#include "ownership.h"
#define MT_WORDS 32u
#define MT_MAGIC 0x494e0101u
#define MT_EVENTS 1u
#define MT_RESET 2u
#define MT_CONFIG_CHUNK 3u
#define MT_CONFIG_COMMIT 4u
#define MT_CONFIG_WORDS 64u
#define MT_CHUNK_WORDS 16u
#define MT_STALL_BLOCKS 900u
#define MT_REPLY_WORDS 16u
#define MT_REPLY_MAGIC 0x494ea101u
typedef struct {
    uint32_t words[MT_WORDS];
    uint32_t epoch, next_message, pending, track, generation, count;
    uint32_t sent_block, faults;
    uint32_t config[MT_CONFIG_WORDS];
    uint32_t config_active, config_track, config_generation, config_revision;
    uint32_t config_schema, config_words, config_chunk, config_check;
    uint32_t applied[MI_TRACKS];
    uint32_t heartbeat_seen, heartbeat, heartbeat_block;
} MiTransport;
void mt_init(MiTransport *t);
/* Returns a retained immutable packet, or null when idle/faulted. Revision zero
 * means the consumer has not committed a configuration; notes wait in the ring.
 * Word 6 is first-send block until ingress timestamping is implemented. */
const uint32_t *mt_prepare(MiTransport *t, MiLedger *m,
                           const uint32_t revisions[MI_TRACKS], uint32_t block);
/* Caller validates reply framing/integrity first. Only exact pending identity
 * can retire events; duplicate and delayed ACKs have no effect. */
int mt_ack(MiTransport *t, MiLedger *m, uint32_t epoch, uint32_t message);
uint32_t mt_check(const uint32_t words[MT_WORDS]);
/* One immutable, bounded transaction at a time. Returns 0 accepted, -1 invalid,
 * -2 busy/reset/fault. Caller uses applied[] as the committed revision snapshot
 * for mt_prepare; no revision is published by a chunk ACK. */
int mt_config_begin(MiTransport *t, const MiLedger *m, uint32_t track,
                    uint32_t revision, uint32_t schema,
                    const uint32_t *words, uint32_t count);
/* Bootstrap ACK-only reply: magic, epoch, accepted message, heartbeat,
 * eleven reserved zero words, checksum. Final UI status needs another version.
 * Call once per CF block, including with null/malformed replies. Returns -1
 * invalid, 0 repeated/missing, 1 fresh, 2 fresh and pending packet acknowledged.
 * Only an epoch-bound, forward heartbeat may acknowledge a packet. */
int mt_reply_tick(MiTransport *t, MiLedger *m,
                  const uint32_t reply[MT_REPLY_WORDS], uint32_t block);
void mt_reply_read(const volatile uint16_t *record, uint32_t out[MT_REPLY_WORDS]);
#endif
