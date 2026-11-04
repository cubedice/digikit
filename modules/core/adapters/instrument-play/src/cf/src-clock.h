#ifndef DK_CANOPY_SRC_CLOCK_H
#define DK_CANOPY_SRC_CLOCK_H
#include <stdint.h>
/* DT2 1.17 bus timer: accumulated high/low, last accumulated counter, now.
 * Result wraps in milliseconds, not at the hardware counter's 32.5 seconds. */
uint32_t cc_milliseconds(uint32_t high,uint32_t low,uint32_t previous,uint32_t counter);
uint32_t instrument_src_now(void);
#endif
