/* DT2 1.17 CANOPY Sound persistence: the C side of the stock hooks in
 * sound-persist.s and sound-load.s.
 *
 * The record extension travels in the Sound record (sound-record.h); in RAM
 * it lives in the association store (sound-assoc.h), never in Sound memory.
 * Hooks may run in any task and in interrupts. Every store update happens
 * with interrupts masked. The store's memory comes from the firmware heap
 * once, in task context with interrupts enabled (the first Sound load or UI
 * tick); until it exists every hook passes through unchanged. */
#if defined(__m68k__) && defined(__GNUC__)
/* No implicit memcpy/memset calls: this object links without libc. */
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif
#include <stdint.h>
#include "instances.h"
#include "sound-persist.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern uint8_t *fw_alloc(uint32_t);
extern void fw_free(uint8_t *);
/* Literals of stock code: the live kit table and the Sound pool. */
extern volatile uint32_t ip_kit_table_lit, ip_sound_pool_lit;

enum { SA_MEMORY = 0x20000 };
static SaStore store;
/* Nonzero once the store exists; the assembly hooks test it first. */
volatile uint32_t ip_sa_ready;
/* [0] store ready, [1] allocation failures, [2] temporary entries,
 * [3] extensions written to records, [4] loads with an extension,
 * [5] long copies and fills seen (most touch no Sound). */
volatile uint32_t ip_sa_info[8];
/* The store's counters, for proofs (copied on each UI tick). */
volatile uint32_t ip_sa_stats[SA_STATS];
/* A pattern can refer to the same Sound on many steps. Recheck its CRC when
 * the stock association changes and when taking the actual snapshot. */
static uint32_t pool_sequence[SA_POOL];
static uint8_t pool_validation[SA_POOL];

SaStore *ip_sa_store(void)
{
    return ip_sa_ready ? &store : 0;
}

int instrument_locked_sound(uint32_t index, uintptr_t expected, CpPreset *out, uint16_t *tune)
{
    if (index >= SA_POOL || (out ? (!expected || !tune) : (expected || tune)))
        return 0;
    uint8_t payload[SR_BYTES];
    CpPreset saved;
    uint32_t type = dkrt_instrument_type(), seq;
    uint32_t sr = dkrt_irq_mask();
    SaStore *s = ip_sa_store();
    uintptr_t sound = s ? s->pool + index * SA_SOUND : 0;
    if (!sound || (expected && expected != sound) || !type ||
        *(const uint8_t *)(sound + 0xa2) != type) {
        dkrt_irq_restore(sr);
        return 0;
    }
    seq = sa_seq(s, sound);
    if (!out && pool_validation[index] && pool_sequence[index] == seq) {
        int valid = pool_validation[index] == 2;
        dkrt_irq_restore(sr);
        return valid;
    }
    int have = sa_get(s, sound, payload, 0);
    uint16_t raw_tune = out ? (uint16_t)instrument_sound_raw_tune(sound) : 0;
    dkrt_irq_restore(sr);
    int valid = have && !sr_decode(out ? out : &saved, payload);
    sr = dkrt_irq_mask();
    if (sa_seq(s, sound) == seq) {
        pool_sequence[index] = seq;
        pool_validation[index] = valid ? 2 : 1;
    }
    dkrt_irq_restore(sr);
    if (valid && out)
        *tune = raw_tune;
    return valid;
}

static int ram(uint32_t p)
{
    return p >= 0x40000000u && p < 0x48000000u;
}

SaStore *ip_sa_ensure(void)
{
    if (ip_sa_ready)
        return &store;
    uint32_t sr = dkrt_irq_mask();
    dkrt_irq_restore(sr);
    if (sr & 0x0700u)
        return 0; /* an interrupt, or interrupts masked */
    uint32_t kits = ip_kit_table_lit, pool = ip_sound_pool_lit;
    if (!ram(kits) || !ram(pool))
        return 0;
    uint8_t *memory = fw_alloc(SA_MEMORY);
    if (!memory) {
        ++ip_sa_info[1];
        return 0;
    }
    SaStore fresh;
    uint32_t temps = sa_init(&fresh, kits, pool, memory, SA_MEMORY);
    sr = dkrt_irq_mask();
    int mine = !ip_sa_ready && temps;
    if (mine) {
        for (uint32_t i = 0; i < sizeof(store); ++i)
            ((volatile uint8_t *)&store)[i] = ((const uint8_t *)&fresh)[i];
        ip_sa_ready = 1;
        ip_sa_info[0] = 1;
        ip_sa_info[2] = temps;
    }
    dkrt_irq_restore(sr);
    if (!mine)
        fw_free(memory);
    return ip_sa_ready ? &store : 0;
}

