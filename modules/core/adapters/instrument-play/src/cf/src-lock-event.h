/* Pure event-list filter for DT2 1.17 (finding 147). The caller must establish
 * that this event belongs to an admitted CANOPY track and serialize access. */
#ifndef DK_INSTRUMENT_SRC_LOCK_EVENT_H
#define DK_INSTRUMENT_SRC_LOCK_EVENT_H
#include <stdint.h>
#include "src-lock-packed.h"

typedef struct {
    uint16_t index;
    int16_t value;
    uint32_t rest;
} IlpEventEntry;
typedef struct {
    uint32_t prefix[2], count, opaque[2];
    IlpEventEntry entry[71];
} IlpEvent;
typedef struct {
    uint16_t id, value;
} IlpPair;
typedef struct {
    uint32_t count;
    IlpPair pair[ILP_LANES];
} IlpSnapshot;

/* For an owned event, atomically extract tagged CANOPY entries from the five
 * selected carrier indices, keeping stock entries in order. Returns 1 for a
 * valid event (including zero locks), 0 for an unowned event, or a negative
 * error for malformed or duplicate locks. Failure leaves both inputs untouched.
 * A caller MUST NOT treat tagged data on an unrelated track as CANOPY-owned. */
int ilp_filter_event(IlpEvent *event, int owned, IlpSnapshot *snapshot);
#endif
