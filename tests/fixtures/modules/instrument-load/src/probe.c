#include <stdint.h>
extern char _dl_area[];
/* Debugger writes a bounded DKDS6 section, then request=1. request=2 retires.
 * No device UI or module-store ingress. The linker owns the area address. */
uint32_t ilp_input[67];
uintptr_t instrument_load_probe(void) {
    ilp_input[2] = (uintptr_t)_dl_area;
    return (uintptr_t)ilp_input;
}
