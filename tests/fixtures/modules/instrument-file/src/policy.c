#include <stdint.h>
/* Diagnostic callbacks only. This is not the CANOPY release resource budget. */
static const uint32_t limits[8] = {0x49525031, 1, 1, 100, 100, 2, 32, 64};
const uint32_t *instrument_runtime_policy(void) { return limits; }
