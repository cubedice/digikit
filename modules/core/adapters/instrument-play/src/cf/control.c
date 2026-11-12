/* UI-task policy and setup for up to IP_INSTANCES admitted keyboard instruments,
 * one per kit track (finding 121). Instance i is DSP slot i. The UI (SRC page,
 * menus, presets, panel keys) acts on `cur`, the instance on the selected track;
 * the tick services every instance; the MIDI mapper finds its own by track. */
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
#include "src-clock.h"
#include "src-mod.h"
#include "instances.h"
#include "../../../midi-events/src/cf/bridge.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_instrument_generation(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
/* Up to IP_INSTANCES admitted CANOPY instances; process and state are per
 * instance. The runtime multiplies them for the total budget and allocation. */
static const uint32_t limits[8] = {0x49525031,1,IP_INSTANCES,100000,1600,8,128,2976};
const uint32_t *instrument_runtime_policy(void) { return limits; }
/* Device routing, shared by every instance. CHANNEL is the default for a track
 * without its own channel (track_channel 0). */
static int enabled, usb=1, din=1, channel=1, selector_octave=1, selector_channel;
static uint8_t track_channel[MI_TRACKS];
static uint32_t healthy, fault_generation;
int instrument_play_faulted(uint32_t generation) { return generation && fault_generation == generation; }
/* Per instance: phase, external MIDI, healthy, prepared, faults, reset mask,
 * applied revision, type ([0..7] is instance 0). */
volatile uint32_t ip_control[IP_INSTANCES * 8];
static dk_ui_menu *menu;
extern volatile uint32_t ip_view;
extern void ip_view_build(uint32_t view);
static uint32_t listed_type;

typedef struct {
    uint32_t track;      /* admitted track (ip_tracks), IP_NONE when free */
    uint32_t bound;      /* track the transport was last bound/reset for */
    uint32_t phase, prepared, conflict;
    /* Stage 3 setup. Updates use the existing atomic configuration channel,
     * retain note ownership, and coalesce knob motion while an ACK is pending. */
    int pad_family, pad_color, pad_drift, pad_attack, pad_release;
    int arp_mix, arp_bpm, arp_rate, arp_gate;
    int arp_order, arp_octaves, arp_swing, arp_clock, arp_restart, pad_strum;
    int wt[12], fm[13], wtfm[4];
    uint32_t pad_dirty;
    int mode, type, voicing, slot;
    KbState keyboard;
} IpInst;
/* Exported for the proofs: element 0 is instance 0. */
CsState src_state[IP_INSTANCES];
IsStatus src_status[IP_INSTANCES];
static IpInst inst[IP_INSTANCES];
static IpInst *cur=&inst[0];
static uint32_t selected=IP_NONE, focus=IP_NONE, initialized;
#define IDX(p) ((uint32_t)((p)-inst))
#define SRC(p) (&src_state[IDX(p)])
#define STS(p) (&src_status[IDX(p)])

static void instance_defaults(IpInst *in)
{
    static const int wt0[12]={0,0,0,10,0,0,0,100,5,15,100,100};
    static const int fm0[13]={32,0,50,3,8,0,5,40,50,15,100,100,100};
    static const int wtfm0[4]={0,3,100,100};
    in->pad_family=0; in->pad_color=64; in->pad_drift=0; in->pad_attack=5; in->pad_release=15;
    in->arp_mix=0; in->arp_bpm=120; in->arp_rate=4; in->arp_gate=50;
    in->arp_order=0; in->arp_octaves=0; in->arp_swing=0; in->arp_clock=0; in->arp_restart=0;
    in->pad_strum=0;
    for(uint32_t i=0;i<12;++i) in->wt[i]=wt0[i];
    for(uint32_t i=0;i<13;++i) in->fm[i]=fm0[i];
    for(uint32_t i=0;i<4;++i) in->wtfm[i]=wtfm0[i];
    in->pad_dirty=1; in->mode=0; in->type=0; in->voicing=0; in->slot=1;
    in->track=in->bound=IP_NONE; in->phase=in->prepared=in->conflict=0;
}
static void ip_init(void)
{
    if(initialized) return;
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        instance_defaults(&inst[i]);
        kb_init(&inst[i].keyboard);
        cs_init(&src_state[i]);
    }
    cur=&inst[0];
    initialized=1;
}
static IpInst *for_track(uint32_t track)
{
    for(uint32_t i=0;track<MI_TRACKS && i<IP_INSTANCES;++i)
        if(inst[i].track==track) return &inst[i];
    return 0;
}
/* The instance bound and admitted for this track (mapper/ISR safe). */
static IpInst *owner(uint32_t track)
{
    for(uint32_t i=0;track<MI_TRACKS && i<IP_INSTANCES;++i)
        if(inst[i].track==track && inst[i].bound==track) return &inst[i];
    return 0;
}
int instrument_track_owned(uint32_t track) { return for_track(track)!=0; }
void instrument_select_track(uint32_t track)
{
    ip_init();
    if(track>=MI_TRACKS) return;
    selected=track;
    IpInst *in=for_track(track);
    if(in) cur=in;
}
/* The UI target after admission changes: the selected track's instance, else
 * the lowest admitted one, else instance 0. */
static IpInst *ui_target(void)
{
    IpInst *in=for_track(selected);
    if(in) return in;
    IpInst *low=0;
    for(uint32_t i=0;i<IP_INSTANCES;++i)
        if(inst[i].track<MI_TRACKS && (!low || inst[i].track<low->track)) low=&inst[i];
    if(low) return low;
    return for_track(cur->track) ? cur : &inst[0];
}
static uint32_t channel_of(const IpInst *in)
{
    uint32_t t=in->track<MI_TRACKS ? in->track : in->bound;
    return t<MI_TRACKS && track_channel[t] ? track_channel[t] : (uint32_t)channel;
}

