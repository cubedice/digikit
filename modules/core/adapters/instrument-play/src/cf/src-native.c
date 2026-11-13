/* DT2 1.17 native SRC adapter. Vtable entries and event dispatch are pinned
 * by image hashes. Untouched interfaces remain stock; no View is retained. */
#include <stdint.h>
#include "src-control.h"
#include "src-clock.h"
#include "../../../../adapters/machines/src/cf/dt2/src-pages.h"
#include "dk_fw_draw.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"
#include "instances.h"
extern uint32_t dkrt_instrument_type(void),dkrt_instrument_generation(void);
extern uint32_t ip_src_type(uint8_t *);
extern uint32_t ip_src_track(uint32_t);
extern uint8_t ip_src_alternate(uint32_t);
extern void ip_src_stock_draw(uint8_t *,uint8_t *);
extern void ip_src_stock_update(uint8_t *);
extern uint32_t ip_src_stock_key(uint8_t *,uint8_t *);
extern uint32_t ip_src_stock_turn(uint8_t *,uint8_t *);
extern uint32_t ip_src_stock_parameter(uint8_t *,uint32_t);
extern uint32_t ip_src_stock_event(uint8_t *,uint8_t *);
extern void ip_src_stock_attach(uint8_t *,uint8_t *),ip_src_stock_detach(uint8_t *,uint8_t *);
extern int32_t ip_src_value(uint8_t *,uint32_t,uint8_t *);
extern void ip_src_tune_format(int32_t,char *),ip_src_level_format(int32_t,char *);
extern void ip_src_focus_touch(uint8_t *,uint32_t);
extern uint8_t ip_src_focus_active(uint8_t *,uint32_t);
extern const uint32_t ip_src_label_font;
extern int dk_ui_toast(const char *);
/* Sequencer UI state (finding 123): SRC view +0x6c, as KeyboardView +0x98. */
extern uint8_t ip_panel_trig_held(uint32_t),ip_panel_live_record(uint32_t);
/* Stock held-trig editor marks the gesture as an edit before touching locks.
 * Without this, releasing a short-held trig may toggle it and erase its locks. */
