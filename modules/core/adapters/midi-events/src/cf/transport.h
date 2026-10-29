/* Internal DTII instrument stream, version 1. Serialized by the caller with the
 * MIDI ledger. This sender is not connected to the ISR until a V2 receiver exists. */
#ifndef DK_MIDI_TRANSPORT_H
#define DK_MIDI_TRANSPORT_H
#include "ownership.h"
#define MT_WORDS 32u
#define MT_MAGIC 0x494e0101u
#define MT_EVENTS 1u
#define MT_RESET 2u
#define MT_STALL_BLOCKS 900u
typedef struct {
    uint32_t words[MT_WORDS];
    uint32_t epoch, next_message, pending, track, generation, count;
    uint32_t sent_block, faults;
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
#endif
