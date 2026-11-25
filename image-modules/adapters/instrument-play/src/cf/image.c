/* The image part of core/instrument-play that is not generated (docs/plans/fork-image-diet.md): what two forwarders of
 * seam.s do while services/instrument.dks is not loaded. Both keep the paths the moved functions take for a type that is
 * not the instrument's (src-mod.c), so RELAY tracks behave as before without the service. */
#include <stdint.h>
#include "instances.h"
extern const uint32_t ip_mod_track_type;
extern uint32_t rtm_mod_param(uint32_t type, uint32_t mirror) __attribute__((weak));
extern int rtm_mod_carrier(uint32_t type) __attribute__((weak));

uint32_t ip_mod_param_absent(uint32_t type, uint32_t mirror)
{
    return rtm_mod_param && type >= 7 ? rtm_mod_param(type, mirror) : UINT32_MAX;
}

int ip_mod_carrier_absent(uint32_t track)
{
    if (track >= 16)
        return 0;
    uint32_t actual = *(volatile const uint8_t *)(uintptr_t)(ip_mod_track_type + 0x9au * track);
    return rtm_mod_carrier && actual >= 7 && rtm_mod_carrier(actual);
}

/* The SourcePageView encoder interface entry: the interface sits 4 bytes into the view. It calls the forwarder of
 * ip_src_turn, so it reaches the service's handler or the stock one. */
extern uint32_t ip_src_turn(uint8_t *view, uint8_t *event);
uint32_t ip_src_encoder_interface(uint8_t *receiver, uint8_t *event)
{
    return ip_src_turn(receiver - 4, event);
}

/* The admission policy the runtime reads (pending.rs): up to IP_INSTANCES admitted CANOPY instances; process and state are
 * per instance, and the runtime multiplies them for the total budget and allocation. It is in the image because the runtime
 * binds it on its first tick and the boot plan counts the instrument capacity with it, both before any service file loads. */
static const uint32_t limits[8] = {0x49525031, 1, IP_INSTANCES, 100000, 1600, 8, 128, 2976};
const uint32_t *instrument_runtime_policy(void)
{
    return limits;
}
