/* Host-side experiment only. No firmware hooks use this API. */
#ifndef CANOPY_LOAD_PREFLIGHT_H
#define CANOPY_LOAD_PREFLIGHT_H
#include <stdint.h>
#include "../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"

enum { CANOPY_PROJECT_SOUND_LIMIT = 2192 };

typedef struct {
    SpPool *pool;
    uint32_t next;
    uint32_t end;
} CanopyLoadPreflight;

/* Caller must serialize all pool operations and call from a task in which
 * allocation and the firmware allocator's locks are safe. These conditions
 * have not been proven at all DTII load entry points. */
int canopy_load_begin(CanopyLoadPreflight *load, SpPool *pool, uint32_t count);
int canopy_load_publish(CanopyLoadPreflight *load, const uint8_t bytes[SC_BYTES],
                        uint16_t *handle);
void canopy_load_end(CanopyLoadPreflight *load);
#endif
