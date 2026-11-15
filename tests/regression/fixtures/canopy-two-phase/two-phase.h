/* Isolated single-core task/IRQ experiment; never linked into firmware. */
#ifndef CANOPY_TWO_PHASE_H
#define CANOPY_TWO_PHASE_H
#include <stdint.h>
#include "../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"
enum { TP_BATCH_MAX=4 };
typedef struct {
    /* True only in task context with interrupts enabled. */
    int (*task_ready)(void *);
    /* Non-reentrant task serialization: only the task may acquire it, and
     * every other task-side pool reservation/mutation must use it. */
    int (*enter_task)(void *);
    void (*leave_task)(void *);
    /* Mask/restore must preserve the incoming interrupt mask and provide
     * compiler and CPU ordering. Every IRQ-side publish/read and every pool
     * observer must use the same exclusion; IRQ must never take task lock. */
    uint32_t (*mask_irq)(void *);
    void (*restore_irq)(void *,uint32_t);
    void *context;
} TpSync;
/* Reserve at most four consecutive unpublished handles. Slabs are allocated
 * without masking interrupts, staged privately and attached in one short
 * masked commit. On failure *first and pool metadata remain untouched by
 * this call. A matching release function is required for rollback.
 * This assumes single-core IRQ preemption, no DMA writer and a quiescent
 * allocator/lifetime protocol. No real-time or hardware safety claim. */
int tp_reserve_batch(SpPool *pool,const TpSync *sync,uint32_t count,uint16_t *first);
#endif
