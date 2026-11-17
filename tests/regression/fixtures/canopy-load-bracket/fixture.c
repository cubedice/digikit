/* ColdFire test of the real adapter bracket and stock DTII deserializer with
 * modeled snapshot/admission. Never registered by the production adapter. */
#include <stdint.h>
#include "../canopy-detach/detach.h"
#include "../../../../modules/core/adapters/instrument-play/src/cf/instances.h"
extern uint32_t canopy_bracket_entry(uint32_t, uint32_t);
extern void instrument_sound_proof_set_load(int (*)(uint32_t, uint32_t, uint32_t, uint32_t),
                                            int (*)(uint32_t, uint32_t));
extern uint32_t tp_probe_mask(void *);
extern void tp_probe_restore(void *, uint32_t);
uint32_t canopy_bracket_force_partial;
volatile uint32_t pp_position[16];
uint32_t dkrt_irq_mask(void)
{
    return tp_probe_mask(0);
}
void dkrt_irq_restore(uint32_t sr)
{
    tp_probe_restore(0, sr);
}
uint32_t dkrt_instrument_type(void)
{
    return 1;
}
uint32_t ip_project(void)
{
    return 0;
}
uint32_t ip_kit(uint32_t p)
{
    (void)p;
    return 0;
}
void ip_refresh(uint32_t source, uint32_t track)
{
    (void)source;
    (void)track;
}
uint32_t pp_current_pattern(void)
{
    return 0;
}
void instrument_mod_defaults(uint32_t source)
{
    (void)source;
}
static uint32_t resets;
void instrument_sound_suspend(uint32_t instance)
{
    if (!instance)
        ++resets;
}
static SpPool pool;
static SpEntry slab[SP_PER_SLAB];
static uint32_t allocated;
static uint16_t credit, incoming;
static uint32_t sound[(DT_SOUND_SIZE + 3) / 4], undo[(DT_SOUND_SIZE + 3) / 4],
    saved[(DT_SOUND_SIZE + 3) / 4], record[280];
static CpPreset current;
static Detach transaction;
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
static uint32_t mask(void *x)
{
    return tp_probe_mask(x);
}
static void restore(void *x, uint32_t sr)
{
    tp_probe_restore(x, sr);
}
static int owned(void *x, uint32_t id, uint32_t gen, int loading)
{
    (void)x;
    return ip_admission[1] == id && ip_admission[4] == gen &&
           ip_admission[6] == (uint32_t)loading && ip_admission[0] == (loading ? 0u : 1u);
}
static int snapshot(void *x, uint32_t id, uint32_t gen, CpPreset *p)
{
    (void)x;
    if (!owned(0, id, gen, 0))
        return -1;
    for (uint32_t i = 0; i < sizeof(*p); ++i)
        ((volatile uint8_t *)p)[i] = ((const volatile uint8_t *)&current)[i];
    return 0;
}
/* ip_sound_begin itself performs the real admission invalidation and suspend
 * immediately after the pre-capture callback returns. */