extern void ip_lock_mark_edited(uint32_t,uint32_t);
/* SoundParameterSet lock calls on the current track ("this" is unused). */
extern int32_t ip_lock_get(uint32_t,uint32_t,uint32_t);
extern uint8_t ip_lock_set(uint32_t,uint32_t,uint32_t,uint32_t),ip_lock_clear(uint32_t,uint32_t,uint32_t);
/* adda.l #state,a0 before the per-track step store at +0x15b (literal at +2). */
extern const uint8_t ip_seq_step[];
static uint32_t owner_type,owner_generation,published,last_draw;
static uint32_t saved_generation,saved_page,saved_valid;
static uint32_t restore_pending,restore_page,was_owned;
static uint32_t live_control=CS_NONE,live_value;
static uint32_t live_track=UINT32_MAX,live_token;
static CvDisplay display;
static uint32_t shown_fault;
typedef struct { uint8_t *event; uint32_t token; } Captured;
static Captured captured[64];
static uint32_t cursor;
volatile uint32_t ip_src_debug[12];
volatile uint32_t ip_src_refresh[8];
static int trig_held(const uint8_t *view);
static uint32_t lock_steps(uint8_t *view,uint32_t steps[4]);
static uint32_t word(const uint8_t *p,uint32_t offset)
{ return *(const uint32_t *)(p+offset); }
static uint8_t *pointer(const uint8_t *p,uint32_t offset)
{ return (uint8_t *)(uintptr_t)word(p,offset); }
static int32_t lock_get(void *ctx,uint32_t code,uint32_t step)
{ (void)ctx; return ip_lock_get(0,code,step); }
static int lock_set(void *ctx,uint32_t code,uint32_t step,uint32_t value)
{ (void)ctx; return ip_lock_set(0,code,value,step); }
static int lock_remove(void *ctx,uint32_t code,uint32_t step)
{ (void)ctx; return ip_lock_clear(0,code,step); }
static const IlpStorage lock_store={0,lock_get,lock_set,lock_remove};
static void lock_snapshot(uint32_t step,IlpSnapshot *snapshot)
{
    static const uint8_t codes[ILP_LANES]={0xcb,0xcc,0xce,0xcf,0xd0};
    snapshot->count=0;
    for(uint32_t i=0;i<ILP_LANES;++i) {
        uint32_t id,value;
        if(ilp_unpack(ip_lock_get(0,codes[i],step),&id,&value)) {
            snapshot->pair[snapshot->count].id=(uint16_t)id;
            snapshot->pair[snapshot->count].value=(uint16_t)value;
            ++snapshot->count;
        }
    }
}
static uint32_t context(uint8_t *view)
{
    uint32_t type=dkrt_instrument_type(),gen=dkrt_instrument_generation();
    if(owner_type && (type!=owner_type || gen!=owner_generation)) {
        mf_src_unregister(owner_type,owner_generation); owner_type=0;
    }
    uint32_t track=ip_src_track(word(view,0x74)),page=word(view,0x90),pages=instrument_src_pages();
    int alternate=ip_src_alternate(word(view,0x6c));
    /* The SRC page's track is the UI selection for menus, presets and focus. */
    if(!alternate && track<16) instrument_select_track(track);
    int owns=type>=7 && type<32 && gen && track<16 && instrument_track_owned(track) &&
        ip_src_type(view)==type && !alternate;
    if(owns) {
        if(restore_pending) {
            page=restore_page; *(uint32_t *)(view+0x90)=page;
        } else if(!was_owned && saved_valid && saved_generation==gen) {
            page=saved_page; *(uint32_t *)(view+0x90)=page;
        }
        if(page>=pages) { page=2; *(uint32_t *)(view+0x90)=page; }
        MfSrcCapability cap={MF_SRC_ABI,sizeof(cap),pages,gen};
        if(mf_src_register(type,&cap)) owns=0;
        else { owner_type=type; owner_generation=gen; }
        saved_generation=gen; saved_page=page; saved_valid=1;
    }
    if(page>=pages) page=0;
    was_owned=(uint32_t)owns;
    published=instrument_src_context(track,page,owns);
    ip_src_debug[0]=track; ip_src_debug[1]=page; ip_src_debug[2]=published;
    return published;
}
uint32_t ip_src_event(uint8_t *controller,uint8_t *event)
{
    ++ip_src_debug[7]; ip_src_debug[8]=published;
    for(uint32_t i=0;i<64;++i) if(captured[i].event==event) captured[i].event=0;
    /* Capture the copy at dispatch, after the stock constructor/copy path.
     * ViewController closures retain this pointer. Ring exhaustion drops a
     * custom edit instead of assigning an event the destination's context. */
    captured[cursor].event=event; captured[cursor].token=published;
    cursor=(cursor+1)&63;
    return ip_src_stock_event(controller,event);
}
static uint32_t take(uint8_t *event)
{
    for(uint32_t n=0;n<64;++n) {
        uint32_t i=(cursor-1-n)&63;
        if(captured[i].event==event) {
            uint32_t token=captured[i].token; captured[i].event=0; return token;
        }
    }
    return 0;
}
void ip_src_draw(uint8_t *view,uint8_t *canvas)
{
    uint32_t token=context(view);
    restore_pending=0;
    ip_src_stock_draw(view,canvas);
    if(!token) return;
    char tune[32],level[32]; uint8_t flags=0;
    tune[0]=0; level[0]=0;
    int32_t raw=ip_src_value(view,0xca,&flags);
    if(raw>=0x400 && raw<=0x7c00) ip_src_tune_format(raw,tune);
    raw=ip_src_value(view,0xd1,&flags);
    if(raw>=0 && raw<=0x7f00) ip_src_level_format(raw,level);
    uint32_t steps[4],first=trig_held(view) ? lock_steps(view,steps) : 128;
    IlpSnapshot locked;
    if(first<128) {
        /* A held trig shows what it plays: its locks, else the Sound. */
        lock_snapshot(first,&locked);
        instrument_src_display_locked(&display,instrument_src_now(),tune,level,&locked);
    } else instrument_src_display(&display,instrument_src_now(),tune,level);
    uint32_t page=word(view,0x90);
    for(uint32_t i=0;i<8;++i) {
        /* The firmware already painted SOUND A/H and the track column.
         * Custom cells never clear or redraw any part of those controls. */
        if(page==0 && (i==0 || i==7)) continue;
        int32_t x=24+26*(int32_t)(i%4),y=i<4?27:0;
        dk_fw_draw_fill_rect(canvas,x-1,y,x+21,y+25,0);
        if(display.rotary&(1u<<i)) {
            /* Reuse the device's native encoder template (CFADE) with a
             * supplied value, without reading/writing its Sound slot.
             * Accessor and state are borrowed only for this paint call. */
            uint32_t *vt=(uint32_t *)pointer(view,0);
            uint8_t *(*access)(uint8_t *,uint32_t,int32_t)=(void *)(uintptr_t)vt[0xc4/4];
            uint8_t *a=access(view,0xcc,-1);
            uint32_t *av=(uint32_t *)pointer(a,0);
            void (*draw)(uint8_t *,uint32_t,int32_t,uint32_t,uint8_t *,uint8_t *,int32_t,int32_t)=
                (void *)(uintptr_t)av[0x60/4];
            draw(a,0xcc,(int32_t)display.knob[i]<<8,0,view+0xb4+20*i,canvas,x,y+7);
        } else {
            dk_fw_text_draw_box(canvas,(uint8_t *)(uintptr_t)ip_src_label_font,
                x+9,y+14,1,0,0,"%s",display.value[i]);
        }
        /* 0x190 is the firmware's show-values state; native edit focus uses
         * its own countdown. Do not implement another SRC key/timer policy. */
        int values=view[0x190] || ip_src_focus_active(view+0x94,i);
        dk_fw_text_draw_box(canvas,(uint8_t *)(uintptr_t)ip_src_label_font,
            x+9,y,1,0,0,"%s",values?display.value[i]:display.label[i]);
    }
    if(display.fault && display.fault!=shown_fault) dk_ui_toast(display.heading);
    shown_fault=display.fault;
    last_draw=instrument_src_now(); ++ip_src_debug[3];
}
void ip_src_attach(uint8_t *view,uint8_t *manager)
{
    uint32_t gen=dkrt_instrument_generation();
    ip_src_stock_attach(view,manager);
    if(saved_valid && saved_generation==gen && instrument_track_owned(ip_src_track(word(view,0x74))) &&
        ip_src_type(view)==dkrt_instrument_type()) {
        restore_page=saved_page; restore_pending=1;
        *(uint32_t *)(view+0x90)=saved_page;
    } else restore_pending=0;
}
void ip_src_detach(uint8_t *view,uint8_t *manager)
{
    (void)context(view);
    ip_src_stock_detach(view,manager);
    was_owned=0;
    published=0; (void)instrument_src_context(ip_src_debug[0],ip_src_debug[1],0);
}
void ip_src_update(uint8_t *view)
{
    ip_src_stock_update(view);
    uint32_t seq=word(view,0x6c);
    if(!seq || !ip_panel_live_record(seq)) live_control=CS_NONE;
    if(context(view) && instrument_src_now()-last_draw>=40) dk_fw_view_invalidate(view);
}
void instrument_src_tick(void)
{
    ++ip_src_refresh[0];
    /* UI-task ownership: resolve the current visible view afresh. The stock
     * SRC update callback alone does not schedule idle animation. Never keep
     * a view pointer across ticks or animate underneath an opaque modal. */
    uint8_t *brain=dk_fw_view_brain_instance();
    uint8_t *manager=brain?dk_fw_view_brain_view_manager(brain):0;
    ip_src_refresh[1]=(uint32_t)(uintptr_t)brain;
    ip_src_refresh[2]=(uint32_t)(uintptr_t)manager;
    if(manager) {
        /* Use the firmware's resolved first drawable node. The per-view
         * visibility flag is a virtual policy, not a raw opaque boolean. */
        uint8_t *sentinel=manager+0x14,*node=pointer(manager,0x1c);
        ip_src_refresh[3]=(uint32_t)(uintptr_t)node;
        for(uint32_t n=0;n<32 && node && node!=sentinel;++n,node=pointer(node,0)) {
            uint8_t *view=pointer(node,8);
            if(!view || view[0x30]) continue;
            uint8_t *vtable=pointer(view,0);
            if(vtable && word(vtable,16)==(uint32_t)(uintptr_t)ip_src_draw) {
                ++ip_src_refresh[4];
                uint32_t now=instrument_src_now();
                ip_src_refresh[5]=now; ip_src_refresh[6]=last_draw;
                if(context(view)) {
                    instrument_src_poll(now);
                    if(now-last_draw>=40) {
                        ++ip_src_refresh[7]; dk_fw_view_invalidate(view);
                    }
                }
                return;
            }
        }
    }
    was_owned=0; published=0; (void)instrument_src_context(ip_src_debug[0],ip_src_debug[1],0);
}
/* Packed trig locks. Grid recording: the held trigs (128-bit mask at
 * +0x258, below the pattern length). Live recording: the track's current
 * step. Returns the first step, or 128 when neither applies. */
