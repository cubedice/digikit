/* Unlinked copy-on-share experiment. This does not manage Sound lifetimes. */
#ifndef DK_CANOPY_SOUND_SHARE_POOL_H
#define DK_CANOPY_SOUND_SHARE_POOL_H
#include <stdint.h>
#include "sound-codec.h"

enum { SS_RESERVED=1, SS_EXCLUSIVE=2, SS_FROZEN=3, SS_MAX_HANDLES=0xfffd };
typedef struct {
    uint8_t bytes[SC_BYTES];
    uintptr_t owner;
    uint8_t state;
} SsEntry;
typedef struct {
    SsEntry *entries;
    uint32_t capacity, count;
} SsPool;

/* The caller provides stable storage; no function allocates or waits. Handles
 * are never recycled, including unused reservations, for the pool lifetime.
 * Initialize only when no Sound can retain an earlier handle. */
int ss_init(SsPool *pool,SsEntry *entries,uint32_t capacity);
/* Reserve in a verified task context, before any edit/copy critical section.
 * Failure does not change the output. A reservation is not readable. */
int ss_reserve(SsPool *pool,uint16_t *handle);
/* Publish a reserved snapshot to an empty owner slot. owner_identity must be
 * a nonzero boot-lifetime token that is never reused while an entry can exist;
 * a Sound address alone is not sufficient. */
int ss_create(SsPool *pool,uint16_t reserved,uintptr_t owner_identity,
              uint16_t *owner_handle,const uint8_t bytes[SC_BYTES]);
/* Freeze immediately before stock copies this Sound. Call while holding the
 * same serialization over the entire copy, not just over this call. */
int ss_share(SsPool *pool,uint16_t handle,uintptr_t owner_identity);
/* Non-mutating eligibility check for a transaction prepare callback. Its
 * result remains valid only while the caller keeps the same exclusion through
 * the eventual ss_edit commit. */
int ss_can_edit(const SsPool *pool,const uint16_t *owner_handle,uintptr_t owner_identity,
                uint16_t expected,uint16_t reserved);
/* Edit only if *owner_handle still equals expected. An exclusive snapshot is
 * updated without consuming reserved; a frozen snapshot needs a reserved handle
 * and atomically detaches to a new exclusive one. Failure leaves the old entry,
 * the caller's Sound handle, and the reservation unchanged. */
int ss_edit(SsPool *pool,uint16_t *owner_handle,uintptr_t owner_identity,
            uint16_t expected,uint16_t reserved,const uint8_t bytes[SC_BYTES]);
int ss_read(const SsPool *pool,uint16_t handle,uint8_t out[SC_BYTES]);

/* All functions, snapshot reads, Sound-handle changes, ownership validation,
 * Sound copies, and edit publication must use one proven serialization. The
 * caller checks the Sound address, generation, machine type, and object life
 * in that same critical section. In particular, the pool cannot validate which
 * Sound owns a frozen handle. If any copy/overwrite bypasses this protocol,
 * snapshots can become stale or be changed under another owner. No reclaim or
 * safe restart/boot handling is supplied by this experiment. */
#endif