int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{
    IpInst *in=owner(track);
    return in && healthy && in->prepared && (in->phase == 2 || in->phase == 3) &&
        epoch == mi_ledger.epoch && generation == mi_ledger.generations[track] &&
        mi_transport.applied[track] != 0 && !mi_ledger.reset_mask && !mi_transport.faults;
}
uint32_t instrument_src_pages(void) { return cur->pad_family>=2 ? 4 : 3; }
uint32_t instrument_src_tune_page(uint32_t page) { return page==0; }
static dk_ui_menu *tone_menu;
static dk_ui_menu *input_menu, *shape_menu;
static void pad_changed(void *ctx,int value) { (void)ctx; (void)value; cur->pad_dirty=1; }
static int keyboard_initialized, storage_result, save_pending;
static uint32_t shown_session, shown_revision, refreshed_revision, refreshed_status;
static uint32_t shown_main=UINT32_MAX;
static dk_ui_menu *learn_menu;
static dk_ui_menu *status_menu;
static uint32_t status_draw_at;
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
static const char channels_key[]="canopy/channels/v1";
static uint8_t route_write[CR_BYTES], channels_write[CC_BYTES];
static int route_pending,route_result;
static CrRoute restore_route;
static uint8_t restore_channels[MI_TRACKS];
static int route_restore;

void instrument_sound_suspend(uint32_t instance)
{
    if(instance>=IP_INSTANCES) return;
    IpInst *in=&inst[instance];
    if (keyboard_initialized && in->keyboard.mode==KB_LEARN) kb_learn_cancel(&in->keyboard);
    /* One reset boundary on the instance's track (its bound track while it
     * moves). The reset carries the slot, so DSP slot i follows the instance;
     * binding to a new track happens in the tick after this release. */
    uint32_t track=in->bound<MI_TRACKS ? in->bound : in->track;
    if (track<MI_TRACKS) { in->bound=track; mi_instance_bind(track,instance); }
    in->prepared=0; in->phase=1;
}
static void suspend_all(void)
{
    for(uint32_t i=0;i<IP_INSTANCES;++i)
        if(inst[i].track<MI_TRACKS || inst[i].phase) instrument_sound_suspend(i);
}

int instrument_keyboard_events(uint32_t track, uint32_t epoch, uint32_t generation,
    const MiEvent *input, uint32_t count, MiEvent *output)
{
    uint32_t i;
    /* The instance admitted for this track maps its keys; the transport only
     * delivers a track's batches after that track was bound and configured. */
    IpInst *in=for_track(track);
    if (!in) { for (i=0;i<count;++i) mi_event_copy(&output[i],&input[i]); return (int)count; }
    if (!keyboard_initialized) return -1;
    if (in->keyboard.epoch!=epoch || in->keyboard.generation!=generation)
        kb_reset(&in->keyboard,epoch,generation);
    int mapped=kb_process(&in->keyboard,input,count,output);
    /* The mapper is track-agnostic; the wire batch names the admitted track. */
    for (i=0;mapped>0 && i<(uint32_t)mapped;++i) output[i].track=(uint8_t)track;
    return mapped;
}
/* Global routing edits re-route every instance; mode, chord, voicing and shape
 * slot belong to the selected instance. */
static void changed(void *ctx, int value)
{
    (void)ctx; (void)value;
    uint32_t sr=dkrt_irq_mask();
    route_restore=0; /* An explicit user edit supersedes deferred boot setup. */
    cur->type=cur->keyboard.type;
    if (cur->keyboard.mode==KB_LEARN) kb_learn_cancel(&cur->keyboard);
    if (selector_channel==(int)channel_of(cur)) selector_channel=0;
    cur->keyboard.mode=(uint8_t)cur->mode; cur->keyboard.type=(uint8_t)cur->type;
    cur->keyboard.voicing=(uint8_t)cur->voicing;
    if(cur->keyboard.slot!=(uint8_t)(cur->slot-1)) cur->keyboard.preset_shape_active=0;
    cur->keyboard.slot=(uint8_t)(cur->slot-1);
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        inst[i].keyboard.selector_base=(uint8_t)((selector_octave+1)*12);
        inst[i].keyboard.selector_channel=selector_channel ? (uint8_t)(selector_channel-1) : MI_NONE;
    }
    suspend_all();
    dkrt_irq_restore(sr);
}
/* The CHANNEL row edits the selected CANOPY track's channel. */
static int ui_channel=1;
static void channel_changed(void *ctx,int value)
{
    (void)value;
    if(cur->track<MI_TRACKS) track_channel[cur->track]=(uint8_t)ui_channel;
    else channel=ui_channel;
    changed(ctx,ui_channel);
}
/* Caller holds the mapper lock. A performance-mode boundary retires the old
 * physical-note generation; choosing another root chord keeps held roots live. */
static void chord_apply(uint32_t next_mode,uint32_t next_type)
{
    if(cur->keyboard.mode!=next_mode) instrument_sound_suspend(IDX(cur));
    cur->mode=cur->keyboard.mode=(uint8_t)next_mode;
    cur->type=cur->keyboard.type=(uint8_t)next_type;
    cur->keyboard.update=1;
}
/* PANIC releases every instance. */
static void panic(void *ctx,int value)
{
    (void)ctx; (void)value;
    uint32_t sr=dkrt_irq_mask();
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        IpInst *in=&inst[i];
        kb_learn_cancel(&in->keyboard);
        if(in->track<MI_TRACKS || in->phase) instrument_sound_suspend(i);
        uint32_t t=in->bound<MI_TRACKS ? in->bound : 0;
        kb_reset(&in->keyboard,mi_ledger.epoch,mi_ledger.generations[t]);
    }
    dkrt_irq_restore(sr);
    dk_ui_toast("PANIC - RELEASE HELD KEYS");
}
/* Learned shapes are one library shared by every instance. */
static void shapes_share(const IpInst *from)
{
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        if(&inst[i]==from) continue;
        for(uint32_t s=0;s<8;++s) inst[i].keyboard.shapes[s]=from->keyboard.shapes[s];
        inst[i].keyboard.dirty=from->keyboard.dirty;
    }
}
static void learn_close(void *ctx,int save)
{
    (void)ctx;
    uint32_t sr=dkrt_irq_mask();
    int result=0;
    if (save) result=kb_learn_save(&cur->keyboard,shown_session,shown_revision);
    else kb_learn_cancel(&cur->keyboard);
    if (!result) {
        if (save) shapes_share(cur);
        instrument_sound_suspend(IDX(cur));
        if (save) save_pending=1;
    }
    dkrt_irq_restore(sr);
    if (result) { dk_ui_toast("CAPTURE CHANGED OR EMPTY"); dk_ui_refresh(learn_menu); }
    else { dk_ui_close(learn_menu); if(save) dk_ui_toast("LEARNED - UNSAVED"); }
}
/* We close explicitly: consume NO so the stock handler cannot close twice. */
static int learn_back(void *ctx) { learn_close(ctx,0); return 1; }
/* YES saves from every visible row. NO is handled by learn_back; it must not
 * depend on which informational row the native list currently selects. */
