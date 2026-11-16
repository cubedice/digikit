/* Paired, checksummed sideband for a pending sequencer-note packet. */
#ifndef DK_MIDI_LOCK_WIRE_H
#define DK_MIDI_LOCK_WIRE_H
#include "transport.h"
enum { ML_WORDS = 18 };
#define ML_MAGIC 0x434c4b31u
/* Both routines leave outputs unchanged on failure. `packet` is the exact
 * retained note packet, so a late or unrelated sideband cannot be applied. */
int ml_encode(const MiTransport *transport, const uint32_t *packet, uint32_t out[ML_WORDS]);
int ml_decode(const uint32_t side[ML_WORDS], const uint32_t *packet, uint16_t locks[MI_LOCKS],
              uint32_t *count, uint16_t source[MI_SOURCE_VALUES], uint32_t *source_valid);
#endif
