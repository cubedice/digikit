/* Host-only experiment: no firmware hooks, scheduler, or persistence ABI. */
#ifndef CANOPY_BOUNDARY_CREDITS_H
#define CANOPY_BOUNDARY_CREDITS_H
#include <stdint.h>
#include "../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"
enum { BC_SLOTS = 4 };
typedef struct {
    uint32_t sound, generation;
    uint16_t credit;
    uint8_t dirty;
} BcSlot;
typedef struct {
    SpPool *pool;
    BcSlot slot[BC_SLOTS];
} BcCredits;
/* Initialize once for a pool lifetime; reinitializing would orphan credits
 * and lose dirty state. Published handles are never reclaimed by this API. */
void bc_init(BcCredits *c, SpPool *pool);
/* Bound identity may be changed only while clean. Unpublished reservations
 * belong to the slot, not the Sound, and are retained for later reuse. */
int bc_bind(BcCredits *c, uint32_t slot, uint32_t sound, uint32_t generation);
int bc_unbind(BcCredits *c, uint32_t slot, uint32_t sound, uint32_t generation);
/* Task context only. Reserve every missing bound-slot credit as one atomic
 * batch, or leave both credits and pool count unchanged on failure. */
int bc_replenish(BcCredits *c);
int bc_can_edit(const BcCredits *c, uint32_t slot, uint32_t sound, uint32_t generation);
int bc_edit_committed(BcCredits *c, uint32_t slot, uint32_t sound, uint32_t generation);
/* Publish only a pre-reserved entry. Caller attaches the returned handle to
 * the verified Sound under the same exclusion, before any copy may begin. */
int bc_capture(BcCredits *c, uint32_t slot, uint32_t sound, uint32_t generation,
               const uint8_t bytes[SC_BYTES], uint16_t *handle);
/* All calls, SpPool users and Sound ownership operations must be serialized.
 * Task replenishment can allocate and block; this interface does not provide
 * an IRQ-safe lock, deferred allocation or a real-time timing guarantee. */
#endif
