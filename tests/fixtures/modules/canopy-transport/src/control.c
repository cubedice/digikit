/* Emulator-only provider. Bootstrap uses actual frame ACKs; never forge readiness
 * in production. The optional UI-tick policy slot is exclusive to this fixture. */
#include "../../../../../modules/core/adapters/midi-events/src/cf/bridge.h"
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
static uint32_t phase;
volatile uint32_t cp_transport_proof[12];

int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{
    return (phase == 2 || phase == 3) && track == 0 && epoch == mi_ledger.epoch &&
        generation == mi_ledger.generations[0] && mi_transport.applied[0] == 1 &&
        !mi_ledger.reset_mask && !mi_transport.faults;
}

void fx_controls_tick(void)
{
    uint32_t sr, value=1;
    if (!phase) {
        /* Initializes the bridge, but cannot admit a route before a heartbeat. */
        (void)mi_route_enable(0,0,0,0);
        mi_instance_reset(0);
        phase=1;
    } else if (phase == 1) {
        if (!mi_config_submit(0,1,0x43505331,&value,1)) phase=2;
    } else if (phase == 2 && instrument_midi_ready(0,mi_ledger.epoch,mi_ledger.generations[0])) {
        if (!mi_route_enable(0,0,0,0) && !mi_route_enable(1,0,0,0)) phase=3;
        else phase=4; /* Explicit test failure; never spin on partial admission. */
    }
    sr=dkrt_irq_mask();
    cp_transport_proof[0]=phase;
    cp_transport_proof[1]=mi_transport.heartbeat_seen;
    cp_transport_proof[2]=mi_transport.heartbeat;
    cp_transport_proof[3]=mi_transport.faults;
    cp_transport_proof[4]=mi_ledger.reset_mask;
    cp_transport_proof[5]=mi_transport.applied[0];
    cp_transport_proof[6]=mi_transport.pending;
    cp_transport_proof[7]=mi_ledger.count;
    cp_transport_proof[8]=mi_transport.next_message;
    cp_transport_proof[9]=mi_ledger.next_token;
    cp_transport_proof[10]=mi_ledger.generations[0];
    cp_transport_proof[11]=mi_ledger.epoch;
    dkrt_irq_restore(sr);
}
