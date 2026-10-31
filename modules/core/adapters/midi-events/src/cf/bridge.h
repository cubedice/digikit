#ifndef DK_MIDI_BRIDGE_H
#define DK_MIDI_BRIDGE_H
#include "transport.h"
extern MiLedger mi_ledger;
extern MiTransport mi_transport;
/* Internal core handoff, serialized with MIDI and the frame ISR. */
int mi_route_enable(uint32_t port, uint32_t channel, uint32_t track, int sequencer);
void mi_instance_reset(uint32_t track);
int mi_config_submit(uint32_t track, uint32_t revision, uint32_t schema,
                     const uint32_t *values, uint32_t count);
/* Last framed registry reply: 16 copied, 0 unavailable, -1 bad capacity,
 * -2 boot trust lost (sticky until cold boot).
 * Output unchanged on 0 or errors. Does not prove freshness or a matching transaction. */
int mi_registry_reply(uint32_t *out, uint32_t words);
/* Explicit recovery only, with all physical keys released. */
void mi_transport_recover(void);
void instrument_write_frame(uintptr_t tx, uint32_t entry, const volatile uint16_t *record);
#endif
