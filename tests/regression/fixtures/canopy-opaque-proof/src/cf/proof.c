/* UNSAFE, synthetic emulator test only. No real Sound is attached or saved. */
#include <stdint.h>
#include "../../../canopy-raw-store/raw-store.h"
extern uint8_t *fw_alloc(uint32_t);
extern void fw_free(uint8_t *);

/* 0 arm, 1 completed, 2 setup error, 3..6 case masks, 7 unexpected
 * callbacks. A mask of 31 proves stock load, classified attach, stock save,
 * restored bytes, and unchanged non-extension bytes, respectively. */
volatile uint32_t canopy_opaque_result[8];
static SpPool pool;
static CrsBatch batch;
static uint8_t seed[CRS_RECORD_BYTES] __attribute__((aligned(4)));
static uint8_t input[CRS_RECORD_BYTES] __attribute__((aligned(4)));
static uint8_t output[CRS_RECORD_BYTES] __attribute__((aligned(4)));
static uint8_t stock_output[CRS_RECORD_BYTES] __attribute__((aligned(4)));
static uint8_t sound[0x450] __attribute__((aligned(4)));
static CrsKind kind;
static uint32_t active;
static uint16_t *sound_handle(void)
{
    return (uint16_t *)(void *)(sound + 0x13a);
}
static void *allocate(uint32_t n, void *ctx)
{
    (void)ctx;
    return fw_alloc(n);
}
static void release(void *p, void *ctx)
{
    (void)ctx;
    fw_free((uint8_t *)p);
}
static void copy_bytes(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        ((volatile uint8_t *)d)[i] = s[i];
}
static uint32_t offset(uint32_t i)
{
    return 0x1cu + 2u * (i < 8 ? 4u * (i + 1u) : 77u + i - 8u);
}
static int is_extension(uint32_t i)
{
    for (uint32_t j = 0; j < 21; ++j)
        if (i == offset(j) || i == offset(j) + 1)
            return 1;
    return 0;
}

void canopy_opaque_saved(uint8_t *record, uint32_t source, uint32_t result)
{
    if (!active || source != (uint32_t)(uintptr_t)sound || record != output)
        return;
    if (!(result & 255u))
        return;
    copy_bytes(stock_output, record, CRS_RECORD_BYTES);
    if (!crs_write(&pool, *sound_handle(), record, CRS_RECORD_BYTES))
        canopy_opaque_result[2 + active] |= 4;
}
static void run(void)
{
    sp_init(&pool, allocate, 0);
    sp_set_deallocator(&pool, release);
    if (crs_begin(&batch, &pool, 4)) {
        canopy_opaque_result[2] = 1;
        return;
    }
    for (uint32_t which = 0; which < 4; ++which) {
        uint8_t raw[SC_BYTES];
        for (uint32_t i = 0; i < SC_BYTES; ++i)
            ((volatile uint8_t *)raw)[i] = 0;
        raw[0] = 'C';
        raw[1] = 'S';
        raw[2] = 1;
        raw[40] = 0x76;
        raw[41] = 0x02;
        if (which == 1)
            raw[2] = 2;
        if (which == 2)
            raw[3] = 1;
        if (which == 3)
            raw[0] = 'X';
        copy_bytes(input, seed, CRS_RECORD_BYTES);
        for (uint32_t i = 0; i < 21; ++i) {
            input[offset(i)] = raw[2 * i];
            input[offset(i) + 1] = raw[2 * i + 1];
        }
        for (uint32_t i = 0; i < sizeof(sound); ++i)
            ((volatile uint8_t *)sound)[i] = 0;
        for (uint32_t i = 0; i < sizeof(output); ++i)
            ((volatile uint8_t *)output)[i] = 0xa5;
        *sound_handle() = 0;
        kind = CRS_EMPTY;
        active = which + 1;
        uint32_t loaded =
            ((uint32_t (*)(void *, const void *))(uintptr_t)0x400db3b2u)(sound, input);
        if (loaded & 255u)
            canopy_opaque_result[3 + which] |= 1;
        if (kind == (CrsKind)(which + 1) && *sound_handle())
            canopy_opaque_result[3 + which] |= 2;
        ((uint32_t (*)(void *, void *, uint32_t))(uintptr_t)0x400dba94u)(output, sound, 0);
        uint32_t exact = 1, other = 1;
        for (uint32_t i = 0; i < 21; ++i)
            if (output[offset(i)] != raw[2 * i] || output[offset(i) + 1] != raw[2 * i + 1])
                exact = 0;
        for (uint32_t i = 0; i < CRS_RECORD_BYTES; ++i)
            if (!is_extension(i) && output[i] != stock_output[i])
                other = 0;
        if (exact)
            canopy_opaque_result[3 + which] |= 8;
        if (other)
            canopy_opaque_result[3 + which] |= 16;
        active = 0;
    }
    if (crs_end(&batch))
        canopy_opaque_result[2] = 2;
}
void canopy_opaque_loaded(uint32_t destination, const uint8_t *record)
{
    if (active) {
        if (destination != (uint32_t)(uintptr_t)sound || record != input ||
            crs_attach(&batch, record, CRS_RECORD_BYTES, sound_handle(), &kind))
            ++canopy_opaque_result[7];
        return;
    }
    if (canopy_opaque_result[0] != 1 || !record)
        return;
    canopy_opaque_result[0] = 0;
    copy_bytes(seed, record, CRS_RECORD_BYTES);
    run();
    canopy_opaque_result[1] = 1;
}
