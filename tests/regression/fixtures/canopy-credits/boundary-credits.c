#include "boundary-credits.h"

void bc_init(BcCredits *c, SpPool *pool)
{
    c->pool = pool;
    for (uint32_t i = 0; i < BC_SLOTS; ++i) {
        c->slot[i].sound = 0;
        c->slot[i].generation = 0;
        c->slot[i].credit = 0;
        c->slot[i].dirty = 0;
    }
}
static int owned(const BcCredits *c, uint32_t i, uint32_t sound, uint32_t generation)
{
    return c && i < BC_SLOTS && sound && generation && c->slot[i].sound == sound &&
           c->slot[i].generation == generation;
}
int bc_bind(BcCredits *c, uint32_t i, uint32_t sound, uint32_t generation)
{
    if (!c || i >= BC_SLOTS || !sound || !generation)
        return -1;
    if (owned(c, i, sound, generation))
        return 0;
    if (c->slot[i].dirty)
        return -1;
    for (uint32_t j = 0; j < BC_SLOTS; ++j)
        if (j != i && c->slot[j].sound == sound)
            return -1;
    c->slot[i].sound = sound;
    c->slot[i].generation = generation;
    return 0;
}
int bc_unbind(BcCredits *c, uint32_t i, uint32_t sound, uint32_t generation)
{
    if (!owned(c, i, sound, generation) || c->slot[i].dirty)
        return -1;
    c->slot[i].sound = 0;
    c->slot[i].generation = 0;
    return 0;
}
int bc_replenish(BcCredits *c)
{
    if (!c || !c->pool)
        return -1;
    uint32_t missing = 0;
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        if (c->slot[i].sound && !c->slot[i].credit)
            ++missing;
    if (!missing)
        return 0;
    uint16_t next = 0;
    if (sp_reserve_batch_task(c->pool, missing, &next))
        return -1;
    for (uint32_t i = 0; i < BC_SLOTS; ++i)
        if (c->slot[i].sound && !c->slot[i].credit)
            c->slot[i].credit = next++;
    return 0;
}
int bc_can_edit(const BcCredits *c, uint32_t i, uint32_t sound, uint32_t generation)
{
    return owned(c, i, sound, generation) && c->slot[i].credit ? 0 : -1;
}
int bc_edit_committed(BcCredits *c, uint32_t i, uint32_t sound, uint32_t generation)
{
    if (bc_can_edit(c, i, sound, generation))
        return -1;
    c->slot[i].dirty = 1;
    return 0;
}
int bc_capture(BcCredits *c, uint32_t i, uint32_t sound, uint32_t generation,
               const uint8_t bytes[SC_BYTES], uint16_t *handle)
{
    if (!bytes || !handle || bc_can_edit(c, i, sound, generation) || !c->slot[i].dirty)
        return -1;
    uint16_t credit = c->slot[i].credit;
    if (sp_publish(c->pool, credit, bytes))
        return -1;
    c->slot[i].credit = 0;
    c->slot[i].dirty = 0;
    *handle = credit;
    return 0;
}
