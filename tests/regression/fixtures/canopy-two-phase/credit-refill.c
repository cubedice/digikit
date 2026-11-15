#include "credit-refill.h"
#include <stdatomic.h>
_Static_assert((unsigned)BC_SLOTS<=(unsigned)TP_BATCH_MAX, "credit batch is too small");
void cr_init(CreditRefill *r,BcCredits *credits,const TpSync *sync)
{ r->credits=credits; r->sync=sync; r->spare_count=0; r->inflight=0; }
static uint32_t mask(const TpSync *s)
{ uint32_t v=s->mask_irq(s->context); atomic_signal_fence(memory_order_seq_cst); return v; }
static void restore(const TpSync *s,uint32_t v)
{ atomic_signal_fence(memory_order_seq_cst); s->restore_irq(s->context,v); }
static void assign_spares(CreditRefill *r)
{
    for(uint32_t i=0;i<BC_SLOTS && r->spare_count;++i) {
        BcSlot *s=&r->credits->slot[i];
        if(s->sound && !s->credit) s->credit=r->spare[--r->spare_count];
    }
}
int cr_replenish(CreditRefill *r)
{
    if(!r || !r->credits || !r->credits->pool || !r->sync || !r->sync->task_ready ||
       !r->sync->mask_irq || !r->sync->restore_irq || !r->sync->task_ready(r->sync->context)) return -1;
    const TpSync *s=r->sync;
    uint32_t sr=mask(s);
    if(r->inflight || r->spare_count>BC_SLOTS) { restore(s,sr); return -1; }
    r->inflight=1;
    assign_spares(r);
    uint32_t need=0;
    for(uint32_t i=0;i<BC_SLOTS;++i)
        if(r->credits->slot[i].sound && !r->credits->slot[i].credit) ++need;
    restore(s,sr);
    uint16_t first=0;
    int failed=need && tp_reserve_batch(r->credits->pool,s,need,&first);
    sr=mask(s);
    if(!failed) {
        /* assign_spares exhausted all existing spares whenever need>0, so
         * the newly reserved range (at most four) always fits here. */
        for(uint32_t i=0;i<need;++i) r->spare[r->spare_count++]=(uint16_t)(first+i);
        assign_spares(r);
    }
    r->inflight=0;
    restore(s,sr);
    return failed ? -1 : 0;
}
