/* UI-task policy and setup for the single admitted keyboard instrument. */
#include <stdint.h>
#include <dk/ui.h>
#include "keyboard.h"
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
static KbState keyboard;
static int keyboard_initialized, mode, latch, type, voicing, selector_octave=1;
static int selector_channel, slot=1, storage_result, save_pending;
static uint32_t shown_session, shown_revision, refreshed_revision, refreshed_status;
static uint32_t shown_main=UINT32_MAX;
static dk_ui_menu *learn_menu;
volatile uint32_t ip_keyboard_status[12];
extern int dkrt_settings_get(const char *, uint8_t *, uint32_t);
extern int dkrt_settings_put(const char *, const uint8_t *, uint32_t);
extern int dkrt_settings_flush(void);
static const char shape_key[]="canopy/shapes/v1";
static const char *const modes[]={"PLAY","ROOT + TYPE"};
static const char *const types[]={"MAJOR","MAJOR 6","SUS 2","MINOR 6","MINOR","SUS 4",
    "ADD 9","DOM 7","DIMINISHED","MIN 7","CUSTOM","MAJ 7"};
static const char *const voicings[]={"CLOSE","OPEN","INVERSION 1","INVERSION 2"};

void instrument_sound_suspend(void)
{
    if (keyboard_initialized && keyboard.mode==KB_LEARN) kb_learn_cancel(&keyboard);
    prepared=0; mi_instance_reset(0); phase=1;
}

int instrument_keyboard_events(uint32_t track, uint32_t epoch, uint32_t generation,
    const MiEvent *input, uint32_t count, MiEvent *output)
{
    uint32_t i;
    if (track) { for (i=0;i<count;++i) mi_event_copy(&output[i],&input[i]); return (int)count; }
    if (!keyboard_initialized) return -1;
    if (keyboard.epoch!=epoch || keyboard.generation!=generation)
        kb_reset(&keyboard,epoch,generation);
    return kb_process(&keyboard,input,count,output);
}
static void changed(void *ctx, int value)
{
    (void)ctx; (void)value;
    uint32_t sr=dkrt_irq_mask();
    type=keyboard.type;
    if (keyboard.mode==KB_LEARN) kb_learn_cancel(&keyboard);
    if (selector_channel==channel) selector_channel=0;
    keyboard.mode=(uint8_t)mode; keyboard.type=(uint8_t)type;
    keyboard.voicing=(uint8_t)voicing; keyboard.slot=(uint8_t)(slot-1);
    keyboard.selector_base=(uint8_t)((selector_octave+1)*12);
    keyboard.selector_channel=selector_channel ? (uint8_t)(selector_channel-1) : MI_NONE;
    instrument_sound_suspend();
    dkrt_irq_restore(sr);
}
static void latch_changed(void *ctx, int value)
{
    (void)ctx; uint32_t sr=dkrt_irq_mask(); kb_latch(&keyboard,value); dkrt_irq_restore(sr);
}
static void choose_mode(void *ctx,int value) { (void)ctx; mode=value; changed(0,0); }
static void choose_type(void *ctx,int value) { (void)ctx; uint32_t sr=dkrt_irq_mask(); type=value; keyboard.type=(uint8_t)value; keyboard.update=1; dkrt_irq_restore(sr); }
static void choose_voicing(void *ctx,int value) { (void)ctx; uint32_t sr=dkrt_irq_mask(); voicing=value; keyboard.voicing=(uint8_t)value; keyboard.update=1; dkrt_irq_restore(sr); }
static void choice(void *ctx,int value)
{
    (void)ctx;
    if (!value) dk_ui_choice("MODE",modes,2,choose_mode,0);
    else if (value==1) dk_ui_choice("CHORD",types,12,choose_type,0);
    else dk_ui_choice("VOICING",voicings,4,choose_voicing,0);
}
static void panic(void *ctx,int value)
{
    (void)ctx; (void)value;
    uint32_t sr=dkrt_irq_mask();
    kb_learn_cancel(&keyboard); instrument_sound_suspend();
    kb_reset(&keyboard,mi_ledger.epoch,mi_ledger.generations[0]);
    dkrt_irq_restore(sr);
    dk_ui_toast("PANIC - RELEASE HELD KEYS");
}
static void learn_close(void *ctx,int save)
{
    (void)ctx;
    uint32_t sr=dkrt_irq_mask();
    int result=0;
    if (save) result=kb_learn_save(&keyboard,shown_session,shown_revision);
    else kb_learn_cancel(&keyboard);
    if (!result) { instrument_sound_suspend(); if (save) save_pending=1; }
    dkrt_irq_restore(sr);
    if (result) { dk_ui_toast("CAPTURE CHANGED OR EMPTY"); dk_ui_refresh(learn_menu); }
    else dk_ui_close(learn_menu);
}
/* We close explicitly: consume NO so the stock handler cannot close twice. */
static int learn_back(void *ctx) { learn_close(ctx,0); return 1; }
static void learn_fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    uint32_t sr=dkrt_irq_mask();
    shown_session=keyboard.session; shown_revision=keyboard.revision;
    uint8_t notes[4],count=keyboard.capture_count,status=keyboard.status;
    for (uint32_t i=0;i<4;++i) notes[i]=keyboard.capture[i];
    dkrt_irq_restore(sr);
    char text[24]; uint32_t at=6;
    text[0]='N'; text[1]='O'; text[2]='T'; text[3]='E'; text[4]='S'; text[5]=' ';
    for (uint32_t i=0;i<count;++i) {
        text[at++]=(char)('0'+notes[i]/100); text[at++]=(char)('0'+notes[i]/10%10);
        text[at++]=(char)('0'+notes[i]%10); text[at++]=' ';
    }
    text[at]=0;
    dk_ui_text(m,text); dk_ui_text(m,status==KB_FULL ? "FULL - FOUR NOTES" : "PLAY UP TO FOUR NOTES");
    dk_ui_action(m,"SAVE",learn_close,0,1); dk_ui_action(m,"CANCEL",learn_close,0,0);
}
static const dk_ui_menu_desc learn_desc={.name="canopy-learn",.title="LEARN TO ROOT",
    .flags=DK_UI_LIST,.fill=learn_fill,.back=learn_back};
