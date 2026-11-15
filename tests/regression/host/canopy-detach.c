/* Host test of the real instrument snapshot/suspend/restore with a modeled
 * admission hook; no stock deserializer or real firmware scheduling here. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#undef phase
#include <assert.h>
#include <stdlib.h>
#include "../fixtures/canopy-detach/detach.h"
static void *allocate(uint32_t n,void *x) { (void)x; assert(!masked); return calloc(1,n); }
static void release(void *p,void *x) { (void)x; free(p); }
static uint32_t lock(void *x) { (void)x; return dkrt_irq_mask(); }
static void unlock(void *x,uint32_t sr) { (void)x; dkrt_irq_restore(sr); }
static int owned(void *x,uint32_t id,uint32_t gen,int loading)
{
    (void)x; assert(masked);
    return ip_admission[1]==id && ip_admission[4]==gen &&
        ip_admission[0]==(loading ? 0u : 1u) && ip_admission[6]==(uint32_t)loading;
}
static int snapshot(void *x,uint32_t id,uint32_t gen,CpPreset *p)
{
    (void)x; assert(masked); uint32_t actual=0;
    return instrument_sound_snapshot(0,id,p,&actual) || actual!=gen;
}
static void begin(void *x,uint32_t id,uint32_t gen)
{
    (void)x; assert(owned(0,id,gen,0));
    ++ip_admission[6]; ip_admission[0]=0; ++ip_admission[4];
    instrument_sound_suspend(0);
}
static void end(void *x) { (void)x; assert(masked && ip_admission[6]==1); --ip_admission[6]; }
static int admit_apply(void *x,uint32_t id,uint32_t gen,const CpPreset *p)
{
    (void)x; assert(masked && !ip_admission[0] && !ip_admission[6]);
    if(ip_admission[1]!=id || ip_admission[4]!=gen) return -1;
    ip_admission[0]=1;
    if(instrument_sound_restore(0,id,gen,p)) { ip_admission[0]=0; return -1; }
    return 0;
}
static uint16_t get_handle(const uint8_t *s)
{ return (uint16_t)((uint16_t)s[DT_HANDLE]<<8 | s[DT_HANDLE+1]); }
int main(void)
{
    mi_ledger.epoch=1; mi_ledger.generations[0]=1; instrument_play_tick();
    ip_admission[0]=1; ip_admission[1]=0x123400; ip_admission[4]=7; ip_admission[5]=admitted;
    cur->mode=keyboard.mode=KB_ROOT; instrument_select_track(0);
    SpPool pool; sp_init(&pool,allocate,0); sp_set_deallocator(&pool,release);
    uint16_t incoming=0,credit=0;
    assert(!sp_reserve_task(&pool,&incoming) && !sp_reserve_task(&pool,&credit));
    CpPreset old,newp,check; uint8_t bytes[SC_BYTES]; preset_snapshot(cur,&old);
    newp=old; newp.control[CP_STRUM]=7; newp.control[CP_CHORD]=3; newp.control[CP_VOICING]=2;
    assert(cp_valid(&newp) && !sc_encode(&newp,bytes) && !sp_publish(&pool,incoming,bytes));
    uint8_t sound[DT_SOUND_SIZE],undo[DT_SOUND_SIZE],before[DT_SOUND_SIZE];
    for(uint32_t i=0;i<DT_SOUND_SIZE;++i) sound[i]=(uint8_t)(i*13u);
    memcpy(before,sound,sizeof(before));
    DtOps ops={lock,unlock,owned,snapshot,begin,end,admit_apply,0}; Detach t={0};
    assert(dt_begin(&t,&pool,&ops,sound,undo,0x123400,6,credit,incoming)<0);
    assert(!masked && ip_admission[0] && !ip_admission[6] && !memcmp(sound,before,sizeof(sound)));
    assert(!dt_begin(&t,&pool,&ops,sound,undo,0x123400,7,credit,incoming));
    assert(t.phase==DT_PENDING && !ip_admission[0] && ip_admission[4]==8 && ip_admission[6]==1);
    assert(get_handle(undo)==credit && !sp_read(&pool,credit,bytes) && !sc_decode(&check,bytes));
    assert(check.control[CP_STRUM]==old.control[CP_STRUM]);
    /* The real mapper rejects access while the in-place load is underway. */
    CpPreset rejected; uint32_t gen=0;
    assert(instrument_sound_snapshot(0,0x123400,&rejected,&gen)<0);
    memset(sound,0x55,sizeof(sound));
    assert(!dt_finish(&t,1) && !masked && t.phase==DT_DONE);
    assert(ip_admission[0] && !ip_admission[6] && ip_admission[4]==8);
    assert(get_handle(sound)==incoming && cur->pad_strum==7 && keyboard.type==3);

    /* An interrupted partial write restores the whole saved Sound and its
     * old settings before modeled readmission. */
    assert(!sp_reserve_task(&pool,&credit));
    memcpy(before,sound,sizeof(before)); Detach p={0};
    assert(!dt_begin(&p,&pool,&ops,sound,undo,0x123400,8,credit,incoming));
    memset(sound,0xaa,32); assert(!dt_finish(&p,0));
    before[DT_HANDLE]=(uint8_t)(credit>>8); before[DT_HANDLE+1]=(uint8_t)credit;
    assert(!memcmp(sound,before,sizeof(sound)) && cur->pad_strum==7 && keyboard.type==3);
    assert(ip_admission[0] && !ip_admission[6] && ip_admission[4]==9);

    /* If the destination identity/generation changes during an overwrite,
     * quarantine it and never copy the old backup into that newer owner. */
    assert(!sp_reserve_task(&pool,&credit)); Detach stale={0};
    assert(!dt_begin(&stale,&pool,&ops,sound,undo,0x123400,9,credit,incoming));
    memset(sound,0x33,sizeof(sound)); memcpy(before,sound,sizeof(sound));
    ++ip_admission[4]; assert(dt_finish(&stale,0)<0 && stale.phase==DT_QUARANTINED);
    assert(!memcmp(sound,before,sizeof(sound)) && !ip_admission[0] && ip_admission[6]==1 && !masked);
    for(uint32_t i=0;i<SP_SLABS;++i) if(pool.slab[i]) free(pool.slab[i]);
    return 0;
}
