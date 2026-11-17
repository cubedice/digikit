/* UNSAFE HOST-SIDE EXPERIMENT: not wired to stock firmware or a module. */
#include "preflight.h"

int canopy_load_begin(CanopyLoadPreflight *load, SpPool *pool, uint32_t count)
{
    uint16_t first;
    if (!load || !pool || load->pool || !count || count > CANOPY_PROJECT_SOUND_LIMIT)
        return -1;
    if (sp_reserve_batch_task(pool, count, &first))
        return -1;
    load->pool = pool;
    load->next = first;
    load->end = (uint32_t)first + count;
    return 0;
}

int canopy_load_publish(CanopyLoadPreflight *load, const uint8_t bytes[SC_BYTES], uint16_t *handle)
{
    if (!load || !load->pool || !bytes || !handle || load->next >= load->end)
        return -1;
    uint16_t h = (uint16_t)load->next;
    if (sp_publish(load->pool, h, bytes))
        return -1;
    ++load->next;
    *handle = h;
    return 0;
}

void canopy_load_end(CanopyLoadPreflight *load)
{
    if (!load)
        return;
    /* Only never-published credits at the current tail can be returned. A
     * concurrent reservation makes this fail closed, leaving them consumed. */
    if (load->pool && load->next && load->next < load->end)
        (void)sp_cancel_reserved_tail(load->pool, (uint16_t)load->next, load->end - load->next);
    load->pool = 0;
    load->next = 0;
    load->end = 0;
}
