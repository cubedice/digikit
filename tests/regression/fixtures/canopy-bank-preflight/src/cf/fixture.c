/* SYNTHETIC WORKER-ONLY EMULATOR EXPERIMENT: never install on a device. */
#include <stdint.h>
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/sound-state.h"
extern int32_t dkrt_job_post(uintptr_t, uintptr_t, uint8_t *);
extern uint8_t *fw_alloc(uint32_t);
extern void fw_free(uint8_t *);
enum { BANK_COUNT = 130, DEST_BYTES = 130 * 0x450, INPUT_BYTES = 4 + 130 * 0x455 };
/* command, job, step, done, return, before/after hash, begin before/after,
 * refusal before/after, injected allocation failure before/after, Sound
 * loads before/after, setup error, prior/final pool count, destination. */
volatile uint32_t canopy_bank_test[19];
volatile uint32_t canopy_bank_loads;
static uint8_t *target;
static SpPool pool;
static uint16_t reservation;
static uint32_t begins, refusals, allocation_failures, inject;
void canopy_bank_tick(void);
void canopy_bank_loaded(void)
{
    ++canopy_bank_loads;
    instrument_sound_proof_set(canopy_bank_tick);
}
static void *allocate(uint32_t n, void *unused)
{
    (void)unused;
    if (inject) {
        ++allocation_failures;
        return 0;
    }
    return fw_alloc(n);
}
static void release(void *memory, void *unused)
{
    (void)unused;
    fw_free((uint8_t *)memory);
}
int canopy_bank_begin(void)
{
    ++begins;
    if (reservation || sp_reserve_batch_task(&pool, BANK_COUNT, &reservation)) {
        ++refusals;
        return -1;
    }
    return 0;
}
void canopy_bank_end(void)
{
    if (reservation) {
        if (sp_cancel_reserved_tail(&pool, reservation, BANK_COUNT))
            ++canopy_bank_test[15];
        reservation = 0;
    }
}
int canopy_bank_is_target(uint32_t address)
{
    return target && address == (uint32_t)(uintptr_t)target;
}
static uint32_t digest(const uint8_t *p, uint32_t n)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < n; ++i)
        hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
static int32_t step(uint8_t *unused)
{
    (void)unused;
    uint8_t *d = fw_alloc(DEST_BYTES), *input = fw_alloc(INPUT_BYTES);
    if (!d || !input) {
        canopy_bank_test[15] = 1;
        if (d)
            fw_free(d);
        if (input)
            fw_free(input);
        return -1;
    }
    sp_init(&pool, allocate, 0);
    sp_set_deallocator(&pool, release);
    for (uint32_t i = 0; i < DEST_BYTES; ++i)
        ((volatile uint8_t *)d)[i] = (uint8_t)(i * 29u + 7u);
    for (uint32_t i = 0; i < INPUT_BYTES; ++i)
        ((volatile uint8_t *)input)[i] = 0;
    input[3] = 3;
    for (uint32_t i = 0; i < BANK_COUNT; ++i)
        input[4 + i * 0x455 + 3] = 3;
    target = d;
    canopy_bank_test[18] = (uint32_t)(uintptr_t)d;
    canopy_bank_test[5] = digest(d, DEST_BYTES);
    canopy_bank_test[7] = begins;
    canopy_bank_test[9] = refusals;
    canopy_bank_test[11] = allocation_failures;
    canopy_bank_test[13] = canopy_bank_loads;
    canopy_bank_test[16] = pool.count;
    /* Force a real new-slab allocation failure. A 130-entry reservation
     * necessarily reaches a slab that was not already allocated. */
    inject = 1;
    canopy_bank_test[4] = ((uint32_t (*)(void *, const void *))(uintptr_t)0x400db830u)(d, input);
    inject = 0;
    canopy_bank_test[6] = digest(d, DEST_BYTES);
    canopy_bank_test[8] = begins;
    canopy_bank_test[10] = refusals;
    canopy_bank_test[12] = allocation_failures;
    canopy_bank_test[14] = canopy_bank_loads;
    canopy_bank_test[17] = pool.count;
    target = 0;
    fw_free(input);
    fw_free(d);
    canopy_bank_test[2] = 1;
    return 0;
}
static void done(uint8_t *unused, int32_t result)
{
    (void)unused;
    canopy_bank_test[3] = (uint32_t)result;
    canopy_bank_test[0] = 2;
}
void canopy_bank_tick(void)
{
    if (canopy_bank_test[0] != 1)
        return;
    canopy_bank_test[0] = 3;
    canopy_bank_test[1] = (uint32_t)dkrt_job_post((uintptr_t)step, (uintptr_t)done, 0);
}
