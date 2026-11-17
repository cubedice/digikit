/* CPU-only full/partial overwrite model. No real Sound or deserializer hook. */
#include "detach.h"
extern uint32_t tp_probe_mask(void *);
extern void tp_probe_restore(void *, uint32_t);
static SpPool pool;
static SpEntry slab[SP_PER_SLAB];
static uint32_t allocated, admitted, loading, generation;
static CpPreset current;
static uint8_t sound[DT_SOUND_SIZE], undo[DT_SOUND_SIZE], saved[DT_SOUND_SIZE];
static Detach t;
static void *allocate(uint32_t n, void *x)
{
    (void)x;
    if (allocated || n != sizeof(slab))
        return 0;
    allocated = 1;
    return slab;
}
static void release(void *p, void *x)
{
    (void)p;
    (void)x;
}
static int owned(void *x, uint32_t id, uint32_t gen, int active_load)
{
    (void)x;
    return id == 0x123400 && gen == generation && loading == (uint32_t)active_load &&
           admitted == (active_load ? 0u : 1u);
}
static int snapshot(void *x, uint32_t id, uint32_t gen, CpPreset *out)
{
    (void)x;
    if (!owned(0, id, gen, 0))
        return -1;
    for (uint32_t i = 0; i < sizeof(*out); ++i)
        ((volatile uint8_t *)out)[i] = ((const volatile uint8_t *)&current)[i];
    return 0;
}
static void begin(void *x, uint32_t id, uint32_t gen)
{
    (void)x;
    (void)id;
    (void)gen;
    admitted = 0;
    ++generation;
    ++loading;
}
static void end(void *x)
{
    (void)x;
    --loading;
}
static int admit_apply(void *x, uint32_t id, uint32_t gen, const CpPreset *p)
{
    (void)x;
    if (id != 0x123400 || gen != generation || loading || admitted)
        return -1;
    for (uint32_t i = 0; i < sizeof(*p); ++i)
        ((volatile uint8_t *)&current)[i] = ((const volatile uint8_t *)p)[i];
    admitted = 1;
    return 0;
}
static DtOps ops = {tp_probe_mask, tp_probe_restore, owned, snapshot, begin, end, admit_apply, 0};
static void zero_t(void)
{
    for (uint32_t i = 0; i < sizeof(t); ++i)
        ((volatile uint8_t *)&t)[i] = 0;
}
uint32_t canopy_detach_cf_run(void)
{
    for (uint32_t i = 0; i < sizeof(current); ++i)
        ((volatile uint8_t *)&current)[i] = 0;
    current.clock = CP_ARP_CLOCK;
    current.bpm = CP_ARP_BPM_RESERVED;
    current.control[CP_RATE] = 1;
    current.control[CP_GATE] = 1;
    current.control[CP_WT_RATE] = 1;
    current.control[CP_STRUM] = 2;
    current.mode = KB_ROOT;
    uint8_t bytes[SC_BYTES];
    if (!cp_valid(&current))
        return 1;
    allocated = 0;
    admitted = 1;
    loading = 0;
    generation = 7;
    sp_init(&pool, allocate, 0);
    sp_set_deallocator(&pool, release);
    uint16_t incoming = 0, credit = 0;
    if (sp_reserve_task(&pool, &incoming) || sp_reserve_task(&pool, &credit))
        return 2;
    current.control[CP_STRUM] = 9;
    if (sc_encode(&current, bytes) || sp_publish(&pool, incoming, bytes))
        return 3;
    current.control[CP_STRUM] = 2;
    for (uint32_t i = 0; i < DT_SOUND_SIZE; ++i)
        ((volatile uint8_t *)sound)[i] = (uint8_t)i;
    zero_t();
    if (dt_begin(&t, &pool, &ops, sound, undo, 0x123400, 7, credit, incoming))
        return 4;
    if (admitted || generation != 8 || loading != 1)
        return 5;
    for (uint32_t i = 0; i < DT_SOUND_SIZE; ++i)
        ((volatile uint8_t *)sound)[i] = 0x55;
    if (dt_finish(&t, 1) || !admitted || loading || current.control[CP_STRUM] != 9 ||
        sound[DT_HANDLE + 1] != (uint8_t)incoming)
        return 6;
    if (sp_reserve_task(&pool, &credit))
        return 7;
    for (uint32_t i = 0; i < DT_SOUND_SIZE; ++i)
        ((volatile uint8_t *)saved)[i] = sound[i];
    zero_t();
    if (dt_begin(&t, &pool, &ops, sound, undo, 0x123400, 8, credit, incoming))
        return 8;
    for (uint32_t i = 0; i < 32; ++i)
        ((volatile uint8_t *)sound)[i] = 0xaa;
    if (dt_finish(&t, 0) || !admitted || loading || generation != 9 ||
        current.control[CP_STRUM] != 9)
        return 9;
    saved[DT_HANDLE] = (uint8_t)(credit >> 8);
    saved[DT_HANDLE + 1] = (uint8_t)credit;
    for (uint32_t i = 0; i < DT_SOUND_SIZE; ++i)
        if (sound[i] != saved[i])
            return 10;
    return 0;
}
