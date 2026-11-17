#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "raw-store.h"

static int allocations_left = -1;
static unsigned allocated, freed;
static void *alloc(uint32_t n, void *ctx)
{
    (void)ctx;
    if (!allocations_left)
        return 0;
    if (allocations_left > 0)
        --allocations_left;
    ++allocated;
    return malloc(n);
}
static void release(void *p, void *ctx)
{
    (void)ctx;
    ++freed;
    free(p);
}
static const uint16_t offsets[21] = {0x24, 0x2c, 0x34, 0x3c, 0x44, 0x4c, 0x54,
                                     0x5c, 0xb6, 0xb8, 0xba, 0xbc, 0xbe, 0xc0,
                                     0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce};
static void scatter(uint8_t *record, const uint8_t *bytes)
{
    for (unsigned i = 0; i < 21; ++i) {
        record[offsets[i]] = bytes[2 * i];
        record[offsets[i] + 1] = bytes[2 * i + 1];
    }
}
static void gather(const uint8_t *record, uint8_t *bytes)
{
    for (unsigned i = 0; i < 21; ++i) {
        bytes[2 * i] = record[offsets[i]];
        bytes[2 * i + 1] = record[offsets[i] + 1];
    }
}
static void free_pool(SpPool *pool)
{
    for (unsigned i = 0; i < SP_SLABS; ++i)
        free(pool->slab[i]);
}

int main(void)
{
    SpPool pool;
    CrsBatch batch = {0};
    uint8_t input[CRS_RECORD_BYTES], output[CRS_RECORD_BYTES], before[CRS_RECORD_BYTES];
    uint8_t raw[SC_BYTES] = {0}, read[SC_BYTES];
    uint16_t old = 0, h = 57, extra = 0;
    CrsKind kind = CRS_CORRUPT;
    sp_init(&pool, alloc, 0);
    sp_set_deallocator(&pool, release);
    raw[0] = 0xab;
    assert(!sp_intern(&pool, raw, &old));
    /* Fail in the middle of a project reservation. Existing records remain
     * usable, and no destination or caller output changes. */
    memset(input, 0x5a, sizeof(input));
    memcpy(before, input, sizeof(input));
    allocations_left = 1;
    freed = 0;
    assert(crs_begin(&batch, &pool, CRS_PROJECT_LIMIT));
    assert(!batch.pool && pool.count == 1 && freed == 1);
    assert(!sp_read(&pool, old, read) && !memcmp(raw, read, SC_BYTES));
    assert(crs_attach(&batch, input, sizeof(input), &h, &kind));
    assert(h == 57 && kind == CRS_CORRUPT && !memcmp(input, before, sizeof(input)));
    allocations_left = -1;
    assert(!crs_begin(&batch, &pool, 8));
    assert(crs_begin(&batch, &pool, 1)); /* nesting cannot replace the owner */

    /* A stock record carries an empty extension and consumes no entry. */
    memset(raw, 0, sizeof(raw));
    scatter(input, raw);
    assert(!crs_attach(&batch, input, sizeof(input), &h, &kind) && !h && kind == CRS_EMPTY);
    assert(batch.next == 2);
    /* A valid signature+CRC is recognizable without loading the module.
     * Semantic validity of this all-zero payload is deliberately not claimed. */
    raw[0] = 'C';
    raw[1] = 'S';
    raw[2] = 1;
    raw[40] = 0x76;
    raw[41] = 0x02;
    scatter(input, raw);
    unsigned before_allocated = allocated;
    assert(!crs_attach(&batch, input, sizeof(input), &h, &kind) && kind == CRS_CURRENT && h == 2);
    assert(allocated == before_allocated);
    memset(output, 0x33, sizeof(output));
    memcpy(before, output, sizeof(output));
    assert(!crs_write(&pool, h, output, sizeof(output)));
    gather(output, read);
    assert(!memcmp(raw, read, SC_BYTES));
    for (unsigned i = 0; i < sizeof(output); ++i) {
        unsigned slot = 0;
        for (unsigned j = 0; j < 21; ++j)
            if (i == offsets[j] || i == offsets[j] + 1u)
                slot = 1;
        if (!slot)
            assert(output[i] == before[i]);
    }
    /* An immutable handle can be copied. Later attachment cannot change it. */
    uint16_t copy = h;
    raw[2] = 2;
    scatter(input, raw);
    assert(!crs_attach(&batch, input, sizeof(input), &h, &kind) && kind == CRS_UNKNOWN_VERSION);
    memset(output, 0, sizeof(output));
    assert(!crs_write(&pool, h, output, sizeof(output)));
    gather(output, read);
    assert(!memcmp(raw, read, SC_BYTES));
    assert(!sp_read(&pool, copy, read) && read[2] == 1);
    raw[2] = 1;
    raw[3] ^= 1;
    scatter(input, raw);
    assert(!crs_attach(&batch, input, sizeof(input), &h, &kind) && kind == CRS_CORRUPT);
    memset(output, 0, sizeof(output));
    assert(!crs_write(&pool, h, output, sizeof(output)));
    gather(output, read);
    assert(!memcmp(raw, read, SC_BYTES));
    raw[0] = 'X';
    scatter(input, raw);
    assert(!crs_attach(&batch, input, sizeof(input), &h, &kind) && kind == CRS_FOREIGN);
    memset(output, 0, sizeof(output));
    assert(!crs_write(&pool, h, output, sizeof(output)));
    gather(output, read);
    assert(!memcmp(raw, read, SC_BYTES));

    /* Refuse short input or invalid handle without touching output. */
    memcpy(before, output, sizeof(output));
    assert(crs_write(&pool, SP_LOAD_ERROR, output, sizeof(output)) &&
           !memcmp(before, output, sizeof(output)));
    assert(crs_write(&pool, h, output, CRS_RECORD_BYTES - 1) &&
           !memcmp(before, output, sizeof(output)));
    assert(!crs_write(&pool, 0, output, sizeof(output)) && !memcmp(before, output, sizeof(output)));
    h = 57;
    kind = CRS_EMPTY;
    assert(crs_attach(&batch, input, CRS_RECORD_BYTES - 1, &h, &kind));
    assert(h == 57 && kind == CRS_EMPTY);

    /* An intervening reservation blocks tail return without changing state. */
    assert(!sp_reserve_task(&pool, &extra));
    uint32_t count = pool.count, next = batch.next, end = batch.end;
    assert(crs_end(&batch) && pool.count == count && batch.next == next && batch.end == end);
    assert(!sp_cancel_reserved_tail(&pool, extra, 1));
    assert(!crs_end(&batch) && !batch.pool && pool.count == next - 1);
    free_pool(&pool);
    return 0;
}
