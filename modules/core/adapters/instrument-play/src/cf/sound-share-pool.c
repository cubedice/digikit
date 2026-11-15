#include "sound-share-pool.h"

static SsEntry *entry(const SsPool *pool,uint16_t handle)
{
    return pool && pool->entries && handle && handle<=pool->count
        ? &pool->entries[handle-1] : 0;
}
static void write_bytes(SsEntry *e,const uint8_t *bytes)
{
    for(uint32_t i=0;i<SC_BYTES;++i) e->bytes[i]=bytes[i];
}
int ss_init(SsPool *pool,SsEntry *entries,uint32_t capacity)
{
    if(!pool || !entries || !capacity || capacity>SS_MAX_HANDLES) return -1;
    pool->entries=entries; pool->capacity=capacity; pool->count=0;
    return 0;
}
int ss_reserve(SsPool *pool,uint16_t *handle)
{
    if(!pool || !handle || !pool->entries || pool->count>=pool->capacity) return -1;
    SsEntry *e=&pool->entries[pool->count];
    e->state=SS_RESERVED; e->owner=0;
    *handle=(uint16_t)(++pool->count);
    return 0;
}
int ss_create(SsPool *pool,uint16_t reserved,uintptr_t owner,
              uint16_t *owner_handle,const uint8_t bytes[SC_BYTES])
{
    SsEntry *e=entry(pool,reserved);
    if(!e || e->state!=SS_RESERVED || !owner || !owner_handle || *owner_handle || !bytes) return -1;
    write_bytes(e,bytes); e->owner=owner; e->state=SS_EXCLUSIVE;
    *owner_handle=reserved;
    return 0;
}
int ss_share(SsPool *pool,uint16_t handle,uintptr_t owner)
{
    SsEntry *e=entry(pool,handle);
    if(!e || !owner || (e->state!=SS_FROZEN &&
        (e->state!=SS_EXCLUSIVE || e->owner!=owner))) return -1;
    e->owner=0; e->state=SS_FROZEN;
    return 0;
}
int ss_can_edit(const SsPool *pool,const uint16_t *owner_handle,uintptr_t owner,
                uint16_t expected,uint16_t reserved)
{
    if(!owner_handle || !owner || *owner_handle!=expected) return -1;
    SsEntry *old=entry(pool,expected);
    if(!old) return -1;
    if(old->state==SS_EXCLUSIVE && old->owner==owner) return 0;
    if(old->state!=SS_FROZEN) return -1;
    SsEntry *next=entry(pool,reserved);
    if(!next || next->state!=SS_RESERVED) return -1;
    return 0;
}
int ss_edit(SsPool *pool,uint16_t *owner_handle,uintptr_t owner,
            uint16_t expected,uint16_t reserved,const uint8_t bytes[SC_BYTES])
{
    if(!bytes || ss_can_edit(pool,owner_handle,owner,expected,reserved)) return -1;
    SsEntry *old=entry(pool,expected);
    if(old->state==SS_EXCLUSIVE) {
        write_bytes(old,bytes);
        return 0;
    }
    SsEntry *next=entry(pool,reserved);
    write_bytes(next,bytes); next->owner=owner; next->state=SS_EXCLUSIVE;
    *owner_handle=reserved;
    return 0;
}
int ss_read(const SsPool *pool,uint16_t handle,uint8_t out[SC_BYTES])
{
    SsEntry *e=entry(pool,handle);
    if(!e || !out || (e->state!=SS_EXCLUSIVE && e->state!=SS_FROZEN)) return -1;
    for(uint32_t i=0;i<SC_BYTES;++i) out[i]=e->bytes[i];
    return 0;
}
