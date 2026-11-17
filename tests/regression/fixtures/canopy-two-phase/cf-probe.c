/* Disposable ColdFire component, invoked from the emulator UI task only. */
#include "two-phase.h"
extern uint32_t tp_probe_sr(void);
extern uint32_t tp_probe_mask(void *);
extern void tp_probe_restore(void *, uint32_t);
volatile uint32_t tp_probe_result[8];
static SpPool pool;
static SpEntry slabs[2][SP_PER_SLAB];
static uint32_t next_slab, task_lock, inject, irq_count;
static uint16_t old;
static int ready(void *x)
{
    (void)x;
    return (tp_probe_sr() & 0x700u) == 0;
}
static int enter(void *x)
{
    (void)x;
    if (task_lock)
        return -1;
    task_lock = 1;
    return 0;
}
static void leave(void *x)
{
    (void)x;
    task_lock = 0;
}
static void *allocate(uint32_t bytes, void *x)
{
    (void)x;
    if ((tp_probe_sr() & 0x700u) || !task_lock || bytes != sizeof(slabs[0]) || next_slab >= 2)
        return 0;
    if (inject) {
        uint8_t input[SC_BYTES], output[SC_BYTES];
        for (uint32_t i = 0; i < SC_BYTES; ++i)
            input[i] = (uint8_t)(i + 1);
        uint32_t sr = tp_probe_mask(0);
        if (sp_publish(&pool, old, input) || sp_read(&pool, old, output))
            tp_probe_result[1] = 1;
        for (uint32_t i = 0; i < SC_BYTES; ++i)
            if (input[i] != output[i])
                tp_probe_result[1] = 2;
        tp_probe_restore(0, sr);
        ++irq_count;
    }
    return slabs[next_slab++];
}
static void release(void *memory, void *x)
{
    (void)memory;
    (void)x;
}
static TpSync sync = {ready, enter, leave, tp_probe_mask, tp_probe_restore, 0};
uint32_t tp_probe_run(const uint8_t *input, uint32_t length)
{
    (void)input;
    (void)length;
    for (uint32_t i = 0; i < 8; ++i)
        tp_probe_result[i] = 0;
    next_slab = task_lock = inject = irq_count = 0;
    old = 0;
    sp_init(&pool, allocate, 0);
    sp_set_deallocator(&pool, release);
    /* Existing reserved snapshot, then arrange for the next four credits to
     * require a new slab. Preparation runs in task context with IRQ enabled. */
    task_lock = 1;
    if (sp_reserve_batch_task(&pool, SP_PER_SLAB, &old)) {
        tp_probe_result[0] = 1;
        return 1;
    }
    task_lock = 0;
    inject = 1;
    uint16_t first = 0;
    if (tp_reserve_batch(&pool, &sync, 4, &first)) {
        tp_probe_result[0] = 2;
        return 2;
    }
    if (first != SP_PER_SLAB + 1 || pool.count != SP_PER_SLAB + 4 || irq_count != 1 ||
        tp_probe_result[1] || task_lock || (tp_probe_sr() & 0x700u)) {
        tp_probe_result[0] = 3;
        return 3;
    }
    tp_probe_result[2] = first;
    tp_probe_result[3] = pool.count;
    tp_probe_result[4] = irq_count;
    tp_probe_result[0] = 0x600d;
    return 0;
}
