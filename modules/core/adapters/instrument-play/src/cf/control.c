/* UI-task policy and setup for the single admitted keyboard instrument. */
#if defined(__m68k__) && defined(__GNUC__)
/* Keep lookup data out of executable sections checked by the CF ISA gate. */
#pragma GCC optimize ("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
#include <stdint.h>
#include <dk/ui.h>
#include "keyboard.h"
#include "preset.h"
#include "routing.h"
#include "src-control.h"
#include "../../../midi-events/src/cf/bridge.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_instrument_generation(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern int instrument_sound_prepare(int allow_refresh);
static const uint32_t limits[8] = {0x49525031,1,1,100000,1000,8,128,768};
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
        mi_transport.applied[0] != 0 && !mi_ledger.reset_mask && !mi_transport.faults;
}
/* Stage 3 setup. Updates use the existing atomic configuration channel,
 * retain note ownership, and coalesce knob motion while an ACK is pending. */
static int pad_family=0, pad_color=64, pad_drift=0, pad_attack=5, pad_release=15;
static int arp_mix=0, arp_bpm=120, arp_rate=4, arp_gate=50;
static int arp_order=0, arp_octaves=1, arp_swing=0, arp_clock=0, arp_restart=0, pad_strum=0;
static uint32_t pad_dirty=1;
static void pad_changed(void *ctx,int value) { (void)ctx; (void)value; pad_dirty=1; }
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
static const char *const preset_keys[CP_SLOTS]={"canopy/preset/1","canopy/preset/2","canopy/preset/3","canopy/preset/4"};
static uint8_t preset_records[CP_SLOTS][CP_RECORD_BYTES], preset_present;
static uint8_t preset_write[CP_RECORD_BYTES];
static int preset_slot=1, preset_result, preset_pending;
static dk_ui_menu *preset_menu;
static const char route_key[]="canopy/routes/v1";
static uint8_t route_write[CR_BYTES];
static int route_pending,route_result;
static CrRoute restore_route;
static int route_restore;
static CsState src_state;
static IsStatus src_status;
static int src_initialized;
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
    route_restore=0; /* An explicit user edit supersedes deferred boot setup. */
    type=keyboard.type;
    if (keyboard.mode==KB_LEARN) kb_learn_cancel(&keyboard);
    if (selector_channel==channel) selector_channel=0;
    keyboard.mode=(uint8_t)mode; keyboard.type=(uint8_t)type;
    keyboard.voicing=(uint8_t)voicing;
    if(keyboard.slot!=(uint8_t)(slot-1)) keyboard.preset_shape_active=0;
    keyboard.slot=(uint8_t)(slot-1);
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
    uint8_t notes[4],count=keyboard.capture_count,status=keyboard.status,shape_slot=keyboard.slot;
    for (uint32_t i=0;i<4;++i) notes[i]=keyboard.capture[i];
    dkrt_irq_restore(sr);
    char title[18];
    for(uint32_t i=0;i<sizeof(title);++i) ((volatile char *)title)[i]="SLOT 1  0/4 NOTES"[i];
    title[5]=(char)('1'+shape_slot); title[8]=(char)('0'+count); dk_ui_text(m,title);
    /* Note zero is C-1, never an empty sentinel. Two pitches per row fit the
     * native menu font even at the four-character C#-1 boundary. */
    for(uint32_t row=0;row<2;++row) {
        char line[14]; uint32_t at=0;
        for(uint32_t col=0;col<2;++col) {
            uint32_t i=row*2+col; char note[6]; cv_note(note,notes[i],i<count);
            if(col) { line[at++]=' '; line[at++]=' '; }
            for(uint32_t j=0;note[j];++j) line[at++]=note[j];
        }
        line[at]=0; dk_ui_text(m,line);
    }
    char root[11]={'R','O','O','T',' ',0}; cv_note(root+5,notes[0],count!=0); dk_ui_text(m,root);
    dk_ui_text(m,status==KB_FULL ? "FULL - FOUR NOTES" : count ? "YES SAVE / NO CANCEL" : "PLAY 1-4 NOTES");
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
static const char *const arp_orders[]={"UP","DOWN","UP / DOWN","AS PLAYED","RANDOM"};
static const char *const arp_clocks[]={"LOCAL BPM","MASTER GRID","MASTER PHASE"};
static const char *const arp_restarts[]={"TRIG","PATTERN","CONTINUE"};
static void arp_order_done(void *ctx,int value)
{ (void)ctx; if(value<0 || value>4) return; arp_order=value; pad_changed(0,value); dk_ui_refresh(menu); }
static void arp_clock_done(void *ctx,int value)
{
    (void)ctx; if(value<0 || value>2) return;
    arp_clock=value; arp_rate=value==1 ? 1 : 4;
    if(value==1) arp_swing=0;
    pad_changed(0,value); dk_ui_refresh(menu);
}
static void arp_restart_done(void *ctx,int value)
{ (void)ctx; if(value<0 || value>2) return; arp_restart=value; pad_changed(0,value); dk_ui_refresh(menu); }
static void arp_choice(void *ctx,int value)
{
    (void)ctx;
    if(value==0) dk_ui_choice("ARP ORDER",arp_orders,5,arp_order_done,0);
    if(value==1) dk_ui_choice("CLOCK",arp_clocks,3,arp_clock_done,0);
    if(value==2) dk_ui_choice("RESTART",arp_restarts,3,arp_restart_done,0);
}
/* Snapshot on the UI task under the mapper lock. Routes and performance state
 * are deliberately absent. Presets include the effective custom shape, not a
 * mutable library slot number. */
