/* The image part of core/track-keyboard beside its pattern storage (docs/plans/fork-image-diet.md section 9.2): the TRIG
 * page's second interface slot. The firmware calls it with the view's interface pointer; it calls the vtable entry the
 * adapter writes, the forwarder tk_trig_turn of seam.s (the service's handler, or the stock one while the service is not loaded). */
#include <stdint.h>

extern uint8_t tk_trig_turn(uint8_t *view, uint8_t *event);

uint8_t tk_trig_turn_interface(uint8_t *receiver, uint8_t *event)
{
    return tk_trig_turn(receiver - 4, event);
}
