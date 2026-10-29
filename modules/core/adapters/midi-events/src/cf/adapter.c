/* Firmware-task boundary. The pure ledger is shared with host behavior tests.
 * One short interrupt mask serializes MIDI, UI configuration and audio sender.
 * A route can only be enabled by the future V2 instance owner after readiness. */
#include "ownership.h"
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
    __attribute__((weak));

MiLedger mi_ledger;
static uint32_t initialized;

int mi_route_enable(uint32_t port, uint32_t channel, uint32_t track, int sequencer)
{
    uint32_t sr;
    int result = -2;
    if (port >= 2 || channel >= 16 || track >= MI_TRACKS || !instrument_midi_ready) return -1;
    sr = dkrt_irq_mask();
    if (!initialized) { mi_init(&mi_ledger); initialized = 1; }
    if (instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track]))
        result = mi_route(&mi_ledger, port, channel, track,
                          mi_ledger.generations[track], sequencer);
    dkrt_irq_restore(sr);
    return result;
}

void mi_instance_reset(uint32_t track)
{
    uint32_t sr = dkrt_irq_mask();
    if (initialized) mi_reset_track(&mi_ledger, track);
    dkrt_irq_restore(sr);
}

int mi_channel_message(const uint8_t *msg, uint32_t len, uint32_t source)
{
    uint32_t sr, port;
    int consumed;
    if (!initialized || len != 3 || !msg) return 0;
    if (source == 2 || source == 4) port = 0;      /* USB cable 0 */
    else if (source == 8 || source == 16) port = 1; /* DIN */
    else return 0;
    sr = dkrt_irq_mask();
    consumed = mi_message(&mi_ledger, port, msg, len);
    dkrt_irq_restore(sr);
    return consumed;
}