static void learn_begin(void *ctx,int value)
{
    (void)ctx; (void)value;
    if (!learn_menu) learn_menu=dk_ui_menu_new(&learn_desc);
    if (!learn_menu) { dk_ui_toast("CANNOT OPEN LEARN"); return; }
    uint32_t sr=dkrt_irq_mask();
    int result=-1;
    if (keyboard.mode!=KB_LEARN && keyboard.session!=UINT32_MAX) {
        instrument_sound_suspend(); result=kb_learn_begin(&keyboard);
    }
    dkrt_irq_restore(sr);
    if (!result && dk_ui_open(learn_menu)) learn_close(0,0);
}
static void recover_done(void *ctx,int yes)
{
    (void)ctx; if (!yes) return;
    if (fault_generation==dkrt_instrument_generation() || mi_keyboard_recover()) {
        dk_ui_toast("RESTART REQUIRED"); return;
    }
    panic(0,0);
}
static void recover(void *ctx,int value)
{
    (void)ctx; (void)value;
    dk_ui_confirm("RECOVER KEYBOARD", "Release ALL keyboard keys and pedals. Ready?",recover_done,0);
}
static void retry_save(void *ctx,int value) { (void)ctx; (void)value; save_pending=1; }
static int quarantined(void)
{
    for (uint32_t i=0;i<MI_ROUTES;++i)
        if (mi_ledger.routes[i].track==0 && mi_ledger.routes[i].quarantined) return 1;
    return 0;
}
static void fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    dk_ui_text(m, "TRACK 1 KEYBOARD");
    dk_ui_toggle(m, "ENABLE", &enabled, changed, 0);
    dk_ui_toggle(m, "USB", &usb, changed, 0);
    dk_ui_toggle(m, "DIN", &din, changed, 0);
    dk_ui_value(m, "CHANNEL", &channel, 1, 16, changed, 0);
    dk_ui_text(m, quarantined() ? "RELEASE KEYS / RECOVER" : mi_transport.faults ? "RECOVER OR RESTART" :
        keyboard.status==KB_RANGE ? "CHORD OUT OF RANGE" :
        keyboard.status==KB_EMPTY ? "CUSTOM SHAPE EMPTY" : phase == 3 ? "READY" : "SELECT CANOPY ON T1");
    dk_ui_action(m,modes[mode],choice,0,0);
    dk_ui_toggle(m,"LATCH",&latch,latch_changed,0);
    dk_ui_action(m,types[keyboard.type],choice,0,1);
    dk_ui_action(m,voicings[voicing],choice,0,2);
    dk_ui_value(m,"SELECTOR OCTAVE",&selector_octave,-1,8,changed,0);
    dk_ui_value(m,"SELECT CH (0=SAME)",&selector_channel,0,16,changed,0);
    dk_ui_value(m,"SHAPE SLOT",&slot,1,8,changed,0);
    dk_ui_action(m,"LEARN TO ROOT",learn_begin,0,0);
    dk_ui_action(m,"PANIC",panic,0,0);
    dk_ui_text(m,storage_result ? "SHAPES NOT SAVED" : keyboard.dirty ? "SAVE PENDING" : "SHAPES SAVED");
    dk_ui_action(m,"RETRY SHAPE SAVE",retry_save,0,0);
    dk_ui_action(m,"RECOVER KEYBOARD",recover,0,0);
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
    if (!keyboard_initialized) {
        uint8_t record[KB_RECORD_BYTES]; kb_init(&keyboard);
        int length=dkrt_settings_get(shape_key,record,sizeof(record));
        if (length>=0) storage_result=kb_decode(&keyboard,record,(uint32_t)length);
        else if (length!=-68) storage_result=length;
        keyboard_initialized=1;
    }
    if (save_pending) {
        uint8_t record[KB_RECORD_BYTES];
        uint32_t sr=dkrt_irq_mask(); kb_encode(&keyboard,record); dkrt_irq_restore(sr);
        storage_result=dkrt_settings_put(shape_key,record,sizeof(record));
        if (!storage_result) storage_result=dkrt_settings_flush();
        if (!storage_result) keyboard.dirty=0;
        save_pending=0;
        dk_ui_toast(storage_result ? "SHAPE IN RAM - SAVE FAILED" : "SHAPES SAVED");
    }
    if (learn_menu && dk_ui_is_open(learn_menu) && keyboard.mode!=KB_LEARN) dk_ui_close(learn_menu);
    if (learn_menu && dk_ui_is_open(learn_menu) && (refreshed_revision!=keyboard.revision || refreshed_status!=keyboard.status)) {
        refreshed_revision=keyboard.revision; refreshed_status=keyboard.status; dk_ui_refresh(learn_menu);
    }
    ip_keyboard_status[0]=keyboard.mode; ip_keyboard_status[1]=keyboard.type;
    ip_keyboard_status[2]=keyboard.latch; ip_keyboard_status[3]=keyboard.capture_count;
    ip_keyboard_status[4]=keyboard.session; ip_keyboard_status[5]=keyboard.revision;
    ip_keyboard_status[6]=keyboard.status; ip_keyboard_status[7]=keyboard.dirty;
    ip_keyboard_status[8]=(uint32_t)storage_result;
    for (uint32_t i=0;i<3;++i) ip_keyboard_status[9+i]=keyboard.capture[i];
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
    if (!healthy || !enabled || (!usb && !din) || quarantined()) {
        if (phase == 2 || phase == 3) instrument_sound_suspend();
        goto publish;
    }
    if (!phase) { mi_instance_reset(0); phase=1; }
    if (phase==3 && (mi_ledger.reset_mask || !mi_transport.applied[0])) instrument_sound_suspend();
    {
        int result=instrument_sound_prepare(phase == 1 && !mi_ledger.reset_mask);
        if (result < 0) instrument_sound_suspend();
        else prepared=(uint32_t)result;
    }
    if (phase == 1 && prepared && !mi_ledger.reset_mask &&
        !mi_config_submit(0,1,0x43505331,&value,1)) phase=2;
    if (phase == 2 && instrument_midi_ready(0,mi_ledger.epoch,mi_ledger.generations[0])) {
        if ((!usb || !mi_route_enable(0,(uint32_t)channel-1,0,0)) &&
            (!din || !mi_route_enable(1,(uint32_t)channel-1,0,0)) &&
            (keyboard.mode!=KB_ROOT || !selector_channel ||
             ((!usb || !mi_route_enable(0,(uint32_t)selector_channel-1,0,0)) &&
              (!din || !mi_route_enable(1,(uint32_t)selector_channel-1,0,0))))) phase=3;
        else instrument_sound_suspend();
    }
publish:
    ip_control[0]=phase; ip_control[1]=(uint32_t)enabled;
    ip_control[2]=healthy; ip_control[3]=prepared;
    ip_control[4]=mi_transport.faults; ip_control[5]=mi_ledger.reset_mask;
    ip_control[6]=mi_transport.applied[0]; ip_control[7]=dkrt_instrument_type();
    uint32_t view=keyboard.type | ((uint32_t)keyboard.mode<<4) | ((uint32_t)keyboard.status<<6) |
        (phase<<9) | ((uint32_t)!!storage_result<<12) | ((uint32_t)keyboard.dirty<<13) |
        ((uint32_t)(quarantined() || mi_transport.faults)<<14);
    if (menu && dk_ui_is_open(menu) && (!learn_menu || !dk_ui_is_open(learn_menu)) && shown_main!=view) {
        shown_main=view; dk_ui_refresh(menu);
    }
}
