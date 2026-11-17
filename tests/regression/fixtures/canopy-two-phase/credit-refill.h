/* Host/emulator-only coordinator; not a firmware integration. */
#ifndef CANOPY_CREDIT_REFILL_H
#define CANOPY_CREDIT_REFILL_H
#include "two-phase.h"
#include "../canopy-credits/boundary-credits.h"
typedef struct {
    BcCredits *credits;
    const TpSync *sync;
    uint16_t spare[BC_SLOTS];
    uint8_t spare_count, inflight;
} CreditRefill;
void cr_init(CreditRefill *r, BcCredits *credits, const TpSync *sync);
/* Task-only. Other owner/edit/capture operations must hold the same IRQ lock.
 * In-flight credits remain owned by this coordinator; owner changes are
 * rechecked at attach, with surplus held as reusable unpublished spares. */
int cr_replenish(CreditRefill *r);
#endif