static void begin(void *x, uint32_t id, uint32_t gen)
{
    (void)x;
    (void)id;
    (void)gen;
}
static void end(void *x)
{
    (void)x;
}
static int admit_apply(void *x, uint32_t id, uint32_t gen, const CpPreset *p)
{
    (void)x;
    if (!owned(0, id, gen, 1))
        return -1;
    for (uint32_t i = 0; i < sizeof(*p); ++i)
        ((volatile uint8_t *)&current)[i] = ((const volatile uint8_t *)p)[i];
    /* Modeled atomic admission: real firmware does not yet have this hook. */
    ip_admission[6] = 0;
    ip_admission[0] = 1;
    return 0;
}
static DtOps ops = {mask, restore, owned, snapshot, begin, end, admit_apply, 0};
static int before(uint32_t slot, uint32_t destination, uint32_t src, uint32_t gen)
{
    (void)src;
    if (slot || destination != (uint32_t)(uintptr_t)sound)
        return -1;
    return dt_begin(&transaction, &pool, &ops, (uint8_t *)sound, (uint8_t *)undo, destination, gen,
                    credit, incoming);
}
static int after(uint32_t cookie, uint32_t result)
{
    return cookie == 1 ? dt_finish(&transaction, (result & 255u) != 0) : -1;
}
uint32_t canopy_bracket_case(uint32_t kind)
{
    for (uint32_t i = 0; i < sizeof(current); ++i)
        ((volatile uint8_t *)&current)[i] = 0;
    for (uint32_t i = 0; i < sizeof(transaction); ++i)
        ((volatile uint8_t *)&transaction)[i] = 0;
    for (uint32_t i = 0; i < IP_INSTANCES * 8; ++i)
        ip_admission[i] = 0;
    for (uint32_t i = 0; i < (DT_SOUND_SIZE + 3) / 4; ++i)
        sound[i] = saved[i] = 0x12121212;
    for (uint32_t i = 0; i < 280; ++i)
        record[i] = 0;
    record[0] = 0xbeefbace;
    record[1] = 3;
    record[2] = 0x23456789;
    ((uint8_t *)record)[0xd2] = 1;
    current.clock = CP_ARP_CLOCK;
    current.bpm = CP_ARP_BPM_RESERVED;
    current.mode = KB_ROOT;
    current.control[CP_RATE] = 1;
    current.control[CP_GATE] = 1;
    current.control[CP_WT_RATE] = 1;
    current.control[CP_STRUM] = 2;
    if (!cp_valid(&current))
        return 1;
    allocated = 0;
    credit = incoming = 0;
    resets = 0;
    canopy_bracket_force_partial = (kind == 1);
    sp_init(&pool, allocate, 0);
    sp_set_deallocator(&pool, release);
    if (sp_reserve_task(&pool, &incoming) || sp_reserve_task(&pool, &credit))
        return 2;
    uint8_t bytes[SC_BYTES];
    current.control[CP_STRUM] = 9;
    if (sc_encode(&current, bytes) || sp_publish(&pool, incoming, bytes))
        return 3;
    current.control[CP_STRUM] = 2;
    ip_admission[0] = 1;
    ip_admission[1] = (uint32_t)(uintptr_t)sound;
    ip_admission[4] = 7;
    ip_admission[5] = 1;
    ip_admission[8] = 1;
    ip_admission[9] = 0x47008000;
    ip_admission[12] = 4;
    ip_admission[13] = 1;
    if (kind == 2)
        credit = 0;
    instrument_sound_proof_set_load(before, after);
    uint32_t result = canopy_bracket_entry((uint32_t)(uintptr_t)sound, (uint32_t)(uintptr_t)record);
    instrument_sound_proof_set_load(0, 0);
    if (kind == 2) {
        if ((result & 255u) || resets || ip_admission[4] != 7 || ip_admission[6] ||
            !ip_admission[0])
            return 4;
        for (uint32_t i = 0; i < (DT_SOUND_SIZE + 3) / 4; ++i)
            if (sound[i] != saved[i])
                return 5;
    } else {
        if ((result & 255u) != (kind == 0) || resets != 1 || ip_admission[4] != 8 ||
            ip_admission[6] || !ip_admission[0])
            return 6;
        if (ip_admission[8] != 1 || ip_admission[12] != 4 || ip_admission[14])
            return 7;
        if (kind == 0) {
            if (sound[0] != record[2] || current.control[CP_STRUM] != 9 ||
                ((uint8_t *)sound)[DT_HANDLE + 1] != (uint8_t)incoming)
                return 8;
        } else {
            ((uint8_t *)saved)[DT_HANDLE] = (uint8_t)(credit >> 8);
            ((uint8_t *)saved)[DT_HANDLE + 1] = (uint8_t)credit;
            for (uint32_t i = 0; i < (DT_SOUND_SIZE + 3) / 4; ++i)
                if (sound[i] != saved[i])
                    return 9;
            if (current.control[CP_STRUM] != 2)
                return 10;
        }
    }
    return 0;
}
