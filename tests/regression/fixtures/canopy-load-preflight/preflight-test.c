#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "preflight.h"

static unsigned allocations, releases;
static int allocations_left = -1;
static void *allocate(uint32_t size, void *unused)
{
    (void)unused; ++allocations;
    if (!allocations_left) return 0;
    if (allocations_left > 0) --allocations_left;
    return malloc(size);
}
static void release(void *p, void *unused)
{ (void)unused; ++releases; free(p); }
static void free_pool(SpPool *p)
{ for (unsigned i = 0; i < SP_SLABS; ++i) free(p->slab[i]); }
static void make(uint8_t *bytes, unsigned n)
{ memset(bytes, 0, SC_BYTES); bytes[0] = (uint8_t)n; bytes[1] = (uint8_t)(n >> 8); }

int main(void)
{
    SpPool p; CanopyLoadPreflight load = {0};
    uint8_t bytes[SC_BYTES], read[SC_BYTES]; uint16_t original, h = 0x1234;
    sp_init(&p, allocate, 0); sp_set_deallocator(&p, release);
    make(bytes, 999); assert(!sp_intern(&p, bytes, &original));
    uint32_t old_count = p.count;
    /* A partial allocation failure leaves live snapshots readable and the
     * transaction inactive, so a checked caller can refuse before mutation. */
    allocations_left = 1; releases = 0;
    assert(canopy_load_begin(&load, &p, CANOPY_PROJECT_SOUND_LIMIT));
    assert(!load.pool && p.count == old_count && releases == 1);
    assert(!sp_read(&p, original, read) && !memcmp(bytes, read, SC_BYTES));
    assert(canopy_load_publish(&load, bytes, &h) && h == 0x1234);

    allocations_left = -1;
    assert(!canopy_load_begin(&load, &p, CANOPY_PROJECT_SOUND_LIMIT));
    assert(p.count == old_count + CANOPY_PROJECT_SOUND_LIMIT);
    assert(canopy_load_begin(&load, &p, 1)); /* nested load is rejected */
    unsigned before = allocations;
    allocations_left = 0;
    for (unsigned i = 0; i < CANOPY_PROJECT_SOUND_LIMIT; ++i) {
        make(bytes, i); assert(!canopy_load_publish(&load, bytes, &h));
        assert(h == old_count + i + 1);
        assert(!sp_read(&p, h, read) && !memcmp(bytes, read, SC_BYTES));
    }
    assert(allocations == before); /* publish must never call the allocator */
    h = 0x1234;
    assert(canopy_load_publish(&load, bytes, &h) && h == 0x1234);
    canopy_load_end(&load);
    assert(canopy_load_publish(&load, bytes, &h));
    assert(!load.pool);
    free_pool(&p);

    /* A short load returns only its unpublished tail, so ordinary loads do
     * not consume 2,192 handles merely because that many were preflighted. */
    SpPool partial; CanopyLoadPreflight short_load = {0};
    sp_init(&partial, allocate, 0); allocations_left = -1;
    assert(!canopy_load_begin(&short_load, &partial, CANOPY_PROJECT_SOUND_LIMIT));
    make(bytes, 77); assert(!canopy_load_publish(&short_load, bytes, &h) && h == 1);
    canopy_load_end(&short_load);
    assert(partial.count == 1 && !sp_read(&partial, h, read) && !memcmp(bytes, read, SC_BYTES));
    assert(!sp_reserve_task(&partial, &h) && h == 2);
    free_pool(&partial);

    SpPool full; CanopyLoadPreflight exhausted = {0};
    sp_init(&full, allocate, 0); allocations_left = -1;
    uint16_t first;
    assert(!sp_reserve_batch_task(&full, SP_MAX_HANDLES - 1, &first));
    old_count = full.count;
    assert(canopy_load_begin(&exhausted, &full, 2));
    assert(full.count == old_count && !exhausted.pool);
    free_pool(&full);
    return 0;
}
