#include "detach.h"
#include <stdint.h>
static void copy_bytes(uint8_t *d, const uint8_t *s)
{
    for (uint32_t i = 0; i < DT_SOUND_SIZE; ++i)
        ((volatile uint8_t *)d)[i] = s[i];
}
static void handle(uint8_t *s, uint16_t h)
{
    s[DT_HANDLE] = (uint8_t)(h >> 8);
    s[DT_HANDLE + 1] = (uint8_t)h;
}
int dt_begin(Detach *t, SpPool *pool, const DtOps *o, uint8_t *sound, uint8_t *undo, uint32_t id,
             uint32_t gen, uint16_t credit, uint16_t incoming)
{
    if (!t || t->phase != DT_EMPTY || !pool || !o || !o->mask || !o->restore || !o->owned ||
        !o->snapshot || !o->suspend_begin || !o->end || !o->admit_apply || !sound || !undo || !id ||
        !gen || gen == UINT32_MAX || !credit || !incoming)
        return -1;
    uintptr_t a = (uintptr_t)sound, b = (uintptr_t)undo;
    if (a > UINTPTR_MAX - DT_SOUND_SIZE || b > UINTPTR_MAX - DT_SOUND_SIZE ||
        (a < b + DT_SOUND_SIZE && b < a + DT_SOUND_SIZE))
        return -1;
    uint32_t sr = o->mask(o->context);
    uint8_t bytes[SC_BYTES];
    if (!o->owned(o->context, id, gen, 0) || sp_read(pool, incoming, bytes) ||
        sc_decode(&t->new_preset, bytes) || o->snapshot(o->context, id, gen, &t->old_preset) ||
        sc_encode(&t->old_preset, bytes) || sp_publish(pool, credit, bytes)) {
        o->restore(o->context, sr);
        return -1;
    }
    copy_bytes(undo, sound);
    handle(undo, credit);
    t->pool = pool;
    t->ops = o;
    t->sound = sound;
    t->undo = undo;
    t->sound_id = id;
    t->old_generation = gen;
    t->new_generation = gen + 1;
    t->old_handle = credit;
    t->new_handle = incoming;
    t->phase = DT_PENDING;
    o->suspend_begin(o->context, id, gen);
    o->restore(o->context, sr);
    return 0;
}
int dt_finish(Detach *t, int complete)
{
    if (!t || t->phase != DT_PENDING)
        return -1;
    const DtOps *o = t->ops;
    uint32_t sr = o->mask(o->context);
    if (!o->owned(o->context, t->sound_id, t->new_generation, 1)) {
        t->phase = DT_QUARANTINED;
        o->restore(o->context, sr);
        return -1;
    }
    if (complete)
        handle(t->sound, t->new_handle);
    else
        copy_bytes(t->sound, t->undo);
    o->end(o->context);
    if (o->admit_apply(o->context, t->sound_id, t->new_generation,
                       complete ? &t->new_preset : &t->old_preset)) {
        t->phase = DT_QUARANTINED;
        o->restore(o->context, sr);
        return -1;
    }
    t->phase = DT_DONE;
    o->restore(o->context, sr);
    return 0;
}