static int trig_held(const uint8_t *view)
{ uint32_t seq=word(view,0x6c); return seq && ip_panel_trig_held(seq); }
static uint32_t lock_steps(uint8_t *view,uint32_t steps[4])
{
    uint32_t seq=word(view,0x6c);
    steps[0]=steps[1]=steps[2]=steps[3]=0;
    if(!seq) return 128;
    if(trig_held(view)) {
        for(uint32_t i=0;i<4;++i) steps[i]=word((const uint8_t *)(uintptr_t)seq,0x258+4*i);
        for(uint32_t s=0;s<128;++s) if(steps[s>>5]>>(s&31)&1u) return s;
        return 128;
    }
    uint32_t track=ip_src_debug[0];
    if(track>=16 || !ip_panel_live_record(seq)) return 128;
    const uint8_t *state=(const uint8_t *)(uintptr_t)*(const uint32_t *)(ip_seq_step+2);
    uint32_t step=state[0x15b+track];
    if(step>=128) return 128;
    steps[step>>5]=1u<<(step&31);
    return step;
}
/* A live-recording sweep continues from its last value across steps. */
static uint32_t lock_turn(uint8_t *view,uint8_t *event,uint32_t token,uint32_t encoder,
    const uint32_t steps[4],uint32_t first)
{
    uint32_t control=0,id=0,base=0,next=0,current=0;
    IlpSnapshot snapshot; lock_snapshot(first,&snapshot);
    int result=instrument_src_lock_target(encoder,&snapshot,&control,&id,&base);
    if(result==CS_NATIVE) return ip_src_stock_turn(view,event);
    int held=trig_held(view);
    if(result==CS_EDIT) {
        if(held) ip_lock_mark_edited(word(view,0x6c),1);
        int found=ilp_store_read(&lock_store,first,id,&current);
        if(found<0) { dk_ui_toast("LOCK SLOT IN USE"); return 1; }
        if(!found) current=base;
        if(!held && live_control==id && live_track==ip_src_debug[0] && live_token==token) current=live_value;
        result=instrument_src_lock_step(token,encoder,(int32_t)word(event,16),instrument_src_now(),&snapshot,current,&next);
    } else if(result==CS_READONLY) dk_ui_toast("NOT LOCKABLE");
    if(result==CS_EDIT) {
        int written=ilp_store_edit_steps(&lock_store,steps,id,next,0);
        if(written!=1) dk_ui_toast(written==ILP_ROLLBACK_FAILED ? "LOCK RESTORE FAILED" :
            written==ILP_FULL || written==ILP_STORAGE_FULL ? "LOCKS FULL" : "LOCK SLOT IN USE");
        else {
            live_control=held ? CS_NONE : id; live_value=next;
            live_track=ip_src_debug[0]; live_token=token;
            char feedback[32]; instrument_src_lock_feedback(feedback,encoder,&snapshot,control,next);
            if(feedback[0]) dk_ui_toast(feedback);
        }
        ip_src_focus_touch(view+0x94,encoder);
        ++ip_src_debug[5];
    }
    dk_fw_view_invalidate(view); return 1;
}
/* Holding a trig and pressing a CANOPY encoder clears that control's lock. */
static int lock_clear(uint8_t *view,uint32_t encoder,const uint32_t steps[4])
{
    uint32_t control,id,base;
    uint32_t first=128;
    for(uint32_t s=0;s<128;++s) if(steps[s>>5]>>(s&31)&1u) { first=s; break; }
    if(first==128) return 0;
    IlpSnapshot snapshot; lock_snapshot(first,&snapshot);
    if(instrument_src_lock_target(encoder,&snapshot,&control,&id,&base)!=CS_EDIT) return 0;
    ip_lock_mark_edited(word(view,0x6c),1);
    int result=ilp_store_edit_steps(&lock_store,steps,id,0,1);
    if(result<0) dk_ui_toast(result==ILP_ROLLBACK_FAILED ? "LOCK RESTORE FAILED" : "LOCK SLOT IN USE");
    return 1;
}
uint32_t ip_src_key(uint8_t *view,uint8_t *event)
{
    uint32_t token=context(view),key=word(event,12),page=word(view,0x90),steps[4];
    if(token && key>=41 && key<=48 && trig_held(view) &&
        lock_steps(view,steps)<128 && lock_clear(view,key-41,steps)) {
        dk_fw_view_invalidate(view); return 1;
    }
    published=0;
    (void)instrument_src_context(ip_src_debug[0],page,0);
    if(token && key>=41 && key<=48 && (page!=0 || (key!=48 && key!=41))) return 1;
    return ip_src_stock_key(view,event);
}
uint32_t ip_src_turn(uint8_t *view,uint8_t *event)
{
    uint32_t captured_token=take(event),token=context(view),encoder=word(event,12);
    ++ip_src_debug[4];
    ip_src_debug[9]=captured_token; ip_src_debug[10]=encoder;
    if(!token) {
        if(captured_token) { ++ip_src_debug[6]; return 1; }
        return ip_src_stock_turn(view,event);
    }
    if(encoder<1 || encoder>8) return ip_src_stock_turn(view,event);
    uint32_t steps[4],first=lock_steps(view,steps);
    if(first<128) return lock_turn(view,event,captured_token,encoder-1,steps,first);
    live_control=CS_NONE;
    int result=instrument_src_encoder(captured_token,encoder-1,(int32_t)word(event,16),instrument_src_now());
    ip_src_debug[11]=(uint32_t)result;
    if(result==CS_NATIVE) return ip_src_stock_turn(view,event);
    if(result!=CS_STALE) {
        ip_src_focus_touch(view+0x94,encoder-1);
        char feedback[32]; instrument_src_feedback(feedback,encoder-1);
        if(display.fault) dk_ui_toast(display.heading);
        else if(feedback[0]) dk_ui_toast(feedback);
    }
    if(result==CS_EDIT) ++ip_src_debug[5];
    if(result==CS_STALE) ++ip_src_debug[6];
    dk_fw_view_invalidate(view); return 1;
}
uint32_t ip_src_encoder_interface(uint8_t *receiver,uint8_t *event)
{ return ip_src_turn(receiver-4,event); }
uint32_t ip_src_parameter(uint8_t *view,uint32_t encoder)
{
    /* V2 has no legacy DK_PARAM descriptor. Keep the native editing path,
     * including modifier handling and Sound persistence, for SOUND A/H. */
    if(context(view)) {
        if(encoder==8) return 10;
        if(word(view,0x90)==0) {
            if(encoder==0) return 0xca;
            if(encoder==7) return 0xd1;
        }
        if(encoder<8) return 0; /* custom cell: no hidden Sound parameter */
    }
    return ip_src_stock_parameter(view,encoder);
}
