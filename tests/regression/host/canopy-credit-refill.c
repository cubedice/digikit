/* Host-only modeled task/IRQ interleaving with the real selector mapper. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include "../fixtures/canopy-two-phase/credit-refill.h"
typedef struct {
    SpPool pool; BcCredits credits; TpSync sync; CreditRefill refill;
    int task_lock,in_irq,inject,fail;
    unsigned interrupts,allocations;
} Harness;
static int task_ready(void *x) { Harness *h=x; return !masked && !h->in_irq; }
static int enter(void *x) { Harness *h=x; if(h->task_lock) return -1; h->task_lock=1; return 0; }
static void leave(void *x) { Harness *h=x; assert(h->task_lock); h->task_lock=0; }
static uint32_t mask(void *x) { (void)x; return dkrt_irq_mask(); }
static void restore(void *x,uint32_t sr) { (void)x; dkrt_irq_restore(sr); }
static int prepare(void *x,const CpPreset *p)
{
    Harness *h=x; uint8_t bytes[SC_BYTES]; assert(masked);
    return bc_can_edit(&h->credits,0,0x123400,7) || sc_encode(p,bytes);
}
static void attach(void *x)
{
    Harness *h=x; assert(masked && !bc_edit_committed(&h->credits,0,0x123400,7));
}
static void interrupt_during_allocation(Harness *h)
{
    assert(!masked && h->task_lock && h->refill.inflight);
    h->in_irq=1; uint32_t sr=dkrt_irq_mask();
    MiEvent input[2]={{0},{0}},out[8];
    input[0].epoch=mi_ledger.epoch; input[0].generation=mi_ledger.generations[0];
    input[0].token=100; input[0].kind=MI_NOTE_ON; input[0].note=25; input[0].value=90;
    input[1]=input[0]; input[1].token=200; input[1].note=60;
    int refused=0;
    int n=instrument_sound_proof_midi(0,0x123400,7,mi_ledger.epoch,mi_ledger.generations[0],
        input,2,out,prepare,attach,h,&refused);
    assert(n>0 && !refused && keyboard.type==1 && h->credits.slot[0].dirty);
    /* Releasing the musical note remains valid while allocation is in flight. */
    input[1].kind=MI_NOTE_OFF;
    assert(instrument_sound_proof_midi(0,0x123400,7,mi_ledger.epoch,mi_ledger.generations[0],
        &input[1],1,out,prepare,attach,h,&refused)>0 && !refused);
    for(uint32_t i=0;i<MI_KEYS;++i) assert(keyboard.keys[i].token!=200 || !keyboard.keys[i].held);
    /* Replacement is allowed only while that slot is clean. Leave the fourth
     * slot unbound so one of the pending credits must become a spare. */
    assert(!bc_unbind(&h->credits,1,0x124400,8));
    assert(!bc_bind(&h->credits,1,0xabc000,20));
    assert(!bc_unbind(&h->credits,3,0x126400,10));
    dkrt_irq_restore(sr); h->in_irq=0; ++h->interrupts;
}
static void *allocate(uint32_t n,void *x)
{
    Harness *h=x; assert(!masked && !h->in_irq && h->task_lock); ++h->allocations;
    if(h->inject) { h->inject=0; interrupt_during_allocation(h); }
    return h->fail ? 0 : calloc(1,n);
}
static void release(void *p,void *x) { Harness *h=x; assert(!masked && h->task_lock); free(p); }
static void init(Harness *h)
{
    memset(h,0,sizeof(*h)); sp_init(&h->pool,allocate,h); sp_set_deallocator(&h->pool,release);
    h->sync=(TpSync){task_ready,enter,leave,mask,restore,h};
    bc_init(&h->credits,&h->pool); cr_init(&h->refill,&h->credits,&h->sync);
    for(uint32_t i=0;i<BC_SLOTS;++i) assert(!bc_bind(&h->credits,i,0x123400+i*0x1000,7+i));
    uint16_t first=0; assert(!tp_reserve_batch(&h->pool,&h->sync,1,&first) && first==1);
    h->credits.slot[0].credit=first;
    /* Unowned padding forces the next reservation across a slab boundary. */
    while(h->pool.count<SP_PER_SLAB) {
        uint32_t n=SP_PER_SLAB-h->pool.count; if(n>TP_BATCH_MAX) n=TP_BATCH_MAX;
        assert(!tp_reserve_batch(&h->pool,&h->sync,n,&first));
    }
    cur->mode=keyboard.mode=KB_ROOT; cur->type=keyboard.type=0;
    keyboard.selector_base=24; keyboard.selector_channel=MI_NONE;
    kb_reset(&keyboard,mi_ledger.epoch,mi_ledger.generations[0]);
}
static void destroy(Harness *h)
{ for(uint32_t i=0;i<SP_SLABS;++i) if(h->pool.slab[i]) free(h->pool.slab[i]); }
static void check_accounting(Harness *h)
{
    /* The three post-padding handles are each held once, either by a current
     * owner or as a spare; no stale generation is attached. */
    unsigned seen[SP_PER_SLAB+4]={0};
    for(uint32_t i=0;i<BC_SLOTS;++i) if(h->credits.slot[i].credit)
        ++seen[h->credits.slot[i].credit];
    for(uint32_t i=0;i<h->refill.spare_count;++i) ++seen[h->refill.spare[i]];
    assert(seen[1]==1);
    for(uint32_t i=SP_PER_SLAB+1;i<=SP_PER_SLAB+3;++i) assert(seen[i]==1);
    assert(h->credits.slot[1].sound==0xabc000 && h->credits.slot[1].generation==20);
}
static void complete_after_allocation(Harness *h)
{
    assert(h->interrupts==1 && keyboard.type==1 && h->credits.slot[0].dirty);
    assert(!h->refill.inflight && !h->task_lock && !masked);
    assert(h->pool.count==SP_PER_SLAB+3 && h->refill.spare_count==1);
    assert(!h->credits.slot[3].sound && !h->credits.slot[3].credit);
    check_accounting(h);
    /* A later owner consumes the spare without allocating another entry. */
    assert(!bc_bind(&h->credits,3,0xddd000,30));
    unsigned calls=h->allocations; assert(!cr_replenish(&h->refill));
    assert(!h->refill.spare_count && h->allocations==calls && h->pool.count==SP_PER_SLAB+3);
    check_accounting(h);
    uint8_t bytes[SC_BYTES],readback[SC_BYTES]; CpPreset p; uint16_t published=0;
    preset_snapshot(cur,&p); assert(!sc_encode(&p,bytes));
    uint32_t sr=dkrt_irq_mask();
    assert(!bc_capture(&h->credits,0,0x123400,7,bytes,&published));
    assert(published==1 && !sp_read(&h->pool,published,readback) && !memcmp(bytes,readback,sizeof(bytes)));
    dkrt_irq_restore(sr);
}
int main(void)
{
    mi_ledger.epoch=1; mi_ledger.generations[0]=1; instrument_play_tick();
    ip_admission[0]=1; ip_admission[1]=0x123400; ip_admission[4]=7; ip_admission[5]=admitted;
    instrument_select_track(0);
    Harness h; init(&h); h.inject=1;
    assert(!cr_replenish(&h.refill)); complete_after_allocation(&h); destroy(&h);

    init(&h); h.inject=1; h.fail=1;
    assert(cr_replenish(&h.refill)<0 && h.pool.count==SP_PER_SLAB);
    assert(h.interrupts==1 && h.credits.slot[0].dirty && !h.refill.inflight && !h.task_lock && !masked);
    assert(!h.refill.spare_count && !h.credits.slot[1].credit && !h.credits.slot[2].credit);
    h.fail=0; assert(!cr_replenish(&h.refill));
    /* One owner departed during failure, so retry requires only two credits. */
    assert(h.pool.count==SP_PER_SLAB+2 && !h.refill.spare_count);
    assert(h.credits.slot[1].sound==0xabc000 && h.credits.slot[1].generation==20);
    assert(h.credits.slot[1].credit && h.credits.slot[2].credit && h.credits.slot[0].dirty);
    destroy(&h); return 0;
}