static void learn_row(dk_ui_menu *m,const char *text)
{ dk_ui_action(m,text,learn_close,0,1); }
static void learn_fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    uint32_t sr=dkrt_irq_mask();
    shown_session=cur->keyboard.session; shown_revision=cur->keyboard.revision;
    uint8_t notes[4],count=cur->keyboard.capture_count,status=cur->keyboard.status,shape_slot=cur->keyboard.slot;
    for (uint32_t i=0;i<4;++i) notes[i]=cur->keyboard.capture[i];
    dkrt_irq_restore(sr);
    char title[18];
    for(uint32_t i=0;i<sizeof(title);++i) ((volatile char *)title)[i]="SLOT 1  0/4 NOTES"[i];
    title[5]=(char)('1'+shape_slot); title[8]=(char)('0'+count); learn_row(m,title);
    /* Note zero is C-1, never an empty sentinel. Two pitches per row fit the
     * native menu font even at the four-character C#-1 boundary. */
    for(uint32_t row=0;row<2;++row) {
        char line[14]; uint32_t at=0;
        for(uint32_t col=0;col<2;++col) {
            uint32_t i=row*2+col; char note[6]; cv_note(note,notes[i],i<count);
            if(col) { line[at++]=' '; line[at++]=' '; }
            for(uint32_t j=0;note[j];++j) line[at++]=note[j];
        }
        line[at]=0; learn_row(m,line);
    }
    char root[11]={'R','O','O','T',' ',0}; cv_note(root+5,notes[0],count!=0); learn_row(m,root);
    learn_row(m,status==KB_FULL ? "FULL - FOUR NOTES" : count ? "YES SAVE / NO CANCEL" : "PLAY 1-4 NOTES");
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
    if (cur->keyboard.mode!=KB_LEARN && cur->keyboard.session!=UINT32_MAX) {
        instrument_sound_suspend(IDX(cur)); result=kb_learn_begin(&cur->keyboard);
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
static int quarantined(const IpInst *in)
{
    for (uint32_t i=0;i<MI_ROUTES;++i)
        if (mi_ledger.routes[i].track==in->bound && mi_ledger.routes[i].quarantined) return 1;
    return 0;
}
/* Snapshot on the UI task under the mapper lock. Routes and performance state
 * are deliberately absent. Presets include the effective custom shape, not a
 * mutable library slot number. */
static void preset_snapshot(const IpInst *in,CpPreset *p)
{
    uint32_t sr=dkrt_irq_mask();
    const KbState *k=&in->keyboard;
    uint16_t v[16]={(uint16_t)k->type,(uint16_t)k->voicing,
        (uint16_t)in->pad_color,(uint16_t)in->pad_drift,(uint16_t)in->pad_strum,(uint16_t)in->arp_mix,
        (uint16_t)in->arp_rate,(uint16_t)in->arp_order,(uint16_t)(in->arp_octaves+3),(uint16_t)in->arp_gate,
        (uint16_t)in->arp_swing,(uint16_t)in->arp_restart,0,0,
        (uint16_t)in->pad_attack,(uint16_t)in->pad_release};
    for(uint32_t i=0;i<16;++i) p->control[i]=v[i];
    for(uint32_t i=0;i<12;++i) p->control[16+i]=(uint16_t)in->wt[i];
    for(uint32_t i=0;i<13;++i) p->fm[i]=(uint16_t)in->fm[i];
    for(uint32_t i=0;i<4;++i) p->control[28+i]=(uint16_t)in->wtfm[i];
    p->bpm=(uint16_t)in->arp_bpm; p->family=(uint8_t)in->pad_family;
    p->mode=k->mode; p->clock=(uint8_t)in->arp_clock;
    const KbShape *shape=k->preset_shape_active ? &k->preset_shape : &k->shapes[k->slot];
    p->shape.count=shape->count;
    for(uint32_t i=0;i<4;++i) p->shape.intervals[i]=shape->intervals[i];
    dkrt_irq_restore(sr);
}
static void src_requested(IpInst *in,CpPreset *current)
{
    ip_init();
    cs_native_tune(SRC(in),instrument_sound_tune(IDX(in)));
    preset_snapshot(in,current); cs_import(SRC(in),current);
}
uint32_t instrument_src_context(uint32_t track,uint32_t page,int visible)
{
    ip_init();
    IpInst *in=for_track(track);
    if(in) cur=in;
    else in=cur;
    CpPreset current; src_requested(in,&current);
    uint32_t generation=track<MI_TRACKS ? mi_ledger.generations[track] : 0;
    uint32_t token=cs_context(SRC(in),track,generation,
        visible && for_track(track)==in && keyboard_initialized && dkrt_instrument_type(),page,
        in->keyboard.mode,(uint32_t)in->arp_clock,in->keyboard.mode==KB_ROOT && in->keyboard.type==10,
        in->keyboard.mode==KB_LEARN || (learn_menu && dk_ui_is_open(learn_menu)));
    is_select(STS(in),track,generation,in->keyboard.mode,in->keyboard.mode==KB_LEARN ? in->keyboard.session : 0);
    return token;
}
int instrument_src_encoder(uint32_t token,uint32_t encoder,int32_t detents,uint32_t now)
{
    if(!initialized) return CS_STALE;
    /* A queued event can outlive a preset load or sound reset without another
     * paint. Revalidate ownership before using its captured token. */
    CsState *s=SRC(cur);
    (void)instrument_src_context(s->track,s->page[s->track<16?s->track:0],s->visible);
    s=SRC(cur);
    uint32_t id=CS_NONE;
    int result=cs_edit(s,token,encoder,detents,now,&id);
    if(result!=CS_EDIT) return result;
    uint32_t sr=dkrt_irq_mask(); int v=id<CP_CONTROLS ? s->value[id] : 0;
    switch(id) {
    case CP_CHORD: chord_apply(s->mode,(uint32_t)v); break;
    case CP_VOICING: cur->voicing=v; cur->keyboard.voicing=(uint8_t)v; cur->keyboard.update=1; break;
    case CP_COLOR: if(cur->pad_family==3) cur->fm[0]=v; else cur->pad_color=v; break;
    case CP_DRIFT: cur->pad_drift=v; break;
    case CP_STRUM: cur->pad_strum=v; break;
    case CP_ARP_MIX: cur->arp_mix=v; break;
    case CP_RATE: cur->arp_rate=v; break;
    case CP_ORDER: cur->arp_order=v; break;
    case CP_OCTAVES: cur->arp_octaves=v-3; break;
    case CP_GATE: cur->arp_gate=v; break;
    case CP_SWING: cur->arp_swing=v; break;
    case CP_RESTART: cur->arp_restart=v; break;
    case CS_CLOCK:
        cur->arp_clock=s->clock; cur->arp_rate=s->value[CP_RATE]; cur->arp_swing=s->value[CP_SWING]; break;
    case CS_BPM: cur->arp_bpm=(int)cs_choice_value(s,CS_BPM); break;
    case CS_WAVE:
        cur->pad_family=(int)((s->metadata>>4)&3u);
        cur->pad_color=s->legacy_color;
        for(uint32_t i=0;i<12;++i) cur->wt[i]=s->wavetable[i];
        for(uint32_t i=0;i<13;++i) cur->fm[i]=s->fm[i];
        break;
    default: if(id>=28 && id<CP_CONTROLS) cur->wtfm[id-28]=v;
    else if(id>=16 && id<28) {
        if(cur->pad_family==3) cur->fm[id-15]=v; else cur->wt[id-16]=v;
    } break;
    }
    cur->pad_dirty=1; dkrt_irq_restore(sr);
    return result;
}
/* Only the focused instance's status reaches the link (the DSP publishes the
 * flagged slot), so drain into that instance. */
static IpInst *focused(void) { return focus<IP_INSTANCES ? &inst[focus] : cur; }
void instrument_src_poll(uint32_t now)
{
    if(!initialized) return;
    IpInst *in=focused();
    IsStatus *st=STS(in);
    is_poll(st,now,mi_status_reply);
    if(is_detail_confirmed(st,now)) {
        if(in->pad_family==3 && st->detail_schema<4) return;
        if(st->detail_schema>=5)
            cs_applied(SRC(in),st->generation,st->detail_control,st->detail,st->detail[42]);
        else {
            uint32_t legacy[32];
            uint32_t count=st->detail_schema>=3?28:16;
            for(uint32_t i=0;i<32;++i) legacy[i]=i<count?st->detail[i]:UINT32_MAX;
            cs_applied(SRC(in),st->generation,st->detail_control,legacy,
                st->detail[st->detail_schema>=3?28:16]);
        }
    }
}
void instrument_src_display(CvDisplay *out,uint32_t now,const char *tune,const char *level)
{
    ip_init();
    CsState *s=SRC(cur);
    (void)instrument_src_context(s->track,s->page[s->track<16?s->track:0],s->visible);
    instrument_src_poll(now);
    cv_build(out,SRC(cur),STS(cur),now,tune,level,preset_result || storage_result);
    uint32_t sr=dkrt_irq_mask();
    cv_ownership(out,STS(cur),&cur->keyboard,now);
    dkrt_irq_restore(sr);
}
int instrument_src_receive(const uint32_t words[IS_WORDS],uint32_t now)
{ return initialized ? is_receive(STS(focused()),words,now) : -1; }
void instrument_src_feedback(char out[32],uint32_t encoder)
{ cv_feedback(out,SRC(cur),encoder); }
static void status_pair(dk_ui_menu *m,const char *a,const char *b)
{
    const char *first=a,*second=b;
    char row[DK_UI_ROW]; uint32_t at=0;
    while(*a && at+1<sizeof(row)) row[at++]=*a++;
    if(at+1<sizeof(row)) row[at++]=' ';
    while(*b && at+1<sizeof(row)) row[at++]=*b++;
    row[at]=0;
    /* The native right column fits 18 parameter-font characters. Keep long
     * chord/bass names and excluded-note counts whole on a separate row. */
    if(at>18) { dk_ui_text(m,first); dk_ui_text(m,second); }
    else dk_ui_text(m,row);
}
static void status_fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    ip_init();
    uint32_t now=instrument_src_now();
    IpInst *in=cur;
    uint32_t track=in->track<MI_TRACKS ? in->track : 0;
    (void)instrument_src_context(track,SRC(in)->page[track],0);
    instrument_src_poll(now);
    CsState snapshot;
    for(uint32_t i=0;i<sizeof(snapshot);++i)
        ((volatile uint8_t *)&snapshot)[i]=((const volatile uint8_t *)SRC(in))[i];
    snapshot.focus=0; snapshot.track=track; snapshot.page[track]=0;
    CvDisplay d; cv_build(&d,&snapshot,STS(in),now,"","",preset_result || storage_result);
    uint32_t sr=dkrt_irq_mask(); cv_ownership(&d,STS(in),&in->keyboard,now); dkrt_irq_restore(sr);
    status_pair(m,snapshot.mode==KB_PLAY?"PLAY":snapshot.mode==KB_ROOT?"ROOT":"LEARN",d.heading);
    status_pair(m,d.notes[0],d.notes[1]); status_pair(m,d.notes[2],d.notes[3]);
    int invalid=d.fault==1 || d.fault==2;
    dk_ui_text(m,invalid?"PEDAL --":d.pedal?"PEDAL DOWN":"PEDAL UP");
    snapshot.page[track]=1; cv_build(&d,&snapshot,STS(in),now,"","",preset_result || storage_result);
    dk_ui_text(m,d.heading);
    /* The carrier track, as the panel numbers it (T1..T16). */
    char where[9]={'T','R','A','C','K',' ','-','-',0};
    if(in->track<MI_TRACKS) {
        uint32_t n=in->track+1;
        if(n>=10) { where[6]='1'; where[7]=(char)('0'+n-10); }
        else { where[6]=(char)('0'+n); where[7]=0; }
    }
    dk_ui_text(m,where);
    status_draw_at=now;
}
/* VerticalMenuView reserves a narrow left title column. */
static const dk_ui_menu_desc status_desc={.name="canopy-status",.title="STATUS",
    .flags=DK_UI_TEXT_PAGE,.fill=status_fill};
