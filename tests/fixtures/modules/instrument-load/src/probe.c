#include <stdint.h>
/* Debugger writes a bounded DKDS6 section, then request=1. request=2 retires.
 * Word 2 is reserved; allocation bounds come from the runtime provider.
 * No device UI or module-store ingress. */
uint32_t ilp_input[67];
uintptr_t instrument_load_probe(void) {
    return (uintptr_t)ilp_input;
}
