/* Reference application of a validated per-trig snapshot to a CPSB bank.
 * It never changes the accepted base bank or its revision. */
#ifndef DK_INSTRUMENT_SRC_LOCK_APPLY_H
#define DK_INSTRUMENT_SRC_LOCK_APPLY_H
#include "src-lock-event.h"
enum { ILP_APPLY_UNSUPPORTED = -5 };
int ilp_apply(const uint32_t base[32], const IlpSnapshot *snapshot, uint32_t output[32]);
#endif