static int preset_dirty(void)
{
    CpPreset p; uint8_t b[CP_RECORD_BYTES]; preset_snapshot(cur,&p);
    if(cp_encode(&p,b) || !(preset_present & (1u<<(preset_slot-1)))) return 1;
    /* Compare current encodings: migrating a V1 preset alone is not an edit. */
    CpPreset saved; uint8_t canonical[CP_RECORD_BYTES];
    if(cp_decode(&saved,preset_records[preset_slot-1],CP_RECORD_BYTES) || cp_encode(&saved,canonical)) return 1;
    for(uint32_t i=0;i<CP_RECORD_BYTES;++i)
        if(b[i]!=canonical[i]) return 1;
    return 0;
}
static void preset_save(void *ctx,int value)
{
    (void)ctx; (void)value;
    if(cur->keyboard.mode==KB_LEARN) { dk_ui_toast("CLOSE LEARN FIRST"); return; }
    CpPreset p; preset_snapshot(cur,&p);
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
    if(cur->keyboard.mode==KB_LEARN) { dk_ui_toast("CLOSE LEARN FIRST"); return; }
    if(!(preset_present & (1u<<(preset_slot-1))) ||
        cp_decode(&p,preset_records[preset_slot-1],CP_RECORD_BYTES)) {
        dk_ui_toast("PRESET EMPTY OR INVALID"); return;
    }
    uint32_t sr=dkrt_irq_mask();
    IpInst *in=cur;
    /* One reset boundary prevents notes from observing a partial sound change.
     * The physical ledger retains old-key tombstones. */
    instrument_sound_suspend(IDX(in));
    in->mode=in->keyboard.mode=p.mode; in->type=in->keyboard.type=(uint8_t)p.control[CP_CHORD];
    in->voicing=in->keyboard.voicing=(uint8_t)p.control[CP_VOICING];
    in->keyboard.preset_shape.count=p.shape.count;
    for(uint32_t i=0;i<4;++i) in->keyboard.preset_shape.intervals[i]=p.shape.intervals[i];
    in->keyboard.preset_shape_active=1;
    in->pad_color=p.control[CP_COLOR]; in->pad_drift=p.control[CP_DRIFT];
    in->pad_strum=p.control[CP_STRUM]; in->arp_mix=p.control[CP_ARP_MIX];
    in->arp_rate=p.control[CP_RATE]; in->arp_order=p.control[CP_ORDER]; in->arp_octaves=(int)p.control[CP_OCTAVES]-3;
    in->arp_gate=p.control[CP_GATE]; in->arp_swing=p.control[CP_SWING]; in->arp_restart=p.control[CP_RESTART];
    in->pad_attack=p.control[CP_ATTACK]; in->pad_release=p.control[CP_RELEASE];
    for(uint32_t i=0;i<12;++i) in->wt[i]=p.control[16+i];
    for(uint32_t i=0;i<13;++i) in->fm[i]=p.fm[i];
    for(uint32_t i=0;i<4;++i) in->wtfm[i]=p.control[28+i];
    in->pad_family=p.family; in->arp_clock=p.clock; in->arp_bpm=p.bpm; in->pad_dirty=1;
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
    /* Version 1 keeps the selected track's channel as the default, so a
     * single-instance setup restores exactly as before. */
    CrRoute r={(uint8_t)enabled,(uint8_t)usb,(uint8_t)din,(uint8_t)channel_of(cur),
        (uint8_t)(selector_octave+1),(uint8_t)selector_channel};
    if(cr_encode(&r,route_write) || cc_encode(track_channel,channels_write)) {
        dk_ui_toast("INVALID ROUTING - NOT SAVED"); return;
    }
    route_pending=1;
}
static const char *const scan_clocks[]={"FREE HZ","TEMPO SYNC"};
static void scan_clock_done(void *ctx,int value)
{
    (void)ctx; if(value<0 || value>1) return;
    cur->wt[CP_WT_CLOCK-16]=value; cur->wt[CP_WT_RATE-16]=value?3:10;
    pad_changed(0,0); if(tone_menu) dk_ui_refresh(tone_menu);
}
static void scan_clock_choice(void *ctx,int value)
{ (void)ctx; (void)value; dk_ui_choice("SCAN CLOCK",scan_clocks,2,scan_clock_done,0); }
static const char *const scan_restarts[]={"FREE","TRIG","PATT"};
static void scan_restart_done(void *ctx,int value)
{
    (void)ctx; if(value<0 || value>2) return;
    cur->wt[CP_WT_RESET-16]=value; pad_changed(0,0); if(tone_menu) dk_ui_refresh(tone_menu);
}
static void scan_restart_choice(void *ctx,int value)
{ (void)ctx; (void)value; dk_ui_choice("SCAN RESTART",scan_restarts,3,scan_restart_done,0); }
/* Biased amount storage is an explicit wire detail; the menu exposes a signed
 * amount, including a neutral zero, rather than unexplained 0..200 values. */
