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
/* Mapped batch: word 4 is phrase identity, high bit marks generated chords. */
#define MT_MAPPED 5u
#define MT_CONFIG_WORDS 64u
#define MT_CHUNK_WORDS 16u
#define MT_STALL_BLOCKS 900u
#define MT_REPLY_WORDS 16u
#define MT_REPLY_MAGIC 0x494ea101u
#define MT_SESSION_REPLY_MAGIC 0x494ea102u
typedef struct {
    uint32_t words[MT_WORDS];
    uint32_t epoch, next_message, pending, track, generation, count;
    uint32_t sent_block, faults, source_sequence;
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
/* Optional musical mapping. One bounded note batch or one controller per call.
 * Retained packets are never transformed twice. Zero output retires silent
 * input locally; negative output revokes the track before any output is sent. */
typedef int (*MtTransform)(uint32_t track, uint32_t epoch, uint32_t generation,
                          const MiEvent *input, uint32_t count, MiEvent output[8]);
const uint32_t *mt_prepare_mapped(MiTransport *t, MiLedger *m,
    const uint32_t revisions[MI_TRACKS], uint32_t block, MtTransform transform);
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
/* Reply: magic, epoch, accepted message, heartbeat, optional registry block
 * in words 4..11, session token at word 12 in version 2 (zero in version 1),
 * row phase/generation in words 13..14 for version 3 (zero in versions 1/2),
 * checksum.
 * Call once per CF block, including with null/malformed replies. Returns -1
 * invalid, 0 repeated/missing, 1 fresh, 2 fresh and pending packet acknowledged.
 * Only an epoch-bound, forward heartbeat may acknowledge a packet. */
/* Framing/integrity only; independent of MIDI epoch and registry identity. */
int mt_reply_valid(const uint32_t reply[MT_REPLY_WORDS]);
int mt_reply_tick(MiTransport *t, MiLedger *m,
                  const uint32_t reply[MT_REPLY_WORDS], uint32_t block);
void mt_reply_read(const volatile uint16_t *record, uint32_t out[MT_REPLY_WORDS]);

/* Separate from MIDI epochs: registry bootstrap precedes MIDI admission.
 * Zero-initialize once per CF boot. invalid is sticky until a real cold boot. */
typedef struct {
    uint32_t seen, heartbeat, block, invalid;
    uint32_t expected, sequence, started, ready;
    uint32_t history[8][MT_REPLY_WORDS], history_count, history_next;
} MtReplyGuard;
/* Arm exactly once before sending BIND_SESSION. 0 accepted, -1 bad arguments,
 * -2 already armed or invalid. No reset/retry; readiness requires the exact bind
 * ACK plus version/token. Deadline uses CF frame blocks, not a UI wall clock. */
int mt_reply_guard_expect(MtReplyGuard *guard, uint32_t token,
                          uint32_t sequence, uint32_t block);
/* Discard only exact copies of the last eight accepted bound replies.
 * Treat it as missing everywhere: no ACK, no cached registry reply, no deadline
 * refresh. Unknown regressions and changed/missing bound sessions still fault. */
int mt_reply_reordered(const MtReplyGuard *guard, const uint32_t *reply);
void mt_reply_guard_tick(MtReplyGuard *guard, const uint32_t *reply, uint32_t block);
/* Call after guard_tick on the same snapshot/block. An armed session must bind
 * before ACKs are consumed; sticky trust loss faults MIDI ownership immediately. */
int mt_reply_tick_guarded(MiTransport *t, MiLedger *m, const uint32_t *reply,
                          uint32_t block, const MtReplyGuard *guard);

/* V3 adds admitted row zero phase/generation in reply words 13/14. */
#define MT_PLAY_REPLY_MAGIC 0x494ea103u

#endif
