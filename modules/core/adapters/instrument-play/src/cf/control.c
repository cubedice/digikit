/* UI-task policy for the single-instance Stage 1 adapter. */
#include <stdint.h>
#include <dk/ui.h>
#include "../../../midi-events/src/cf/bridge.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_instrument_generation(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern int instrument_sound_prepare(int allow_refresh);
static const uint32_t limits[8] = {0x49525031,1,1,100000,1000,8,128,528};
const uint32_t *instrument_runtime_policy(void) { return limits; }
static int enabled, usb=1, din=1, channel=1;
static uint32_t phase, prepared, healthy, fault_generation;
int instrument_play_faulted(uint32_t generation) { return generation && fault_generation == generation; }
volatile uint32_t ip_control[8];
static dk_ui_menu *menu;
extern volatile uint32_t ip_view;
extern void ip_view_build(uint32_t view);
static uint32_t listed_type;

int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{
    return enabled && healthy && prepared && (phase == 2 || phase == 3) &&
        track == 0 && epoch == mi_ledger.epoch && generation == mi_ledger.generations[0] &&
        mi_transport.applied[0] == 1 && !mi_ledger.reset_mask && !mi_transport.faults;
}
void instrument_sound_suspend(void)
{
    prepared=0; mi_instance_reset(0); phase=1;
}
static void changed(void *ctx, int value)
{
    (void)ctx; (void)value;
    /* Edits revoke old routes and preserve physical-key tombstones. */
    instrument_sound_suspend();
}
static void fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    dk_ui_text(m, "TRACK 1 KEYBOARD");
    dk_ui_toggle(m, "ENABLE", &enabled, changed, 0);
    dk_ui_toggle(m, "USB", &usb, changed, 0);
    dk_ui_toggle(m, "DIN", &din, changed, 0);
    dk_ui_value(m, "CHANNEL", &channel, 1, 16, changed, 0);
    dk_ui_text(m, phase == 3 ? "READY" : "SELECT CANOPY ON T1");
}
static const dk_ui_menu_desc desc = {.name="instrument-keyboard",.title="KEYBOARD",.fill=fill};
void dk_app_instrument_settings_rows(dk_ui_menu *parent)
{
    if (!menu) menu=dk_ui_menu_new(&desc);
    if (menu) dk_ui_submenu(parent, "KEYBOARD", menu);
}
void instrument_play_tick(void)
{
    uint32_t reply[16], value=1;
    uint32_t type=dkrt_instrument_type();
    /* V2 publication completes after the stock view's boot constructor.
     * Rebuild once on this owning UI task, after the admitted rows are visible.
     * The retained row remains listed after a fault; no unload or hot reload. */
    if (type && ip_view && listed_type != type) {
        ip_view_build(ip_view);
        listed_type=type;
    }
    int count=mi_registry_reply(reply,16);
    /* A temporarily missing/reordered reply cannot revoke a healthy route;
     * the transport's independent deadline and reset guard remain authoritative. */
    if (count == 16 && reply[0] == MT_PLAY_REPLY_MAGIC && reply[13] == 5 &&
        reply[14] == dkrt_instrument_generation()) fault_generation=reply[14];
    if (count == -2 || !dkrt_instrument_type()) healthy=0;
    else if (count == 16) healthy=reply[0] == MT_PLAY_REPLY_MAGIC &&
        reply[13] == 3 && reply[14] == dkrt_instrument_generation();
    if (!healthy || !enabled || (!usb && !din)) {
        if (phase == 2 || phase == 3) instrument_sound_suspend();
        goto publish;
    }
    if (!phase) { mi_instance_reset(0); phase=1; }
    {
        int result=instrument_sound_prepare(phase == 1 && !mi_ledger.reset_mask);
        if (result < 0) instrument_sound_suspend();
        else prepared=(uint32_t)result;
    }
    if (phase == 1 && prepared && !mi_ledger.reset_mask &&
        !mi_config_submit(0,1,0x43505331,&value,1)) phase=2;
    if (phase == 2 && instrument_midi_ready(0,mi_ledger.epoch,mi_ledger.generations[0])) {
        if ((!usb || !mi_route_enable(0,(uint32_t)channel-1,0,0)) &&
            (!din || !mi_route_enable(1,(uint32_t)channel-1,0,0))) phase=3;
        else instrument_sound_suspend();
    }
publish:
    ip_control[0]=phase; ip_control[1]=(uint32_t)enabled;
    ip_control[2]=healthy; ip_control[3]=prepared;
    ip_control[4]=mi_transport.faults; ip_control[5]=mi_ledger.reset_mask;
    ip_control[6]=mi_transport.applied[0]; ip_control[7]=dkrt_instrument_type();
}
