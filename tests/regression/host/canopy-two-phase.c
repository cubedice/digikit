/* Cooperative single-core preemption test. This does not simulate a true
 * scheduler, DMA, ColdFire memory barriers, or timing. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../fixtures/canopy-two-phase/two-phase.h"
typedef struct {
    SpPool pool; TpSync sync;
    int masked,task_lock,in_irq,fail,inject_irq,inject_stale;
    unsigned allocations,frees,interrupts,nested_refused;
    uint16_t old;
} Harness;
static int ready(void *x) { Harness *h=x; return !h->masked && !h->in_irq; }
static int enter(void *x) { Harness *h=x; if(h->task_lock) return -1; h->task_lock=1; return 0; }
static void leave(void *x) { Harness *h=x; assert(h->task_lock); h->task_lock=0; }
static uint32_t mask(void *x) { Harness *h=x; uint32_t old=(uint32_t)h->masked; h->masked=1; return old; }
static void restore(void *x,uint32_t old) { ((Harness *)x)->masked=(int)old; }
static void *allocate(uint32_t bytes,void *x)
{
    Harness *h=x; assert(!h->masked && !h->in_irq && h->task_lock); ++h->allocations;
    if(h->inject_irq) {
        uint8_t in[SC_BYTES],out[SC_BYTES]; memset(in,0x63,sizeof(in));
        assert(h->pool.count==SP_PER_SLAB && !h->pool.slab[1]);
        h->in_irq=1; uint32_t sr=mask(h);
        assert(!sp_publish(&h->pool,h->old,in) && !sp_read(&h->pool,h->old,out));
        assert(!memcmp(in,out,sizeof(in))); restore(h,sr); h->in_irq=0; ++h->interrupts;
        /* Reentrant task reservation is rejected while the original task
         * owns its lock; an IRQ call is rejected by task_ready as well. */
        uint16_t value=0;
        assert(tp_reserve_batch(&h->pool,&h->sync,1,&value)<0 && !value);
        ++h->nested_refused;
    }
    if(h->inject_stale) ++h->pool.count; /* deliberately violate lock contract */
    return h->fail ? 0 : calloc(1,bytes);
}
static void release(void *p,void *x)
{ Harness *h=x; assert(!h->masked && h->task_lock); ++h->frees; free(p); }
static void init(Harness *h)
{
    memset(h,0,sizeof(*h)); sp_init(&h->pool,allocate,h); sp_set_deallocator(&h->pool,release);
    h->sync=(TpSync){ready,enter,leave,mask,restore,h};
    /* Fill exactly one slab through the task-side protocol in <=4 batches. */
    uint16_t first=0;
    while(h->pool.count<SP_PER_SLAB) {
        uint32_t n=SP_PER_SLAB-h->pool.count; if(n>TP_BATCH_MAX) n=TP_BATCH_MAX;
        assert(!tp_reserve_batch(&h->pool,&h->sync,n,&first));
        if(!h->old) h->old=first;
    }
    assert(h->pool.count==SP_PER_SLAB && h->old==1);
}
static void destroy(Harness *h)
{ for(uint32_t i=0;i<SP_SLABS;++i) if(h->pool.slab[i]) free(h->pool.slab[i]); }
int main(void)
{
    Harness h; init(&h);
    h.inject_irq=1; uint16_t first=0;
    assert(!tp_reserve_batch(&h.pool,&h.sync,4,&first));
    assert(first==SP_PER_SLAB+1 && h.pool.count==SP_PER_SLAB+4);
    assert(h.interrupts==1 && h.nested_refused==1 && !h.task_lock && !h.masked);
    uint8_t bytes[SC_BYTES]; memset(bytes,0x11,sizeof(bytes));
    uint32_t sr=mask(&h); assert(!sp_publish(&h.pool,first,bytes)); restore(&h,sr);
    h.masked=1; uint16_t unchanged=0x4444;
    assert(tp_reserve_batch(&h.pool,&h.sync,1,&unchanged)<0 && unchanged==0x4444 && h.masked);
    h.masked=0; h.in_irq=1;
    assert(tp_reserve_batch(&h.pool,&h.sync,1,&unchanged)<0 && unchanged==0x4444);
    h.in_irq=0; destroy(&h);

    init(&h); h.fail=1; first=0x4444;
    assert(tp_reserve_batch(&h.pool,&h.sync,4,&first)<0 && first==0x4444);
    assert(h.pool.count==SP_PER_SLAB && !h.pool.slab[1] && !h.task_lock && !h.masked);
    h.fail=0; assert(!tp_reserve_batch(&h.pool,&h.sync,4,&first));
    assert(first==SP_PER_SLAB+1 && h.pool.count==SP_PER_SLAB+4); destroy(&h);

    init(&h); h.inject_stale=1; first=0x4444;
    assert(tp_reserve_batch(&h.pool,&h.sync,4,&first)<0 && first==0x4444);
    assert(h.pool.count==SP_PER_SLAB+1 && !h.pool.slab[1] && h.frees==1);
    assert(!h.task_lock && !h.masked); destroy(&h);
    return 0;
}
