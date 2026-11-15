/* Disposable emulator fixture. No Sound or production pool is modified. */
#include <stdint.h>
#include "../../../canopy-two-phase/credit-refill.h"
extern int32_t dkrt_job_post(uintptr_t,uintptr_t,uint8_t *);
extern uint8_t *fw_alloc(uint32_t);
extern void fw_free(uint8_t *);
extern uint32_t tp_probe_sr(void);
extern uint32_t tp_probe_mask(void *);
extern void tp_probe_restore(void *,uint32_t);

/* command, job ID, done status, done count, allocation SR, allocation calls,
 * masked-allocation refusals, pool count, slot count, UI TCB, worker TCB,
 * nested-lock refusals, worker step count, worker SR, fail injection, errors. */
volatile uint32_t worker_refill_test[16];
static SpPool pool;
static BcCredits credits;
static CreditRefill refill;
static volatile uint32_t task_guard;
static uint8_t initialized;
static int task_ready(void *x) { (void)x; return (tp_probe_sr()&0x700u)==0; }
static int enter(void *x)
{
    (void)x; uint32_t sr=tp_probe_mask(0); int busy=(int)task_guard;
    if(!busy) task_guard=1;
    tp_probe_restore(0,sr); return busy ? -1 : 0;
}
static void leave(void *x)
{ (void)x; uint32_t sr=tp_probe_mask(0); task_guard=0; tp_probe_restore(0,sr); }
static TpSync sync={task_ready,enter,leave,tp_probe_mask,tp_probe_restore,0};
static void *allocate(uint32_t size,void *x)
{
    (void)x; uint32_t sr=tp_probe_sr();
    worker_refill_test[4]=sr; ++worker_refill_test[5];
    if(sr&0x700u) { ++worker_refill_test[6]; return 0; }
    /* Re-entry must fail immediately; the worker never blocks for itself. */
    if(!enter(0)) { ++worker_refill_test[15]; leave(0); return 0; }
    ++worker_refill_test[11];
    if(worker_refill_test[14]) return 0;
    return fw_alloc(size);
}
static void release(void *p,void *x)
{
    (void)x;
    if(tp_probe_sr()&0x700u) { ++worker_refill_test[6]; return; }
    fw_free((uint8_t *)p);
}
static int32_t step(uint8_t *x)
{
    (void)x; ++worker_refill_test[12]; worker_refill_test[13]=tp_probe_sr();
    /* DTII 1.17 context-switch current-TCB operand at 0x4000041a. */
    worker_refill_test[10]=*(volatile const uint32_t *)(uintptr_t)0x47db3f64u;
    int result=cr_replenish(&refill);
    uint32_t sr=tp_probe_mask(0),count=0;
    for(uint32_t i=0;i<BC_SLOTS;++i) if(credits.slot[i].credit) ++count;
    worker_refill_test[7]=pool.count; worker_refill_test[8]=count;
    if(refill.inflight || task_guard) ++worker_refill_test[15];
    tp_probe_restore(0,sr); return result;
}
static void done(uint8_t *x,int32_t status)
{ (void)x; worker_refill_test[2]=(uint32_t)status; ++worker_refill_test[3]; }
void worker_refill_tick(void)
{
    uint32_t cmd=worker_refill_test[0];
    if(cmd!=1 && cmd!=2) return;
    worker_refill_test[0]=0;
    if(!initialized) {
        sp_init(&pool,allocate,0); sp_set_deallocator(&pool,release);
        bc_init(&credits,&pool); cr_init(&refill,&credits,&sync);
        uint32_t sr=tp_probe_mask(0);
        for(uint32_t i=0;i<BC_SLOTS;++i)
            if(bc_bind(&credits,i,0x1000u+0x100u*i,1)) ++worker_refill_test[15];
        tp_probe_restore(0,sr); initialized=1;
    }
    worker_refill_test[14]=(cmd==1);
    worker_refill_test[9]=*(volatile const uint32_t *)(uintptr_t)0x47db3f64u;
    worker_refill_test[1]=(uint32_t)dkrt_job_post((uintptr_t)step,(uintptr_t)done,0);
}
