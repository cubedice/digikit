/* Immutable, boot-lifetime snapshot handles. This is a standalone candidate;
 * the caller must provide serialized access and Sound lifetime management. */
#ifndef DK_CANOPY_SOUND_POOL_H
#define DK_CANOPY_SOUND_POOL_H
#include <stdint.h>
#include "sound-codec.h"
/* A 89-entry slab fits a 4 KiB buddy block, including each entry's state. */
/* The emulator experiment reserves the top two values for Sound lifecycle
 * states; neither can alias an immutable snapshot handle. */
enum { SP_PER_SLAB=89, SP_MAX_HANDLES=0xfffd, SP_LOAD_ERROR=0xfffe, SP_PENDING_DEFAULT=0xffff,
    SP_SLABS=(SP_MAX_HANDLES+SP_PER_SLAB-1)/SP_PER_SLAB, SP_BUCKETS=256 };
typedef void *(*SpAlloc)(uint32_t size, void *context);
typedef void (*SpFree)(void *memory, void *context);
typedef struct { uint8_t bytes[SC_BYTES]; uint16_t next; uint8_t ready; } SpEntry;
typedef struct {
    SpEntry *slab[SP_SLABS];
    uint16_t bucket[SP_BUCKETS];
    uint32_t count;
    SpAlloc allocate;
    SpFree release;
    void *context;
} SpPool;
/* Pool and allocated slabs must remain alive until all Sound handles expire.
 * Zero means no snapshot. A published handle is never recycled within one boot. */
void sp_init(SpPool *pool, SpAlloc allocate, void *context);
/* Optional matching allocator release, used only to roll back slabs newly
 * allocated by a failed batch. Set before using the pool under the same
 * external serialization as other pool operations. */
void sp_set_deallocator(SpPool *pool, SpFree release);
/* TASK ONLY: may allocate. Reserves a handle; no snapshot is readable until
 * published, after which it is never reused. Failure leaves the output untouched. The
 * caller must serialize all pool operations, including allocation. */
int sp_reserve_task(SpPool *pool, uint16_t *handle);
/* TASK ONLY: atomically reserve `count` consecutive handles, allocating every
 * needed slab before advancing the pool. On failure no handles are consumed
 * and *first is unchanged. With a matching deallocator, newly allocated
 * slabs are released on failure; otherwise they remain available for retry. This
 * does not make a stock loader transactional, nor identify Sound owners. */
int sp_reserve_batch_task(SpPool *pool, uint32_t count, uint16_t *first);
/* TASK ONLY: return a contiguous, unpublished reservation at the current pool
 * tail. The caller must own every token in the range and retire them before
 * reuse, under the same pool lock. No published handle is ever recycled.
 * Fails without mutation if the range is not the tail or contains a snapshot. */
int sp_cancel_reserved_tail(SpPool *pool, uint16_t first, uint32_t count);
/* Never allocates; publishes a previously reserved handle exactly once.
 * External serialization and a measured IRQ latency budget are still needed. */
int sp_publish(SpPool *pool, uint16_t handle, const uint8_t bytes[SC_BYTES]);
int sp_intern(SpPool *pool, const uint8_t bytes[SC_BYTES], uint16_t *handle);
int sp_read(const SpPool *pool, uint16_t handle, uint8_t out[SC_BYTES]);
#endif