static void preset_snapshot(CpPreset *p)
{
    uint32_t sr=dkrt_irq_mask();
    uint16_t v[CP_CONTROLS]={(uint16_t)keyboard.type,(uint16_t)keyboard.voicing,
        (uint16_t)pad_color,(uint16_t)pad_drift,(uint16_t)pad_strum,(uint16_t)arp_mix,
        (uint16_t)arp_rate,(uint16_t)arp_order,(uint16_t)arp_octaves,(uint16_t)arp_gate,
        (uint16_t)arp_swing,(uint16_t)arp_restart,(uint16_t)keyboard.latch,0,
        (uint16_t)pad_attack,(uint16_t)pad_release};
    for(uint32_t i=0;i<CP_CONTROLS;++i) p->control[i]=v[i];
    p->bpm=(uint16_t)arp_bpm; p->family=(uint8_t)pad_family;
    p->mode=keyboard.mode; p->clock=(uint8_t)arp_clock;
    const KbShape *shape=keyboard.preset_shape_active ? &keyboard.preset_shape : &keyboard.shapes[keyboard.slot];
    p->shape.count=shape->count;
    for(uint32_t i=0;i<4;++i) p->shape.intervals[i]=shape->intervals[i];
    dkrt_irq_restore(sr);
}
static void src_requested(CpPreset *current)
{
    if(!src_initialized) { cs_init(&src_state); src_initialized=1; }
    preset_snapshot(current); cs_sync(&src_state,current->control);
    cs_metadata(&src_state,current->mode | ((uint32_t)current->clock<<2) |
        ((uint32_t)current->family<<4) | ((uint32_t)current->bpm<<8));
}
uint32_t instrument_src_context(uint32_t track,uint32_t page,int visible)
{
    CpPreset current; src_requested(&current);
    uint32_t generation=mi_ledger.generations[0];
    uint32_t token=cs_context(&src_state,track,generation,
        visible && track==0 && keyboard_initialized && dkrt_instrument_type(),page,
        keyboard.mode,(uint32_t)arp_clock,keyboard.mode==KB_ROOT && keyboard.type==10,
        keyboard.mode==KB_LEARN || (learn_menu && dk_ui_is_open(learn_menu)));
    is_select(&src_status,track,generation,keyboard.mode,keyboard.mode==KB_LEARN ? keyboard.session : 0);
    return token;
}
int instrument_src_encoder(uint32_t token,uint32_t encoder,int32_t detents,uint32_t now)
{
    if(!src_initialized) return CS_STALE;
    /* A queued event can outlive a preset load or sound reset without another
     * paint. Revalidate ownership before using its captured token. */
    (void)instrument_src_context(src_state.track,src_state.page[src_state.track],src_state.visible);
    uint32_t id=CS_NONE;
    int result=cs_edit(&src_state,token,encoder,detents,now,&id);
    if(result!=CS_EDIT) return result;
    uint32_t sr=dkrt_irq_mask(); int v=src_state.value[id];
    switch(id) {
    case CP_CHORD: type=v; keyboard.type=(uint8_t)v; keyboard.update=1; break;
    case CP_VOICING: voicing=v; keyboard.voicing=(uint8_t)v; keyboard.update=1; break;
    case CP_COLOR: pad_color=v; break;
    case CP_DRIFT: pad_drift=v; break;
    case CP_STRUM: pad_strum=v; break;
    case CP_ARP_MIX: arp_mix=v; break;
    case CP_RATE: arp_rate=v; break;
    case CP_ORDER: arp_order=v; break;
    case CP_OCTAVES: arp_octaves=v; break;
    case CP_GATE: arp_gate=v; break;
    case CP_SWING: arp_swing=v; break;
    case CP_RESTART: arp_restart=v; break;
    case CP_LATCH: latch=v; kb_latch(&keyboard,v); break;
    default: break;
    }
    pad_dirty=1; dkrt_irq_restore(sr);
    return result;
}
void instrument_src_display(CvDisplay *out,uint32_t now,const char *tune,const char *level)
{
    if(!src_initialized) (void)instrument_src_context(0,0,0);
    (void)instrument_src_context(src_state.track,src_state.page[src_state.track],src_state.visible);
    is_poll(&src_status,now,mi_status_reply);
    if(is_detail_confirmed(&src_status,now))
        cs_applied(&src_state,src_status.generation,src_status.detail_control,src_status.detail);
    cv_build(out,&src_state,&src_status,now,tune,level,preset_result || storage_result);
}
int instrument_src_receive(const uint32_t words[IS_WORDS],uint32_t now)
{ return src_initialized ? is_receive(&src_status,words,now) : -1; }
static int preset_dirty(void)
{
    CpPreset p; uint8_t b[CP_RECORD_BYTES]; preset_snapshot(&p);
    if(cp_encode(&p,b) || !(preset_present & (1u<<(preset_slot-1)))) return 1;
    for(uint32_t i=0;i<CP_RECORD_BYTES;++i)
        if(b[i]!=preset_records[preset_slot-1][i]) return 1;
    return 0;
}
static void preset_save(void *ctx,int value)
{
    (void)ctx; (void)value;
    if(keyboard.mode==KB_LEARN) { dk_ui_toast("CLOSE LEARN FIRST"); return; }
    CpPreset p; preset_snapshot(&p);
    if(cp_encode(&p,preset_write)) { dk_ui_toast("INVALID SOUND - NOT SAVED"); return; }
    preset_pending=preset_slot;
}
static void preset_select(void *ctx,int value)
{
    (void)ctx; (void)value;
    if(preset_menu) dk_ui_refresh(preset_menu);
}
static void preset_load(void *ctx,int value)
{
    (void)ctx; (void)value; CpPreset p;
    if(keyboard.mode==KB_LEARN) { dk_ui_toast("CLOSE LEARN FIRST"); return; }
    if(!(preset_present & (1u<<(preset_slot-1))) ||
        cp_decode(&p,preset_records[preset_slot-1],CP_RECORD_BYTES)) {
        dk_ui_toast("PRESET EMPTY OR INVALID"); return;
    }
    uint32_t sr=dkrt_irq_mask();
    /* One reset boundary prevents notes from observing a partial sound change.
     * The physical ledger retains old-key tombstones. */
    instrument_sound_suspend();
    mode=keyboard.mode=p.mode; type=keyboard.type=(uint8_t)p.control[CP_CHORD];
    voicing=keyboard.voicing=(uint8_t)p.control[CP_VOICING];
    latch=keyboard.latch=(uint8_t)p.control[CP_LATCH];
    keyboard.preset_shape.count=p.shape.count;
    for(uint32_t i=0;i<4;++i) keyboard.preset_shape.intervals[i]=p.shape.intervals[i];
    keyboard.preset_shape_active=1;
    pad_color=p.control[CP_COLOR]; pad_drift=p.control[CP_DRIFT];
    pad_strum=p.control[CP_STRUM]; arp_mix=p.control[CP_ARP_MIX];
    arp_rate=p.control[CP_RATE]; arp_order=p.control[CP_ORDER]; arp_octaves=p.control[CP_OCTAVES];
    arp_gate=p.control[CP_GATE]; arp_swing=p.control[CP_SWING]; arp_restart=p.control[CP_RESTART];
    pad_attack=p.control[CP_ATTACK]; pad_release=p.control[CP_RELEASE];
    pad_family=p.family; arp_clock=p.clock; arp_bpm=p.bpm; pad_dirty=1;
    preset_pending=0;
    dkrt_irq_restore(sr);
    dk_ui_toast("PRESET LOADED - PRESS NEW KEYS");
    if(preset_menu) dk_ui_refresh(preset_menu);
}
static void preset_fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    dk_ui_value(m,"PRESET",&preset_slot,1,CP_SLOTS,preset_select,0);
    dk_ui_text(m,preset_result ? "NOT SAVED - RETRY SAVE" : preset_dirty() ? "UNSAVED CHANGES" : "SAVED");
    dk_ui_action(m,"SAVE CURRENT SOUND",preset_save,0,0);
    dk_ui_action(m,"LOAD SAVED SOUND",preset_load,0,0);
    dk_ui_text(m,"ROUTING IS NOT IN PRESETS");
}
static const dk_ui_menu_desc preset_desc={.name="canopy-presets",.title="CANOPY PRESETS",.fill=preset_fill};
static void route_save(void *ctx,int value)
{
    (void)ctx; (void)value;
    CrRoute r={(uint8_t)enabled,(uint8_t)usb,(uint8_t)din,(uint8_t)channel,
        (uint8_t)(selector_octave+1),(uint8_t)selector_channel};
    if(cr_encode(&r,route_write)) { dk_ui_toast("INVALID ROUTING - NOT SAVED"); return; }
    route_pending=1;
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
    dk_ui_toggle(m,"REED (OFF=VELVET)",&pad_family,pad_changed,0);
    dk_ui_value(m,"COLOR",&pad_color,0,127,pad_changed,0);
    dk_ui_value(m,"DRIFT",&pad_drift,0,127,pad_changed,0);
    dk_ui_value(m,"ATTACK",&pad_attack,0,127,pad_changed,0);
    dk_ui_value(m,"RELEASE",&pad_release,0,127,pad_changed,0);
    dk_ui_text(m,arp_clock==2 ? "MASTER PHASE ARP" : arp_clock==1 ? "MASTER GRID ARP" : "LOCAL ARP");
    dk_ui_value(m,"ARP MIX",&arp_mix,0,127,pad_changed,0);
    dk_ui_value(m,"LOCAL BPM",&arp_bpm,30,300,pad_changed,0);
    dk_ui_value(m,arp_clock==1 ? "MASTER STEPS" : "NOTES / Q",&arp_rate,1,8,pad_changed,0);
    dk_ui_value(m,"ARP GATE",&arp_gate,1,100,pad_changed,0);
    dk_ui_action(m,arp_orders[arp_order],arp_choice,0,0);
    dk_ui_value(m,"OCTAVES",&arp_octaves,1,3,pad_changed,0);
    dk_ui_value(m,"SWING",&arp_swing,0,arp_clock==1 ? 0 : 75,pad_changed,0);
    dk_ui_action(m,arp_clocks[arp_clock],arp_choice,0,1);
    dk_ui_action(m,arp_restarts[arp_restart],arp_choice,0,2);
    dk_ui_value(m,"STRUM MS",&pad_strum,0,500,pad_changed,0);
    if(!preset_menu) preset_menu=dk_ui_menu_new(&preset_desc);
    if(preset_menu) dk_ui_submenu(m,"PRESETS",preset_menu);
    dk_ui_action(m,"SAVE ROUTING",route_save,0,0);
    if(route_result) dk_ui_text(m,"ROUTING NOT SAVED");
}
static const dk_ui_menu_desc desc = {.name="instrument-keyboard",.title="KEYBOARD",.fill=fill};
void dk_app_instrument_settings_rows(dk_ui_menu *parent)
{
    if (!menu) menu=dk_ui_menu_new(&desc);
    if (menu) dk_ui_submenu(parent, "KEYBOARD", menu);
}
void instrument_play_tick(void)
{
    uint32_t reply[16];
    if (!keyboard_initialized) {
        uint8_t record[KB_RECORD_BYTES]; kb_init(&keyboard);
        int length=dkrt_settings_get(shape_key,record,sizeof(record));
        if (length>=0) storage_result=kb_decode(&keyboard,record,(uint32_t)length);
        else if (length!=-68) storage_result=length;
        preset_present=0;
        for(uint32_t i=0;i<CP_SLOTS;++i) {
            CpPreset p;
            int n=dkrt_settings_get(preset_keys[i],preset_records[i],CP_RECORD_BYTES);
            if(n>=0 && !cp_decode(&p,preset_records[i],(uint32_t)n)) preset_present|=(uint8_t)(1u<<i);
        }
        uint8_t route[CR_BYTES];
        int n=dkrt_settings_get(route_key,route,sizeof(route));
        route_restore=n>=0 && !cr_decode(&restore_route,route,(uint32_t)n);
        route_result=(n==-68 || route_restore) ? 0 : n<0 ? n : -1;
        keyboard_initialized=1;
    }
    /* Reading a valid record cannot open MIDI routes. Restore requested setup
     * only after this boot has an admitted instrument generation; the ordinary
     * healthy/prepared/configured handshake still owns actual route activation. */
    if(route_restore && dkrt_instrument_type() && dkrt_instrument_generation()) {
        enabled=restore_route.enabled; usb=restore_route.usb; din=restore_route.din;
        channel=restore_route.channel; selector_octave=(int)restore_route.selector_octave-1;
        selector_channel=restore_route.selector_channel;
        changed(0,0);
    }
    if(route_pending) {
        route_result=dkrt_settings_put(route_key,route_write,sizeof(route_write));
        if(!route_result) route_result=dkrt_settings_flush();
        route_pending=0;
        dk_ui_toast(route_result ? "ROUTING IN RAM - SAVE FAILED" : "ROUTING SAVED");
        if(menu && dk_ui_is_open(menu)) dk_ui_refresh(menu);
    }
    if(preset_pending) {
        uint32_t index=(uint32_t)preset_pending-1;
        /* settings_put checks shared capacity before changing its RAM record.
         * A failed flush never promotes our last confirmed durable copy. */
        preset_result=dkrt_settings_put(preset_keys[index],preset_write,sizeof(preset_write));
        if(!preset_result) preset_result=dkrt_settings_flush();
        if(!preset_result) {
            for(uint32_t i=0;i<CP_RECORD_BYTES;++i) preset_records[index][i]=preset_write[i];
            preset_present|=(uint8_t)(1u<<index);
        }
        preset_pending=0;
        dk_ui_toast(preset_result ? "SOUND IN RAM - SAVE FAILED" : "PRESET SAVED");
        if(preset_menu && dk_ui_is_open(preset_menu)) dk_ui_refresh(preset_menu);
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
    /* The DSP bank and its presentation values come from one locked snapshot.
     * The same revision owns SRC pending markers and the audio configuration. */
    CpPreset current; src_requested(&current);
    const uint16_t *v=current.control;
    uint32_t values[32]={6|((uint32_t)current.mode<<8),current.family,
        v[CP_COLOR],v[CP_DRIFT],v[CP_ATTACK],v[CP_RELEASE],v[CP_ARP_MIX],current.bpm,
        v[CP_RATE],v[CP_GATE],v[CP_ORDER],v[CP_OCTAVES],v[CP_SWING],current.clock,
        v[CP_RESTART],(uint32_t)v[CP_STRUM]*3/2,
        v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],
        v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]};
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
        !mi_config_submit(0,src_state.revision,0x43505336,values,32)) { phase=2; pad_dirty=0; }
    if (phase==3 && src_state.revision>mi_transport.applied[0] &&
        !mi_config_submit(0,src_state.revision,0x43505336,values,32)) pad_dirty=0;
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
