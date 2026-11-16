/* Unlinked compact immutable pool experiment. Never install on hardware. */
#include "compact-pool.h"
_Static_assert(sizeof(CrpEntry) == 43, "unexpected record layout");
_Static_assert(sizeof(CrpEntry) * CRP_PER_SLAB <= 4096, "slab exceeds 4 KiB");

static CrpEntry *entry(const CrpPool *pool, uint32_t handle)
{
    if (!pool || !handle || handle > pool->count)
        return 0;
    uint32_t index = handle - 1, slab = index / CRP_PER_SLAB;
    return pool->slab[slab] ? &pool->slab[slab][index % CRP_PER_SLAB] : 0;
}
void crp_init(CrpPool *pool, CrpAlloc allocate, CrpFree release, void *context)
{
    for (uint32_t i = 0; i < CRP_SLABS; ++i)
        pool->slab[i] = 0;
    pool->count = 0;
    pool->allocate = allocate;
    pool->release = release;
    pool->context = context;
}
int crp_reserve(CrpPool *pool, uint32_t count, uint16_t *first)
{
    if (!pool || !pool->allocate || !first || !count || pool->count > CRP_MAX_HANDLES ||
        count > CRP_MAX_HANDLES - pool->count)
        return -1;
    uint32_t start = pool->count, end = start + count;
    volatile uint32_t created[(CRP_SLABS + 31) / 32];
    for (uint32_t i = 0; i < (CRP_SLABS + 31) / 32; ++i)
        created[i] = 0;
    for (uint32_t slab = start / CRP_PER_SLAB; slab <= (end - 1) / CRP_PER_SLAB; ++slab) {
        if (!pool->slab[slab]) {
            CrpEntry *p =
                (CrpEntry *)pool->allocate(sizeof(CrpEntry) * CRP_PER_SLAB, pool->context);
            if (!p) {
                if (pool->release)
                    for (uint32_t n = start / CRP_PER_SLAB; n < slab; ++n)
                        if (created[n / 32] & (1u << (n % 32))) {
                            CrpEntry *old = pool->slab[n];
                            pool->slab[n] = 0;
                            pool->release(old, pool->context);
                        }
                return -1;
            }
            pool->slab[slab] = p;
            created[slab / 32] |= 1u << (slab % 32);
        }
    }
    for (uint32_t index = start; index < end; ++index)
        pool->slab[index / CRP_PER_SLAB][index % CRP_PER_SLAB].ready = 0;
    pool->count = end;
    *first = (uint16_t)(start + 1);
    return 0;
}
int crp_cancel_tail(CrpPool *pool, uint16_t first, uint32_t count)
{
    if (!pool || !first || !count || first > pool->count ||
        count != pool->count - (uint32_t)first + 1u)
        return -1;
    for (uint32_t h = first; h <= pool->count; ++h) {
        const CrpEntry *p = entry(pool, h);
        if (!p || p->ready)
            return -1;
    }
    pool->count = (uint32_t)first - 1u;
    return 0;
}
int crp_publish(CrpPool *pool, uint16_t handle, const uint8_t bytes[CRP_BYTES])
{
    if (!bytes)
        return -1;
    CrpEntry *p = entry(pool, handle);
    if (!p || p->ready)
        return -1;
    for (uint32_t i = 0; i < CRP_BYTES; ++i)
        p->bytes[i] = bytes[i];
    p->ready = 1;
    return 0;
}
int crp_read(const CrpPool *pool, uint16_t handle, uint8_t out[CRP_BYTES])
{
    if (!out)
        return -1;
    const CrpEntry *p = entry(pool, handle);
    if (!p || !p->ready)
        return -1;
    for (uint32_t i = 0; i < CRP_BYTES; ++i)
        out[i] = p->bytes[i];
    return 0;
}
