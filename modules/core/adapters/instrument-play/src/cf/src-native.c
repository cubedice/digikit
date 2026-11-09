/* DT2 1.17 native SRC adapter. Vtable entries and event dispatch are pinned
 * by image hashes. Untouched interfaces remain stock; no View is retained. */
#include <stdint.h>
#include "src-control.h"
#include "src-clock.h"
#include "../../../../adapters/machines/src/cf/dt2/src-pages.h"
#include "dk_fw_draw.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"
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
static uint32_t owner_type,owner_generation,published,last_draw;
static uint32_t saved_generation,saved_page,saved_valid;
static uint32_t restore_pending,restore_page,was_owned;
static CvDisplay display;
static uint32_t shown_fault;
typedef struct { uint8_t *event; uint32_t token; } Captured;
static Captured captured[64];
static uint32_t cursor;
volatile uint32_t ip_src_debug[12];
volatile uint32_t ip_src_refresh[8];
static uint32_t word(const uint8_t *p,uint32_t offset)
{ return *(const uint32_t *)(p+offset); }
static uint8_t *pointer(const uint8_t *p,uint32_t offset)
{ return (uint8_t *)(uintptr_t)word(p,offset); }
static uint32_t context(uint8_t *view)
{
    uint32_t type=dkrt_instrument_type(),gen=dkrt_instrument_generation();
    if(owner_type && (type!=owner_type || gen!=owner_generation)) {
        mf_src_unregister(owner_type,owner_generation); owner_type=0;
    }
    uint32_t track=ip_src_track(word(view,0x74)),page=word(view,0x90),pages=instrument_src_pages();
    int owns=type>=7 && type<32 && gen && track==0 && ip_src_type(view)==type &&
        !ip_src_alternate(word(view,0x6c));
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
    instrument_src_display(&display,instrument_src_now(),tune,level);
    uint32_t page=word(view,0x90);
    for(uint32_t i=0;i<8;++i) {
        /* The firmware already painted SOUND A/H and the track column.
         * Custom cells never clear or redraw any part of those controls. */
        if((page!=1 && i==7) || ((page==0 || page==2) && i==0)) continue;
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
    if(saved_valid && saved_generation==gen && ip_src_track(word(view,0x74))==0 &&
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
uint32_t ip_src_key(uint8_t *view,uint8_t *event)
{
    uint32_t token=context(view),key=word(event,12),page=word(view,0x90);
    published=0;
    (void)instrument_src_context(ip_src_debug[0],page,0);
    if(token && key>=41 && key<=48 &&
        (page==1 || (key!=48 && (page==3 || key!=41)))) return 1;
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
        if(word(view,0x90)!=1) {
            if(encoder==0 && word(view,0x90)!=3) return 0xca;
            if(encoder==7) return 0xd1;
        }
        if(encoder<8) return 0; /* custom cell: no hidden Sound parameter */
    }
    return ip_src_stock_parameter(view,encoder);
}
