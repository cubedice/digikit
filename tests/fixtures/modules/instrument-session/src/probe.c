#include <stdint.h>

/* Emulator writes once after boot. There is deliberately no device UI. */
volatile uint32_t isp_request;
uint32_t instrument_session_probe(void) { return isp_request; }
