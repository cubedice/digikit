/* Host-only credit lifecycle and real encoder-transaction integration. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include "../fixtures/canopy-credits/boundary-credits.h"

typedef struct {
    int fail;
    unsigned calls, frees;
} Allocator;
static void *allocate(uint32_t n, void *context)
{
    Allocator *a = context;
    assert(!masked);
    ++a->calls;
    return a->fail ? 0 : calloc(1, n);
}
static void release(void *p, void *context)
{
    Allocator *a = context;
    ++a->frees;
    free(p);
}
static void clear_pool(SpPool *p)
{
    for (unsigned i = 0; i < SP_SLABS; ++i)
        if (p->slab[i])
            free(p->slab[i]);
}
static int prepare(void *context, const CpPreset *p)
{
    BcCredits *c = context;
    uint8_t bytes[SC_BYTES];
    assert(masked);
    return bc_can_edit(c, 0, 0x123400, 7) || sc_encode(p, bytes);
}
static void attach(void *context)
{
    assert(masked && !bc_edit_committed(context, 0, 0x123400, 7));
}
static int turn(BcCredits *c, int amount)
{
    instrument_select_track(0);
    uint32_t view = instrument_src_context(0, 0, 1);
    assert(view);
    return instrument_sound_proof_edit(0, 0x123400, 7, view, 3, amount, 100, prepare, attach, c);
}
static void scenario(void)
{
    Allocator alloc = {0};
    SpPool pool;
    BcCredits c;
    sp_init(&pool, allocate, &alloc);
    sp_set_deallocator(&pool, release);
    bc_init(&c, &pool);
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        assert(!bc_bind(&c, i, 0x123400 + 0x1000 * i, 7 + i));
    assert(bc_bind(&c, 1, 0x123400, 8) < 0); /* duplicate active Sound */
    assert(bc_bind(&c, 4, 5, 1) < 0 && bc_bind(&c, 0, 0, 1) < 0);
    assert(turn(&c, 1) < 0 && cur->pad_strum == 0 && !c.slot[0].dirty);
    /* Whole initial batch fails with no partial credits or accepted edit. */
    alloc.fail = 1;
    assert(bc_replenish(&c) < 0 && !pool.count);
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        assert(!c.slot[i].credit);
    assert(turn(&c, 1) < 0 && cur->pad_strum == 0);
    alloc.fail = 0;
    assert(!bc_replenish(&c) && pool.count == 4);
    uint16_t keep[BC_SLOTS];
    for (uint32_t i = 0; i < BC_SLOTS; ++i) {
        keep[i] = c.slot[i].credit;
        assert(keep[i] == i + 1);
    }
    assert(!bc_replenish(&c) && pool.count == 4);
    assert(turn(&c, 1) == CS_EDIT && turn(&c, 1) == CS_EDIT && cur->pad_strum == 2);
    assert(c.slot[0].dirty && c.slot[0].credit == keep[0] && pool.count == 4);
    assert(bc_unbind(&c, 0, 0x123400, 7) < 0 && bc_bind(&c, 0, 0x999900, 8) < 0);
    uint8_t bytes[SC_BYTES], readback[SC_BYTES];
    CpPreset p;
    preset_snapshot(cur, &p);
    assert(!sc_encode(&p, bytes));
    uint16_t handle = 0;
    assert(bc_capture(&c, 0, 0x123400, 8, bytes, &handle) < 0 && !handle);
    assert(!bc_capture(&c, 0, 0x123400, 7, bytes, &handle) && handle == keep[0]);
    assert(!sp_read(&pool, handle, readback) && !memcmp(bytes, readback, sizeof(bytes)));
    assert(turn(&c, 1) < 0 && cur->pad_strum == 2);
    assert(!bc_replenish(&c) && pool.count == 5 && c.slot[0].credit == 5);
    assert(!bc_unbind(&c, 0, 0x123400, 7));
    assert(!bc_bind(&c, 0, 0x999900, 8) && c.slot[0].credit == 5 && pool.count == 5);
    assert(bc_can_edit(&c, 0, 0x123400, 7) < 0 && !bc_can_edit(&c, 0, 0x999900, 8));
    assert(!bc_unbind(&c, 0, 0x999900, 8));
    assert(!bc_bind(&c, 0, 0x123400, 7) && c.slot[0].credit == 5);
    assert(turn(&c, 1) == CS_EDIT && cur->pad_strum == 3);
    assert(!sp_read(&pool, handle, readback) && !memcmp(bytes, readback, sizeof(bytes)));
    clear_pool(&pool);
}
static void cross_slab_failure(void)
{
    Allocator alloc = {0};
    SpPool pool;
    BcCredits c;
    uint16_t first = 0;
    sp_init(&pool, allocate, &alloc);
    sp_set_deallocator(&pool, release);
    bc_init(&c, &pool);
    assert(!sp_reserve_batch_task(&pool, SP_PER_SLAB - 1, &first) && first == 1);
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        assert(!bc_bind(&c, i, 0x2000 + i, 1));
    alloc.fail = 1;
    assert(bc_replenish(&c) < 0 && pool.count == SP_PER_SLAB - 1);
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        assert(!c.slot[i].credit);
    alloc.fail = 0;
    assert(!bc_replenish(&c) && pool.count == SP_PER_SLAB + 3);
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        assert(c.slot[i].credit == SP_PER_SLAB + i);
    clear_pool(&pool);
}
int main(void)
{
    mi_ledger.epoch = 1;
    mi_ledger.generations[0] = 1;
    instrument_play_tick();
    ip_admission[0] = 1;
    ip_admission[1] = 0x123400;
    ip_admission[4] = 7;
    ip_admission[5] = admitted;
    cur->mode = keyboard.mode = KB_ROOT;
    instrument_select_track(0);
    scenario();
    cross_slab_failure();
    return 0;
}
