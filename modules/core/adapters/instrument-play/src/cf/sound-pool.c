#include "sound-pool.h"

_Static_assert(sizeof(SpEntry) * SP_PER_SLAB <= 4096,
               "Sound pool slab exceeds a 4 KiB buddy block");

static uint32_t hash(const uint8_t *bytes)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < SC_BYTES; ++i)
        h = (h ^ bytes[i]) * 16777619u;
    return (h ^ (h >> 16)) & (SP_BUCKETS - 1);
}
static SpEntry *entry(const SpPool *pool, uint32_t handle)
{
    if (!handle || handle > pool->count)
        return 0;
    uint32_t index = handle - 1, slab = index / SP_PER_SLAB;
    return pool->slab[slab] ? &pool->slab[slab][index % SP_PER_SLAB] : 0;
}
void sp_init(SpPool *pool, SpAlloc allocate, void *context)
{
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        pool->slab[i] = 0;
    for (uint32_t i = 0; i < SP_BUCKETS; ++i)
        pool->bucket[i] = 0;
    pool->count = 0;
    pool->allocate = allocate;
    pool->release = 0;
    pool->context = context;
}
void sp_set_deallocator(SpPool *pool, SpFree release)
{
    if (pool)
        pool->release = release;
}
int sp_reserve_batch_task(SpPool *pool, uint32_t count, uint16_t *first)
{
    if (!pool || !first || !pool->allocate || !count || pool->count > SP_MAX_HANDLES ||
        count > SP_MAX_HANDLES - pool->count)
        return -1;
    uint32_t start = pool->count, end = start + count;
    /* Volatile stores prevent the freestanding compiler from synthesizing an
     * unavailable memset dependency in the ColdFire proof object. */
    volatile uint32_t created[(SP_SLABS + 31) / 32];
    for (uint32_t n = 0; n < (SP_SLABS + 31) / 32; ++n)
        created[n] = 0;
    for (uint32_t slab = start / SP_PER_SLAB; slab <= (end - 1) / SP_PER_SLAB; ++slab) {
        if (!pool->slab[slab]) {
            SpEntry *p = (SpEntry *)pool->allocate(sizeof(SpEntry) * SP_PER_SLAB, pool->context);
            if (!p) {
                if (pool->release)
                    for (uint32_t n = start / SP_PER_SLAB; n < slab; ++n)
                        if (created[n / 32] & (1u << (n % 32))) {
                            SpEntry *old = pool->slab[n];
                            pool->slab[n] = 0;
                            pool->release(old, pool->context);
                        }
                return -1;
            }
            pool->slab[slab] = p;
            created[slab / 32] |= 1u << (slab % 32);
        }
    }
    for (uint32_t index = start; index < end; ++index) {
        SpEntry *e = &pool->slab[index / SP_PER_SLAB][index % SP_PER_SLAB];
        e->ready = 0;
        e->next = 0;
    }
    pool->count = end;
    *first = (uint16_t)(start + 1);
    return 0;
}
int sp_reserve_task(SpPool *pool, uint16_t *handle)
{
    return sp_reserve_batch_task(pool, 1, handle);
}
int sp_cancel_reserved_tail(SpPool *pool, uint16_t first, uint32_t count)
{
    if (!pool || !first || !count || first > pool->count ||
        count != pool->count - (uint32_t)first + 1u)
        return -1;
    for (uint32_t h = first; h <= pool->count; ++h) {
        const SpEntry *e = entry(pool, h);
        if (!e || e->ready)
            return -1;
    }
    pool->count = (uint32_t)first - 1u;
    return 0;
}
int sp_publish(SpPool *pool, uint16_t handle, const uint8_t bytes[SC_BYTES])
{
    if (!pool || !bytes)
        return -1;
    SpEntry *e = entry(pool, handle);
    if (!e || e->ready)
        return -1;
    uint32_t bucket = hash(bytes);
    for (uint32_t i = 0; i < SC_BYTES; ++i)
        e->bytes[i] = bytes[i];
    e->next = pool->bucket[bucket];
    e->ready = 1;
    pool->bucket[bucket] = handle;
    return 0;
}
int sp_intern(SpPool *pool, const uint8_t bytes[SC_BYTES], uint16_t *handle)
{
    if (!pool || !bytes || !handle || !pool->allocate)
        return -1;
    uint32_t bucket = hash(bytes);
    for (uint32_t h = pool->bucket[bucket]; h;) {
        const SpEntry *e = entry(pool, h);
        if (!e || !e->ready)
            return -1;
        uint32_t n = 0;
        while (n < SC_BYTES && e->bytes[n] == bytes[n])
            ++n;
        if (n == SC_BYTES) {
            *handle = (uint16_t)h;
            return 0;
        }
        h = e->next;
    }
    uint16_t reserved;
    if (sp_reserve_task(pool, &reserved) || sp_publish(pool, reserved, bytes))
        return -1;
    *handle = reserved;
    return 0;
}
int sp_read(const SpPool *pool, uint16_t handle, uint8_t out[SC_BYTES])
{
    if (!pool || !out)
        return -1;
    const SpEntry *e = entry(pool, handle);
    if (!e || !e->ready)
        return -1;
    for (uint32_t i = 0; i < SC_BYTES; ++i)
        out[i] = e->bytes[i];
    return 0;
}
