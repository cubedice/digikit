/* Unlinked host experiment: never install on hardware. */
#ifndef CANOPY_RAW_STORE_H
#define CANOPY_RAW_STORE_H
#include <stdint.h>
#include "../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"

enum { CRS_RECORD_BYTES = 0x455, CRS_PROJECT_LIMIT = 2192 };
typedef enum {
    CRS_EMPTY = 0,
    CRS_CURRENT = 1,
    CRS_UNKNOWN_VERSION = 2,
    CRS_CORRUPT = 3,
    CRS_FOREIGN = 4
} CrsKind;
typedef struct {
    SpPool *pool;
    uint32_t next, end;
} CrsBatch;

/* One serialized task must own all pool/handle access, including copies and
 * edit publication. Begin must succeed before stock may mutate a destination;
 * no firmware caller currently enforces that contract. Initialize batch to 0. */
int crs_begin(CrsBatch *batch, SpPool *pool, uint32_t max_records);
/* After a successful stock deserialization, attach an immutable opaque record.
 * Publishes without allocating. Failure changes neither handle nor kind. */
int crs_attach(CrsBatch *batch, const uint8_t *record, uint32_t length, uint16_t *handle,
               CrsKind *kind);
/* Return unused tail reservations only if still contiguous and unpublished.
 * Failure leaves the batch active and all handles unmodified. */
int crs_end(CrsBatch *batch);
/* After stock serialization, restore exactly the stored bytes. Empty handle
 * leaves the stock output untouched. An invalid handle never mutates it. */
int crs_write(const SpPool *pool, uint16_t handle, uint8_t *record, uint32_t length);
/* CURRENT establishes magic/version/CRC only, not semantic validity or owner. */
CrsKind crs_classify(const uint8_t bytes[SC_BYTES]);
#endif
