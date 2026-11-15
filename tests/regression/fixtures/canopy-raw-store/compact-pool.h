/* Unlinked compact immutable pool experiment. Never install on hardware. */
#ifndef CANOPY_COMPACT_RAW_POOL_H
#define CANOPY_COMPACT_RAW_POOL_H
#include <stdint.h>
enum { CRP_BYTES=42, CRP_PER_SLAB=95, CRP_MAX_HANDLES=0xfffd,
       CRP_SLABS=(CRP_MAX_HANDLES+CRP_PER_SLAB-1)/CRP_PER_SLAB };
typedef void *(*CrpAlloc)(uint32_t size,void *context);
typedef void (*CrpFree)(void *memory,void *context);
typedef struct { uint8_t bytes[CRP_BYTES],ready; } CrpEntry;
typedef struct {
    CrpEntry *slab[CRP_SLABS];
    uint32_t count;
    CrpAlloc allocate;
    CrpFree release;
    void *context;
} CrpPool;
/* Task only, with all calls externally serialized. Existing records remain
 * alive until reset and published handles are never recycled. */
void crp_init(CrpPool *pool,CrpAlloc allocate,CrpFree release,void *context);
int crp_reserve(CrpPool *pool,uint32_t count,uint16_t *first);
/* Only the owner may return unpublished tokens at the pool's current tail. */
int crp_cancel_tail(CrpPool *pool,uint16_t first,uint32_t count);
/* These never allocate, but the caller must serialize publication and reads. */
int crp_publish(CrpPool *pool,uint16_t handle,const uint8_t bytes[CRP_BYTES]);
int crp_read(const CrpPool *pool,uint16_t handle,uint8_t out[CRP_BYTES]);
#endif