static int wheel_amount,fm_wheel_amount,wtfm_wheel_amount;
static void amount_changed(void *ctx,int value)
{
    uint32_t id=(uint32_t)(uintptr_t)ctx;
    /* The FM wheel and WT wheel share a wire ID in their different banks.
     * Menu contexts identify the bank explicitly. */
    if(id==0) cur->wt[CP_WT_WHEEL-16]=value+100;
    else if(id==1) cur->fm[CP_FM_WHEEL-15]=value+100;
    else if(id==2) cur->wtfm[CP_WTFM_WHEEL-28]=value+100;
    pad_changed(0,0);
}
static void tone_fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    wheel_amount=cur->wt[CP_WT_WHEEL-16]-100; fm_wheel_amount=cur->fm[CP_FM_WHEEL-15]-100;
    wtfm_wheel_amount=cur->wtfm[CP_WTFM_WHEEL-28]-100;
    dk_ui_action(m,scan_clocks[cur->wt[CP_WT_CLOCK-16]],scan_clock_choice,0,0);
    dk_ui_action(m,scan_restarts[cur->wt[CP_WT_RESET-16]],scan_restart_choice,0,0);
    dk_ui_value(m,"WHEEL TO POS",&wheel_amount,-100,100,amount_changed,0);
    dk_ui_value(m,"WHEEL TO FM",&fm_wheel_amount,-100,100,amount_changed,(void *)(uintptr_t)1);
    dk_ui_value(m,"WHEEL TO WT FM",&wtfm_wheel_amount,-100,100,amount_changed,(void *)(uintptr_t)2);
}
static const dk_ui_menu_desc tone_desc={.name="canopy-tone",.title="CANOPY MODULATION",.fill=tone_fill};
static void input_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    ui_channel=(int)channel_of(cur);
    dk_ui_toggle(m, "EXTERNAL MIDI", &enabled, changed, 0);
    dk_ui_toggle(m, "USB", &usb, changed, 0);
    dk_ui_toggle(m, "DIN", &din, changed, 0);
    dk_ui_value(m, "CHANNEL", &ui_channel, 1, 16, channel_changed, 0);
    dk_ui_action(m,"SAVE ROUTING",route_save,0,0);
    dk_ui_value(m,"SELECTOR OCTAVE",&selector_octave,-1,8,changed,0);
    dk_ui_value(m,"SELECT CH (0=SAME)",&selector_channel,0,16,changed,0);
    dk_ui_action(m,"PANIC",panic,0,0);
    dk_ui_action(m,"RECOVER KEYBOARD",recover,0,0);
    dk_ui_text(m, quarantined(cur) ? "RELEASE KEYS / RECOVER" : mi_transport.faults ? "RECOVER OR RESTART" :
        cur->keyboard.status==KB_RANGE ? "CHORD OUT OF RANGE" :
        cur->keyboard.status==KB_EMPTY ? "CUSTOM SHAPE EMPTY" :
        cur->conflict ? "CHANNEL IN USE" : cur->phase == 3 ? "READY" : "SELECT CANOPY ON A TRACK");
    if(route_result) dk_ui_text(m,"ROUTING NOT SAVED");
    dk_ui_text(m,"PANEL: ITS TRACK / KEYS");
}
static const dk_ui_menu_desc input_desc={.name="canopy-input",.title="CANOPY INPUT",.fill=input_fill};
static void shape_fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    dk_ui_value(m,"SHAPE SLOT",&cur->slot,1,8,changed,0);
    dk_ui_action(m,"LEARN TO ROOT",learn_begin,0,0);
    dk_ui_text(m,storage_result ? "SHAPES NOT SAVED" : cur->keyboard.dirty ? "SAVE PENDING" : "SHAPES SAVED");
    dk_ui_action(m,"RETRY SHAPE SAVE",retry_save,0,0);
    dk_ui_text(m,"PLAY SHAPE: CHRD USER");
}
static const dk_ui_menu_desc shape_desc={.name="canopy-shapes",.title="CANOPY CHORD LEARN",.fill=shape_fill};
static void fill(dk_ui_menu *m,void *ctx)
{
    (void)ctx;
    if(!input_menu) input_menu=dk_ui_menu_new(&input_desc);
    if(input_menu) dk_ui_submenu(m,"INPUT",input_menu);
    if(!shape_menu) shape_menu=dk_ui_menu_new(&shape_desc);
    if(shape_menu) dk_ui_submenu(m,"CHORD LEARN",shape_menu);
    if(!tone_menu) tone_menu=dk_ui_menu_new(&tone_desc);
    if(tone_menu) dk_ui_submenu(m,"MODULATION",tone_menu);
    if(!preset_menu) preset_menu=dk_ui_menu_new(&preset_desc);
    if(preset_menu) dk_ui_submenu(m,"PRESETS",preset_menu);
}
static const dk_ui_menu_desc desc = {.name="instrument-keyboard",.title="CANOPY SETUP",.fill=fill};
/* CANOPY is the only admitted instrument policy today. The menu asks with a
 * loaded module's validated id; a null parent is a capability query. */
