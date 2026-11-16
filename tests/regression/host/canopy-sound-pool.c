#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"

static int fail_alloc;
static int allocations_until_failure = -1;
static uint32_t allocation_calls;
static uint32_t release_calls;
static void *allocate(uint32_t size, void *unused)
{
    (void)unused;
    ++allocation_calls;
    if (fail_alloc || allocations_until_failure == 0)
        return 0;
    if (allocations_until_failure > 0)
        --allocations_until_failure;
    return malloc(size);
}
static void release(void *memory, void *unused)
{
    (void)unused;
    ++release_calls;
    free(memory);
}
static void make(uint8_t bytes[SC_BYTES], uint32_t n)
{
    memset(bytes, 0, SC_BYTES);
    bytes[0] = (uint8_t)n;
    bytes[1] = (uint8_t)(n >> 8);
    bytes[2] = (uint8_t)(n >> 16);
    bytes[SC_BYTES - 1] = 0xa5;
}
static void reservation(void)
{
    SpPool pool;
    uint8_t bytes[SC_BYTES], out[SC_BYTES], unchanged[SC_BYTES];
    uint16_t h = 71;
    sp_init(&pool, allocate, 0);
    make(bytes, 9);
    memset(out, 0x5a, sizeof(out));
    memcpy(unchanged, out, sizeof(out));
    fail_alloc = 1;
    assert(sp_reserve_task(&pool, &h) < 0 && h == 71 && !pool.count);
    fail_alloc = 0;
    assert(!sp_reserve_task(&pool, &h) && h == 1 && pool.count == 1);
    assert(sp_read(&pool, h, out) < 0 && !memcmp(out, unchanged, sizeof(out)));
    /* Publishing uses only previously reserved memory. */
    fail_alloc = 1;
    assert(!sp_publish(&pool, h, bytes));
    assert(!sp_read(&pool, h, out) && !memcmp(bytes, out, sizeof(out)));
    make(bytes, 10);
    assert(sp_publish(&pool, h, bytes) < 0);
    assert(!sp_read(&pool, h, out) && memcmp(bytes, out, sizeof(out)));
    fail_alloc = 0;
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
}
static void batch_reservation(void)
{
    SpPool pool;
    uint8_t bytes[SC_BYTES], out[SC_BYTES];
    uint16_t first = 99, existing = 0, other = 0;
    sp_init(&pool, allocate, 0);
    make(bytes, 42);
    assert(!sp_intern(&pool, bytes, &existing) && existing == 1);
    /* Failure after two new slabs have been attempted does not consume any
     * handles or alter an existing published snapshot. */
    allocations_until_failure = 1;
    assert(sp_reserve_batch_task(&pool, 2192, &first) < 0);
    assert(first == 99 && pool.count == 1);
    assert(!sp_read(&pool, existing, out) && !memcmp(bytes, out, SC_BYTES));
    allocations_until_failure = -1;
    assert(!sp_reserve_batch_task(&pool, 2192, &first) && first == 2);
    assert(pool.count == 1 + 2192);
    assert(sp_read(&pool, first, out) < 0);
    make(bytes, 43);
    assert(!sp_publish(&pool, first, bytes));
    assert(!sp_read(&pool, first, out) && !memcmp(bytes, out, SC_BYTES));
    /* Ordinary interning cannot take one of the reserved handles. */
    make(bytes, 44);
    assert(!sp_intern(&pool, bytes, &other));
    assert(other == first + 2192);
    first = 99;
    assert(sp_reserve_batch_task(&pool, 0, &first) < 0 && first == 99);
    assert(sp_reserve_batch_task(&pool, UINT32_MAX, &first) < 0 && first == 99);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
}
static void batch_crosses_maximum_slabs(void)
{
    SpPool pool;
    uint16_t first = 0;
    uint32_t calls;
    sp_init(&pool, allocate, 0);
    assert(!sp_reserve_batch_task(&pool, SP_PER_SLAB - 1, &first));
    calls = allocation_calls;
    assert(!sp_reserve_batch_task(&pool, 2192, &first));
    /* The first slot is in the existing slab; 25 more slabs are needed. */
    assert(first == SP_PER_SLAB && allocation_calls - calls == 25);
    assert(pool.count == SP_PER_SLAB - 1 + 2192);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
}
static void batch_failure_releases_only_new_slabs(void)
{
    SpPool pool;
    uint8_t bytes[SC_BYTES], out[SC_BYTES];
    uint16_t first = 77, existing = 0;
    sp_init(&pool, allocate, 0);
    make(bytes, 17);
    assert(!sp_intern(&pool, bytes, &existing) && existing == 1);
    SpEntry *original = pool.slab[0];
    /* A previous failed batch without a deallocator retained slab 1. */
    allocations_until_failure = 1;
    assert(sp_reserve_batch_task(&pool, 2192, &first) < 0);
    SpEntry *retained = pool.slab[1];
    assert(retained && pool.count == 1 && first == 77);
    sp_set_deallocator(&pool, release);
    release_calls = 0;
    allocations_until_failure = 2;
    assert(sp_reserve_batch_task(&pool, 2192, &first) < 0);
    assert(first == 77 && pool.count == 1 && release_calls == 2);
    assert(pool.slab[0] == original && pool.slab[1] == retained && !pool.slab[2] && !pool.slab[3]);
    assert(!sp_read(&pool, existing, out) && !memcmp(bytes, out, SC_BYTES));
    allocations_until_failure = -1;
    assert(!sp_reserve_batch_task(&pool, 2192, &first) && first == 2);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
}
static void cancellation_only_returns_unpublished_tail(void)
{
    SpPool pool;
    uint16_t first = 0, other = 0;
    uint8_t bytes[SC_BYTES], out[SC_BYTES];
    sp_init(&pool, allocate, 0);
    assert(!sp_reserve_batch_task(&pool, 4, &first) && first == 1);
    make(bytes, 42);
    assert(!sp_publish(&pool, first, bytes));
    assert(sp_cancel_reserved_tail(&pool, first, 4) < 0 && pool.count == 4);
    assert(sp_cancel_reserved_tail(&pool, 2, 2) < 0 && pool.count == 4);
    assert(sp_cancel_reserved_tail(&pool, 2, UINT32_MAX) < 0 && pool.count == 4);
    assert(!sp_cancel_reserved_tail(&pool, 2, 3) && pool.count == 1);
    assert(!sp_read(&pool, 1, out) && !memcmp(bytes, out, SC_BYTES));
    assert(!sp_reserve_batch_task(&pool, 3, &first) && first == 2);
    assert(!sp_reserve_task(&pool, &other) && other == 5);
    assert(sp_cancel_reserved_tail(&pool, first, 3) < 0 && pool.count == 5);
    assert(!sp_cancel_reserved_tail(&pool, other, 1) && pool.count == 4);
    assert(!sp_cancel_reserved_tail(&pool, first, 3) && pool.count == 1);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
}
int main(void)
{
    assert(SP_MAX_HANDLES < SP_LOAD_ERROR && SP_LOAD_ERROR < SP_PENDING_DEFAULT);
    reservation();
    batch_reservation();
    batch_crosses_maximum_slabs();
    batch_failure_releases_only_new_slabs();
    cancellation_only_returns_unpublished_tail();
    SpPool pool;
    uint8_t bytes[SC_BYTES], out[SC_BYTES];
    uint16_t handle = 17;
    sp_init(&pool, allocate, 0);
    assert(sp_read(&pool, 0, out) < 0 && sp_read(&pool, 1, out) < 0);
    make(bytes, 1);
    fail_alloc = 1;
    assert(sp_intern(&pool, bytes, &handle) < 0 && handle == 17 && !pool.count);
    fail_alloc = 0;
    for (uint32_t i = 1; i <= SP_MAX_HANDLES; ++i) {
        make(bytes, i);
        assert(!sp_intern(&pool, bytes, &handle) && handle == i);
        if (i == SP_PER_SLAB) {
            fail_alloc = 1;
            make(bytes, i + 1);
            uint16_t unchanged = 123;
            assert(sp_intern(&pool, bytes, &unchanged) < 0 && unchanged == 123 && pool.count == i);
            fail_alloc = 0;
        }
    }
    for (uint32_t i = 1; i <= SP_MAX_HANDLES; i += 251) {
        make(bytes, i);
        assert(!sp_read(&pool, (uint16_t)i, out));
        assert(!memcmp(bytes, out, SC_BYTES));
        assert(!sp_intern(&pool, bytes, &handle) && handle == i);
    }
    make(bytes, SP_MAX_HANDLES + 1);
    handle = 123;
    assert(sp_intern(&pool, bytes, &handle) < 0 && handle == 123 && pool.count == SP_MAX_HANDLES);
    assert(sp_reserve_batch_task(&pool, 1, &handle) < 0 && handle == 123 &&
           pool.count == SP_MAX_HANDLES);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
    return 0;
}