void ip_sa_tick(void)
{
    if (!ip_sa_ensure())
        return;
    uint32_t sr = dkrt_irq_mask();
    for (uint32_t i = 0; i < SA_STATS; ++i)
        ip_sa_stats[i] = store.stats[i];
    dkrt_irq_restore(sr);
}

/* Sound loader: before the stock body, keep the record's extension. */
void ip_sa_load_begin(uint32_t destination, uint32_t record, uint8_t *keep)
{
    for (uint32_t i = 0; i < SR_BYTES; ++i)
        keep[i] = 0;
    if (!destination || !record || *(const uint32_t *)(uintptr_t)(record + 4) != 3)
        return;
    ip_sa_ensure();
    sr_gather((const uint8_t *)(uintptr_t)record, keep);
}
/* After a successful stock load the Sound has exactly that extension (or
 * none). A failed load wrote nothing and changes nothing. */
void ip_sa_load_end(uint32_t destination, uint32_t result, const uint8_t *keep)
{
    if (!(result & 0xffu) || !destination || !ip_sa_ready)
        return;
    int present = sr_present(keep);
    uint32_t sr = dkrt_irq_mask();
    sa_replaced(&store, destination, present ? keep : 0);
    if (present)
        ++ip_sa_info[4];
    dkrt_irq_restore(sr);
}

/* Sound writer: the stock body zero-filled the extension positions. */
extern int rtm_can_save(uint32_t sound) __attribute__((weak));
uint32_t ip_sa_saved(uint32_t record, uint32_t sound, uint32_t result)
{
    if (!(result & 0xffu) || !record || !sound || !ip_sa_ready)
        return result;
    if (rtm_can_save && !rtm_can_save(sound))
        return 0;
    instrument_sound_flush(sound, sound + SA_SOUND);
    uint8_t payload[SR_BYTES];
    uint32_t sr = dkrt_irq_mask();
    int present = sa_get(&store, sound, payload, 0);
    if (present)
        ++ip_sa_info[3];
    dkrt_irq_restore(sr);
    if (present)
        sr_scatter((uint8_t *)(uintptr_t)record, payload);
    return result;
}

extern void tk_pattern_copied(uint32_t, uint32_t, uint32_t) __attribute__((weak));
extern void tk_pattern_cleared(uint32_t, uint32_t) __attribute__((weak));

/* memcpy of at least one Sound's length, before and after the stock copy. */
void ip_sa_copy_before(uint32_t destination, uint32_t source, uint32_t length)
{
    (void)destination;
    if (ip_sa_ready)
        instrument_sound_flush(source, source + length);
}
void ip_sa_copy_after(uint32_t destination, uint32_t source, uint32_t length)
{
    if (tk_pattern_copied && (length == 0x1db8cu || length == 0x6a5u))
        tk_pattern_copied(destination, source, length);
    if (!ip_sa_ready)
        return;
    uint32_t sr = dkrt_irq_mask();
    ++ip_sa_info[5];
    sa_copied(&store, destination, source, length);
    dkrt_irq_restore(sr);
}
/* memset or bzero of at least one Sound's length, after the stock fill. */
void ip_sa_cleared(uint32_t destination, uint32_t length)
{
    if (tk_pattern_cleared && length == 0x1db8cu)
        tk_pattern_cleared(destination, length);
    if (!ip_sa_ready)
        return;
    uint32_t sr = dkrt_irq_mask();
    ++ip_sa_info[5];
    sa_cleared(&store, destination, length);
    dkrt_irq_restore(sr);
}
/* The Sound initializer or a machine-type change: CANOPY defaults. */
void ip_sa_reset(uint32_t sound)
{
    if (!ip_sa_ready || !sound)
        return;
    uint32_t sr = dkrt_irq_mask();
    sa_replaced(&store, sound, 0);
    dkrt_irq_restore(sr);
}