int dk_app_instrument_module_settings(const uint8_t *name,dk_ui_menu *parent)
{
    static const char id[]="digikit/canopy";
    if(!name || !dkrt_instrument_type()) return 0;
    for(uint32_t i=0;i<sizeof(id);++i) if(name[i]!=(uint8_t)id[i]) return 0;
    if(!parent) return 1;
    if(!status_menu) status_menu=dk_ui_menu_new(&status_desc);
    if(status_menu) dk_ui_submenu(parent,"STATUS",status_menu);
    if(!menu) menu=dk_ui_menu_new(&desc);
    if(menu) dk_ui_submenu(parent,"SETUP",menu);
    dk_ui_text(parent,"LEVEL/DATA: TRACK LEVEL");
    dk_ui_text(parent,"SOUND H: SAMPLE LEVEL");
    dk_ui_text(parent,"SRC HOLD: SHOW VALUES");
    return 1;
}
void dk_app_instrument_settings_rows(dk_ui_menu *parent)
{
    if (!menu) menu=dk_ui_menu_new(&desc);
    if (menu) dk_ui_submenu(parent, "CANOPY", menu);
    if (!status_menu) status_menu=dk_ui_menu_new(&status_desc);
    if (status_menu) dk_ui_submenu(parent,"CANOPY STATUS",status_menu);
}
/* Enable this instance's panel and external routes. A channel already played
 * by another instance (the lower track keeps it) is skipped and reported. */
