#include "two-phase.h"
#include <stdatomic.h>
_Static_assert((unsigned)TP_BATCH_MAX<=(unsigned)SP_PER_SLAB, "batch spans more than two slabs");
typedef struct { uint32_t index; SpEntry *observed,*staged; } TpSlab;

static uint32_t mask(const TpSync *s)
{
    uint32_t sr=s->mask_irq(s->context);
    atomic_signal_fence(memory_order_seq_cst); return sr;
}
static void restore(const TpSync *s,uint32_t sr)
{
    atomic_signal_fence(memory_order_seq_cst); s->restore_irq(s->context,sr);
}
int tp_reserve_batch(SpPool *p,const TpSync *s,uint32_t count,uint16_t *first)
{
    if(!p || !s || !first || !s->task_ready || !s->enter_task || !s->leave_task || !s->mask_irq ||
       !s->restore_irq || !p->allocate || !p->release || !count || count>TP_BATCH_MAX)
        return -1;
    if(!s->task_ready(s->context)) return -1;
    if(s->enter_task(s->context)) return -1;
    /* Freestanding ColdFire links do not import libc memset. Volatile stores
     * keep the compiler from turning this initialization into an implicit
     * library call, without changing the staging semantics. */
    TpSlab slabs[2];
    volatile uint8_t *init=(volatile uint8_t *)slabs;
    for(uint32_t i=0;i<sizeof(slabs);++i) init[i]=0;
    uint32_t nslabs=0,start,end;
    int result=-1;
    if(!s->task_ready(s->context)) goto out;
    uint32_t sr=mask(s);
    start=p->count;
    if(start>SP_MAX_HANDLES || count>SP_MAX_HANDLES-start ||
       (start && !p->slab[(start-1)/SP_PER_SLAB])) {
        restore(s,sr); goto out;
    }
    end=start+count;
    for(uint32_t i=start/SP_PER_SLAB;i<=(end-1)/SP_PER_SLAB;++i) {
        slabs[nslabs].index=i; slabs[nslabs].observed=p->slab[i]; ++nslabs;
    }
    restore(s,sr);
    for(uint32_t i=0;i<nslabs;++i) if(!slabs[i].observed) {
        slabs[i].staged=(SpEntry *)p->allocate(sizeof(SpEntry)*SP_PER_SLAB,p->context);
        if(!slabs[i].staged) goto out;
    }
    sr=mask(s);
    if(p->count!=start) { restore(s,sr); goto out; }
    for(uint32_t i=0;i<nslabs;++i) if(p->slab[slabs[i].index]!=slabs[i].observed) {
        restore(s,sr); goto out;
    }
    for(uint32_t i=0;i<nslabs;++i) if(slabs[i].staged) {
        p->slab[slabs[i].index]=slabs[i].staged; slabs[i].staged=0;
    }
    for(uint32_t i=start;i<end;++i) {
        SpEntry *e=&p->slab[i/SP_PER_SLAB][i%SP_PER_SLAB];
        e->ready=0; e->next=0;
    }
    p->count=end; *first=(uint16_t)(start+1); result=0;
    restore(s,sr);
out:
    for(uint32_t i=0;i<nslabs;++i) if(slabs[i].staged) p->release(slabs[i].staged,p->context);
    s->leave_task(s->context); return result;
}
