/* ColdFire component probe; not a device module. */
#include "copy-guard.h"
#include "../../../../modules/core/adapters/instrument-play/src/cf/sound-share-pool.h"
extern uint32_t cg_probe_mask(void *);
extern void cg_probe_restore(void *, uint32_t);
extern void *cg_probe_stock_memcpy(void *, const void *, uint32_t);
static uint32_t source[(CG_KIT + 3) / 4], dest[(CG_KIT + 3) / 4];
static SsEntry slots[16];
static SsPool pool;
static uint32_t calls;
static int freeze(void *context, const uint8_t *sound)
{
    (void)context;
    uint16_t h = (uint16_t)(((uint16_t)sound[0x13a] << 8) | sound[0x13b]);
    ++calls;
    return h ? ss_share(&pool, h, h) : 0;
}
static void copy(void *context, void *d, const void *s, uint32_t n)
{
    (void)context;
    (void)cg_probe_stock_memcpy(d, s, n);
}
static CopyGuard guard = {cg_probe_mask, cg_probe_restore, freeze, copy, 0};

uint32_t cg_probe_setup(uint32_t kit)
{
    for (uint32_t i = 0; i < (CG_KIT + 3) / 4; ++i) {
        ((volatile uint32_t *)source)[i] = 0;
        ((volatile uint32_t *)dest)[i] = 0;
    }
    if (ss_init(&pool, slots, 16))
        return 1;
    uint8_t bytes[SC_BYTES];
    for (uint32_t i = 0; i < SC_BYTES; ++i)
        bytes[i] = (uint8_t)i;
    uint32_t count = kit ? 16 : 1;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t reserved = 0, handle = 0;
        uint8_t *sound = (uint8_t *)source + (kit ? CG_KIT_FIRST + i * CG_SOUND : 0);
        if (ss_reserve(&pool, &reserved) || ss_create(&pool, reserved, reserved, &handle, bytes))
            return 2;
        sound[0x13a] = (uint8_t)(handle >> 8);
        sound[0x13b] = (uint8_t)handle;
    }
    calls = 0;
    return 0;
}
uint32_t cg_probe_run(uint32_t kit)
{
    return (uint32_t)cg_copy(&guard, dest, source, kit ? CG_KIT : CG_SOUND);
}
uint32_t cg_probe_verify(uint32_t kit)
{
    uint32_t count = kit ? 16 : 1;
    if (calls != count)
        return 1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *sound = (const uint8_t *)dest + (kit ? CG_KIT_FIRST + i * CG_SOUND : 0);
        uint16_t handle = (uint16_t)(((uint16_t)sound[0x13a] << 8) | sound[0x13b]);
        if (handle != i + 1 || slots[i].state != SS_FROZEN)
            return 2;
    }
    return 0;
}