static int route_one(IpInst *in,uint32_t port,uint32_t ch)
{
    uint32_t owner_track=mi_route_track(port,ch);
    if(owner_track==in->bound) return 0;
    if(owner_track<MI_TRACKS) { in->conflict=1; return 0; }
    return mi_route_enable(port,ch,in->bound,0);
}
static int routes_enable(IpInst *in)
{
    uint32_t ch=channel_of(in)-1;
    in->conflict=0;
    if(mi_route_enable(MI_PANEL,0,in->bound,0)) return 0;
    if(!enabled) return 1;
    if(usb && route_one(in,0,ch)) return 0;
    if(din && route_one(in,1,ch)) return 0;
    if(in->keyboard.mode==KB_ROOT && selector_channel) {
        uint32_t sel=(uint32_t)selector_channel-1;
        if(usb && route_one(in,0,sel)) return 0;
        if(din && route_one(in,1,sel)) return 0;
    }
    return 1;
}
static void tick_instance(uint32_t i,const IpInst *ui)
{
    IpInst *in=&inst[i];
    cur=in;
    in->track=ip_tracks[i];
    /* The DSP bank and its presentation values come from one locked snapshot.
     * The same revision owns SRC pending markers and the audio configuration. */
    CpPreset current; src_requested(in,&current);
    if(in==ui) instrument_mod_publish(current.family);
    const CsState *s=SRC(in);
    const uint16_t *v=s->value;
    uint32_t values[32]={11|((uint32_t)current.mode<<8)|(s->native_tune<<16),current.family,
        v[CP_COLOR],v[CP_DRIFT],v[CP_ATTACK],v[CP_RELEASE],v[CP_ARP_MIX],current.bpm,
        v[CP_RATE],v[CP_GATE],v[CP_ORDER],v[CP_OCTAVES],v[CP_SWING],current.clock,
        v[CP_RESTART],(uint32_t)v[CP_STRUM]*3/2,
        v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],
        v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]};
    for(uint32_t k=0;k<16;++k) values[16+k]|=(uint32_t)v[16+k]<<16;
    /* Adapter-owned flag: this instance's status reaches the link. */
    if(i==focus) values[0]|=0x8000u;
    if(in->track>=MI_TRACKS) {
        /* Freed: release the old track; keep the sound for the next track. */
        if(in->phase) { instrument_sound_suspend(i); in->phase=0; }
        return;
    }
    if (!healthy || quarantined(in)) {
        if (in->phase == 2 || in->phase == 3) instrument_sound_suspend(i);
        return;
    }
    if (!in->phase) in->phase=1;
    if (in->phase==3 && (mi_ledger.reset_mask || in->bound!=in->track ||
        !mi_transport.applied[in->bound])) instrument_sound_suspend(i);
    {
        int result=instrument_sound_prepare(i,in->phase == 1 && !mi_ledger.reset_mask);
        if (result < 0) instrument_sound_suspend(i);
        else in->prepared=(uint32_t)result;
    }
    /* Bind DSP slot i to the instance's track before its first configuration
     * there. The reset ACK gates the submission below; the old track was
     * already released by the suspension that preceded this change. */
    if (in->phase == 1 && in->prepared && in->bound!=in->track) {
        in->bound=in->track; mi_instance_bind(in->bound,i);
    }
    uint32_t track=in->bound;
    if (in->phase == 1 && in->prepared && !mi_ledger.reset_mask &&
        !mi_config_submit(track,s->revision,0x43505342,values,32)) { in->phase=2; in->pad_dirty=0; }
    if (in->phase==3 && s->revision>mi_transport.applied[track] &&
        !mi_config_submit(track,s->revision,0x43505342,values,32)) in->pad_dirty=0;
    if (in->phase == 2 && instrument_midi_ready(track,mi_ledger.epoch,mi_ledger.generations[track])) {
        if (routes_enable(in)) in->phase=3;
        else instrument_sound_suspend(i);
    }
}
void instrument_play_tick(void)
{
    ip_init();
#if defined(__m68k__)
    extern void instrument_src_tick(void);
    instrument_src_tick();
    if(status_menu && dk_ui_is_open(status_menu)) {
        uint32_t now=instrument_src_now();
        /* Drain at the UI tick cadence, independently of the slower repaint.
         * Waiting 100 ms to drain can overflow the bounded telemetry ring. */
        instrument_src_poll(now);
        if(now-status_draw_at>=100) dk_ui_refresh(status_menu);
    }
#endif
    uint32_t reply[16];
    if (!keyboard_initialized) {
        uint8_t record[KB_RECORD_BYTES];
        int length=dkrt_settings_get(shape_key,record,sizeof(record));
        if (length>=0) storage_result=kb_decode(&inst[0].keyboard,record,(uint32_t)length);
        else if (length!=-68) storage_result=length;
        shapes_share(&inst[0]);
        preset_present=0;
        for(uint32_t i=0;i<CP_SLOTS;++i) {
            CpPreset p;
            int n=dkrt_settings_get(preset_keys[i],preset_records[i],CP_RECORD_BYTES);
            if(n>=0 && !cp_decode(&p,preset_records[i],(uint32_t)n) && !cp_encode(&p,preset_records[i]))
                preset_present|=(uint8_t)(1u<<i);
        }
        uint8_t route[CR_BYTES];
        int n=dkrt_settings_get(route_key,route,sizeof(route));
        route_restore=n>=0 && !cr_decode(&restore_route,route,(uint32_t)n);
        route_result=(n==-68 || route_restore) ? 0 : n<0 ? n : -1;
        uint8_t channels[CC_BYTES];
        for(uint32_t t=0;t<MI_TRACKS;++t) restore_channels[t]=0;
        n=dkrt_settings_get(channels_key,channels,sizeof(channels));
        if(n>=0 && cc_decode(restore_channels,channels,(uint32_t)n))
            for(uint32_t t=0;t<MI_TRACKS;++t) restore_channels[t]=0;
        keyboard_initialized=1;
    }
    /* Reading a valid record cannot open MIDI routes. Restore requested setup
     * only after this boot has an admitted instrument generation; the ordinary
     * healthy/prepared/configured handshake still owns actual route activation. */
    if(route_restore && dkrt_instrument_type() && dkrt_instrument_generation()) {
        enabled=restore_route.enabled; usb=restore_route.usb; din=restore_route.din;
        channel=restore_route.channel; selector_octave=(int)restore_route.selector_octave-1;
        selector_channel=restore_route.selector_channel;
        for(uint32_t t=0;t<MI_TRACKS;++t) track_channel[t]=restore_channels[t];
        changed(0,0);
    }
    if(route_pending) {
        route_result=dkrt_settings_put(route_key,route_write,sizeof(route_write));
        if(!route_result) route_result=dkrt_settings_put(channels_key,channels_write,sizeof(channels_write));
        if(!route_result) route_result=dkrt_settings_flush();
        route_pending=0;
        dk_ui_toast(route_result ? "ROUTING IN RAM - SAVE FAILED" : "ROUTING SAVED");
        if(input_menu && dk_ui_is_open(input_menu)) dk_ui_refresh(input_menu);
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
        uint32_t sr=dkrt_irq_mask(); kb_encode(&cur->keyboard,record); dkrt_irq_restore(sr);
        storage_result=dkrt_settings_put(shape_key,record,sizeof(record));
        if (!storage_result) storage_result=dkrt_settings_flush();
        if (!storage_result) {
            for(uint32_t i=0;i<IP_INSTANCES;++i) inst[i].keyboard.dirty=0;
        }
        save_pending=0;
        dk_ui_toast(storage_result ? "SHAPE IN RAM - SAVE FAILED" : "SHAPES SAVED");
    }
    if (learn_menu && dk_ui_is_open(learn_menu) && cur->keyboard.mode!=KB_LEARN) dk_ui_close(learn_menu);
    if (learn_menu && dk_ui_is_open(learn_menu) && (refreshed_revision!=cur->keyboard.revision || refreshed_status!=cur->keyboard.status)) {
        refreshed_revision=cur->keyboard.revision; refreshed_status=cur->keyboard.status; dk_ui_refresh(learn_menu);
    }
    uint32_t itype=dkrt_instrument_type();
    /* V2 publication completes after the stock view's boot constructor.
     * Rebuild once on this owning UI task, after the admitted rows are visible.
     * The retained row remains listed after a fault; no unload or hot reload. */
    if (itype && ip_view && listed_type != itype) {
        ip_view_build(ip_view);
        listed_type=itype;
    }
    int count=mi_registry_reply(reply,16);
    /* A temporarily missing/reordered reply cannot revoke a healthy route;
     * the transport's independent deadline and reset guard remain authoritative. */
    if (count == 16 && reply[0] == MT_PLAY_REPLY_MAGIC && reply[13] == 5 &&
        reply[14] == dkrt_instrument_generation()) fault_generation=reply[14];
    if (count == -2 || !dkrt_instrument_type()) healthy=0;
    else if (count == 16) healthy=reply[0] == MT_PLAY_REPLY_MAGIC &&
        reply[13] == 3 && reply[14] == dkrt_instrument_generation();
    instrument_sound_scan();
    for(uint32_t i=0;i<IP_INSTANCES;++i) inst[i].track=ip_tracks[i];
    IpInst *ui=ui_target();
    /* Status focus follows the UI target; a revision bump resends its bank
     * with the focus flag, which moves the DSP's status slot. */
    if(ui->track<MI_TRACKS && focus!=IDX(ui)) {
        focus=IDX(ui);
        cs_touch(SRC(ui));
    }
    for(uint32_t i=0;i<IP_INSTANCES;++i) tick_instance(i,ui);
    cur=ui;
    ip_keyboard_status[0]=cur->keyboard.mode; ip_keyboard_status[1]=cur->keyboard.type;
    ip_keyboard_status[2]=0; ip_keyboard_status[3]=cur->keyboard.capture_count;
    ip_keyboard_status[4]=cur->keyboard.session; ip_keyboard_status[5]=cur->keyboard.revision;
    ip_keyboard_status[6]=cur->keyboard.status; ip_keyboard_status[7]=cur->keyboard.dirty;
    ip_keyboard_status[8]=(uint32_t)storage_result;
    for (uint32_t i=0;i<3;++i) ip_keyboard_status[9+i]=cur->keyboard.capture[i];
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        const IpInst *in=&inst[i];
        volatile uint32_t *c=&ip_control[i*8];
        uint32_t t=in->bound<MI_TRACKS ? in->bound : 0;
        c[0]=in->phase; c[1]=(uint32_t)enabled;
        c[2]=healthy; c[3]=in->prepared;
        c[4]=mi_transport.faults; c[5]=mi_ledger.reset_mask;
        c[6]=mi_transport.applied[t]; c[7]=itype;
    }
    uint32_t view=cur->keyboard.type | ((uint32_t)cur->keyboard.mode<<4) | ((uint32_t)cur->keyboard.status<<6) |
        (cur->phase<<9) | ((uint32_t)!!storage_result<<12) | ((uint32_t)cur->keyboard.dirty<<13) |
        ((uint32_t)(quarantined(cur) || mi_transport.faults)<<14) | ((uint32_t)cur->conflict<<15) |
        (IDX(cur)<<16);
    if ((!learn_menu || !dk_ui_is_open(learn_menu)) && shown_main!=view) {
        shown_main=view;
        if(input_menu && dk_ui_is_open(input_menu)) dk_ui_refresh(input_menu);
        if(shape_menu && dk_ui_is_open(shape_menu)) dk_ui_refresh(shape_menu);
    }
}
