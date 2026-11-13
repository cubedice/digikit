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
#include "src-waves.h"
#include "src-envelope.h"
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
/* Diagrams use top-left display coordinates. The firmware primitive uses a
 * bottom-left origin; every shape remains inside the custom SRC body. */
static void diagram_box(uint8_t *b,int x,int y,int w,int h,int ink)
{
    if(w>0 && h>0) dk_fw_draw_fill_rect(b,x,64-y-h,x+w-1,63-y,ink);
}
static void diagram_line(uint8_t *b,int x0,int y0,int x1,int y1)
{
    if(y0==y1) { int lo=x0<x1?x0:x1,hi=x0>x1?x0:x1; diagram_box(b,lo,y0,hi-lo+1,1,1); return; }
    if(x0==x1) { int lo=y0<y1?y0:y1,hi=y0>y1?y0:y1; diagram_box(b,x0,lo,1,hi-lo+1,1); return; }
    int dx=x1>x0?x1-x0:x0-x1,dy=y1>y0?y1-y0:y0-y1;
    int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
    for(;;) {
        diagram_box(b,x0,y0,1,1,1);
        if(x0==x1 && y0==y1) break;
        int e=2*err;
        if(e>-dy) { err-=dy; x0+=sx; }
        if(e<dx) { err+=dx; y0+=sy; }
    }
}
static void diagram_rect(uint8_t *b,int x,int y,int w,int h)
{
    diagram_box(b,x,y,w,1,1); diagram_box(b,x,y+h-1,w,1,1);
    diagram_box(b,x,y,1,h,1); diagram_box(b,x+w-1,y,1,h,1);
}
static void diagram_text(uint8_t *b,int x,int y,const char *text)
{ dk_fw_text_draw_box(b,(uint8_t *)(uintptr_t)ip_src_label_font,x,59-y,1,0,0,"%s",text); }
static void diagram_node(uint8_t *b,int x,int y)
{ diagram_box(b,x-1,y-1,3,3,1); diagram_box(b,x,y,1,1,0); }
static uint32_t diagram_sqrt(uint32_t n)
{
    uint32_t result=0,bit=1u<<30;
    while(bit>n) bit>>=2;
    while(bit) { if(n>=result+bit) { n-=result+bit; result=(result>>1)+bit; } else result>>=1; bit>>=2; }
    return result;
}
static int native_index(const CvDisplay *d,uint32_t id)
{ return d->raw[id]<128 ? d->raw[id] : 127; }
static void diagram_envelope(uint8_t *b,const CvDisplay *d,int fm)
{
    uint32_t attack=canopy_env_weight[0][native_index(d,fm?CP_FM_ATTACK:CP_WT_ATTACK)];
    uint32_t release=canopy_env_weight[1][native_index(d,fm?CP_FM_RELEASE:CP_WT_RELEASE)];
    uint32_t decay=0,sustain=100;
    if(fm) {
        sustain=d->raw[CP_FM_SUSTAIN]; if(sustain>100) sustain=100;
        decay=(canopy_env_weight[0][native_index(d,CP_FM_DECAY)]*diagram_sqrt((100-sustain)*100))/100;
        release=(release*diagram_sqrt(sustain*100))/100;
    }
    uint32_t available=fm?72:39,total=attack+decay+release;
    int x0=24,x1=x0+(int)(total?(available*attack+total/2)/total:0);
    int x2=x1+(int)(total?(available*decay+total/2)/total:0);
    int end=fm?121:71,hold=fm?25:8,x3=x2+hold;
    if(x3>end) x3=end;
    int y=fm?29-(int)((16*sustain+50)/100):13;
    diagram_line(b,x0,29,x1,13);
    if(fm) diagram_line(b,x1,13,x2,y);
    else x2=x1;
    diagram_line(b,x2,y,x3,y); diagram_line(b,x3,y,end,29);
    if(d->focus_id==(fm?CP_FM_ATTACK:CP_WT_ATTACK)) diagram_line(b,x0,30,x1,14);
    if(fm && d->focus_id==CP_FM_DECAY) diagram_line(b,x1,14,x2,y+1);
    if(fm && d->focus_id==CP_FM_SUSTAIN) diagram_line(b,x2,y+1,x3,y+1);
    if(d->focus_id==(fm?CP_FM_RELEASE:CP_WT_RELEASE)) diagram_line(b,x3,y+1,end,30);
    diagram_node(b,x1,13);
    if(fm) diagram_node(b,x2,y);
    diagram_node(b,x3,y);
    if(x3+1<124) for(int j=12;j<30;j+=2) diagram_box(b,x3+1,j,1,1,1);
}
static void diagram_keyboard(uint8_t *b,const CvDisplay *d)
{
    static const uint8_t white[]={0,2,4,5,7,9,11},black[]={1,3,6,8,10},after[]={0,1,3,4,5};
    uint32_t pcs=0;
    if(d->fresh && d->fault!=2) for(uint32_t i=0;i<d->note_count && i<4;++i) pcs|=1u<<(d->pitches[i]%12);
    for(int i=0;i<7;++i) {
        int x=24+7*i; diagram_rect(b,x,40,7,17);
        if(pcs&(1u<<white[i])) diagram_box(b,x+1,50,5,6,1);
    }
    for(int i=0;i<5;++i) {
        int x=28+7*after[i]; diagram_box(b,x,40,5,10,0); diagram_rect(b,x,40,5,9);
        if(pcs&(1u<<black[i])) diagram_box(b,x+1,41,3,7,1);
    }
    char caption[10]; uint32_t i=0;
    const char *s=!d->fresh?"LINK LOST":d->fault==2?"VOICE ERR":d->playing;
    while(i<9 && s[i]) { caption[i]=s[i]; ++i; } caption[i]=0;
    diagram_text(b,48,59,caption);
    diagram_text(b,78,40,"P"); diagram_text(b,93,40,"A");
    diagram_line(b,77,50,95,50); diagram_line(b,77,48,77,52); diagram_line(b,95,48,95,52);
    int pos=77+(18*d->raw[CP_ARP_MIX]+63)/127;
    diagram_box(b,pos-1,47,3,7,1);
}
static void diagram_keys(uint8_t *b,const CvDisplay *d)
{
    diagram_text(b,85,14,d->value[2]);
    if(d->value[2][0]!='-') for(int i=0;i<4;++i) {
        int x=78+5*i,y=27-2*i;
        diagram_line(b,x,y,x+2,y); diagram_line(b,x+2,y-3,x+2,y);
    }
    if(d->value[3][0]=='-') diagram_text(b,111,19,"--");
    else for(int i=0;i<4;++i) {
        int stride=d->raw[CP_STRUM] ? 1+(int)(2*d->raw[CP_STRUM]/500) : 0;
        int y=14+4*i,x=103+stride*i;
        diagram_box(b,x,y,3,2,1); diagram_line(b,x+4,y,120,y);
    }
    diagram_keyboard(b,d);
}
static void diagram_arp(uint8_t *b,const CvDisplay *d)
{
    uint32_t order=d->raw[CP_ORDER],oct=d->raw[CP_OCTAVES],gate=d->raw[CP_GATE],swing=d->raw[CP_SWING];
    if(oct>6) oct=6;
    if(order>=3) diagram_text(b,59,19,order==3?"PLAY":"RND");
    else for(int i=0;i<4;++i) {
        int rank=order==1?3-i:order==2?(i<2?i:3-i):i;
        diagram_line(b,51+4*i,28-4*rank,54+4*i,28-4*rank);
    }
    diagram_text(b,85,14,d->value[2]);
    for(int i=0;i<3;++i) {
        int y=oct<3?19+4*i:oct>3?27-4*i:23;
        diagram_line(b,77+6*i,y,83+6*i,y);
        if(i<2 && oct!=3) diagram_line(b,83+6*i,y,83+6*i,oct<3?y+4:y-4);
    }
    int width=(int)((17*gate+50)/100); if(width<1) width=1;
    diagram_line(b,102,27,104,27); diagram_line(b,104,27,104,17);
    diagram_line(b,104,17,104+width,17); diagram_line(b,104+width,17,104+width,27);
    diagram_line(b,104+width,27,121,27);
    int middle=25+(int)((17*(100+swing)+100)/200); if(middle>41) middle=41;
    diagram_line(b,24,52,43,52);
    diagram_line(b,25,44,25,52); diagram_line(b,middle,44,middle,52); diagram_line(b,42,44,42,52);
    diagram_box(b,26,45,middle-26,2,1); diagram_box(b,middle+1,45,42-middle-1,2,1);
    diagram_text(b,59,43,d->value[5]); diagram_line(b,51,52,66,52);
    diagram_line(b,51,49,51,52); diagram_line(b,64,50,53,50); diagram_line(b,53,50,54,49);
    if(order>=3) diagram_text(b,98,47,order==3?"PLAY":"RND");
    else {
      uint8_t candidates[16]; uint32_t count=0;
      if(order<2 && d->fresh && d->fault!=2) {
        uint32_t spread=oct<3?3-oct:oct-3;
        for(uint32_t o=0;o<=spread;++o) for(uint32_t k=0;k<d->note_count && k<4;++k) {
            int pitch=(int)d->pitches[k]+(oct<3?-12:12)*(int)o;
            if(pitch<0 || pitch>127) continue;
            uint32_t at=count;
            while(at && candidates[at-1]>(uint8_t)pitch) { candidates[at]=candidates[at-1]; --at; }
            candidates[at]=(uint8_t)pitch; ++count;
        }
      }
      for(int i=0,marked=0;i<8;++i) {
        int rank=order==1?7-i:order==2?(i<4?i:7-i):i;
        int y=54-2*rank;
        uint32_t step=0;
        if(count) {
            step=(uint32_t)i%count;
            if(order==1) step=count-1-step;
            int range=candidates[count-1]-candidates[0];
            y=range ? 54-14*(candidates[step]-candidates[0])/range : 47;
        }
        diagram_line(b,76+6*i,y,79+6*i,y);
        /* Mark only an actual candidate from the fresh, pitch-sorted set. */
        if(count && d->arp_active && d->raw[CP_ARP_MIX] &&
           d->clock_state!=IS_CLOCK_LOST && d->clock_state!=IS_CLOCK_WAIT &&
           d->clock_state!=IS_CLOCK_STOPPED && !marked) {
            if(candidates[step]==d->arp_note) { diagram_rect(b,75+6*i,y-2,6,5); marked=1; }
        }
      }
    }
    if(!d->fresh) diagram_text(b,98,59,"LINK LOST");
    else if(d->fault==2) diagram_text(b,98,59,"VOICE ERR");
    else if(!d->raw[CP_ARP_MIX]) diagram_text(b,98,59,"ARP OFF");
    else if(!d->note_count) diagram_text(b,98,59,"NO NOTES");
    else if(d->clock_state==IS_CLOCK_LOST) diagram_text(b,98,59,"CLK LOST");
    else if(d->clock_state==IS_CLOCK_WAIT) diagram_text(b,98,59,"WAIT CLK");
    else if(d->clock_state==IS_CLOCK_STOPPED) diagram_text(b,98,59,"STOPPED");
    else if(d->arp_active) { char note[6]; cv_note(note,d->arp_note,1); diagram_text(b,98,59,note); }
    else diagram_text(b,98,59,"ARP --");
}
static void diagram_wavetable(uint8_t *b,const CvDisplay *d)
{
    uint32_t table=d->raw[CP_WT_TABLE],position=d->raw[CP_WT_POS],depth=d->raw[CP_WT_DEPTH];
    if(table>4) table=4;
    if(position>700) position=700;
    if(depth>100) depth=100;
    uint32_t frame=position/100,fraction=position%100,next=frame<7?frame+1:frame;
    int samples[74],peak=1;
    for(int i=0;i<74;++i) {
        samples[i]=((int)canopy_preview_waves[table][frame][i]*(int)(100-fraction)+
            (int)canopy_preview_waves[table][next][i]*(int)fraction)/100;
        int abs=samples[i]<0?-samples[i]:samples[i]; if(abs>peak) peak=abs;
    }
    int magnitude=(6*(samples[0]<0?-samples[0]:samples[0])+peak/2)/peak;
    int last=20+(samples[0]<0?magnitude:-magnitude);
    for(int i=1;i<74;++i) {
        magnitude=(6*(samples[i]<0?-samples[i]:samples[i])+peak/2)/peak;
        int y=20+(samples[i]<0?magnitude:-magnitude);
        diagram_line(b,48+i,last,49+i,y); last=y;
    }
    diagram_line(b,49,29,121,29);
    for(int i=0;i<8;++i) {
        int x=49+(72*i+3)/7; diagram_line(b,x,28,x,30);
    }
    int x=49+(int)((72*position+350)/700),radius=(int)((72*depth+100)/200);
    int lo=x-radius,hi=x+radius; if(lo<49) lo=49; if(hi>121) hi=121;
    diagram_line(b,lo,12,hi,12); diagram_line(b,lo,12,lo,14); diagram_line(b,hi,12,hi,14);
    diagram_line(b,x,13,x,30); diagram_box(b,x-1,27,3,3,1);
    /* The scan-shape glyph is qualitative; RAND intentionally promises no
     * future random trajectory. */
    if(d->raw[CP_WT_SHAPE]==2) diagram_text(b,33,46,"RAND");
    else {
        static const int8_t sine[]={0,2,4,5,5,4,2,0,-2,-4,-5,-5,-4,-2,0,1,0,0};
        for(int i=1;i<18;++i) {
            int a=d->raw[CP_WT_SHAPE] ? (i-1<9 ? (i-1)*5/4 : (17-(i-1))*5/4)-5 : sine[i-1];
            int c=d->raw[CP_WT_SHAPE] ? (i<9 ? i*5/4 : (17-i)*5/4)-5 : sine[i];
            diagram_line(b,24+i,48-a,25+i,48-c);
        }
    }
}
static void diagram_operator(uint8_t *b,int x,int y,const char *name)
{ diagram_rect(b,x,y,13,8); diagram_text(b,x+6,y+2,name); }
static void diagram_fm(uint8_t *b,const CvDisplay *d)
{
    uint32_t alg=d->raw[CP_FM_ALG];
    diagram_operator(b,50,12,"A"); diagram_operator(b,104,12,"B");
    diagram_operator(b,50,23,"X"); diagram_operator(b,104,23,"Y");
    diagram_text(b,82,13,d->value[2]);
    if(alg==0) { diagram_line(b,56,20,56,22); diagram_line(b,110,20,110,22); }
    else if(alg==1) { /* B -> A -> X; Y is clean. */
        diagram_line(b,56,11,110,11); diagram_box(b,56,12,1,1,1); diagram_line(b,56,20,56,22);
    } else if(alg==2) { /* A+B -> X; Y is clean. */
        diagram_line(b,56,20,56,22); diagram_line(b,103,19,64,22);
    } else { /* B -> A -> X -> Y. */
        diagram_line(b,56,11,110,11); diagram_box(b,56,12,1,1,1); diagram_line(b,56,20,56,22);
        diagram_line(b,63,27,103,27);
        diagram_line(b,100,26,102,27); diagram_line(b,100,28,102,27);
    }
    if(alg!=3) { diagram_line(b,63,27,103,27); }
    int balance=76+(int)((18*d->raw[CP_FM_BAL]+50)/100);
    diagram_box(b,balance-1,25,2,5,1);
    /* Feedback loop on B. */
    diagram_line(b,117,13,120,13); diagram_line(b,120,13,120,19); diagram_line(b,120,19,117,19);
}
static void diagram_legacy(uint8_t *b,const CvDisplay *d)
{
    static const int8_t wave[]={0,2,4,6,7,7,6,5,4,3,3,3,3,2,0,-2,-4,-6,-7,-7,-6,-5,-4,-3,-3,-3,-3,-2,0};
    for(int i=1;i<92;++i) {
        int a=wave[(i-1)*28/91],c=wave[i*28/91];
        if(d->family) {
            int pa=(i-1)*28/91,pc=i*28/91;
            a=pa<14 ? 7-pa : pa-21;
            c=pc<14 ? 7-pc : pc-21;
        }
        diagram_line(b,26+i,48-a,27+i,48-c);
    }
    diagram_text(b,73,59,d->family?"REED":"VELVET");
}
static void diagram_routing(uint8_t *b,int wt)
{
    if(wt) {
        diagram_text(b,110,40,"ENV"); diagram_line(b,110,46,110,50);
        diagram_line(b,103,50,118,50); diagram_line(b,103,50,103,52); diagram_line(b,118,50,118,52);
        diagram_text(b,104,54,"P"); diagram_text(b,118,54,"F");
    } else {
        diagram_text(b,82,42,"ENV"); diagram_line(b,93,44,101,44); diagram_line(b,100,43,101,44); diagram_text(b,110,42,"FM");
        diagram_text(b,82,53,"AMP"); diagram_line(b,93,55,101,55); diagram_line(b,100,54,101,55); diagram_text(b,111,53,"VOL");
    }
}
static uint32_t diagram_mask(uint32_t page,uint32_t family)
{
    if(page==0) return 0x7c;
    if(page==1) return 0xfe;
    if(page==2) return family==2?0x1e:family==3?0x0e:0xf8;
    return family==2?0x83:family==3?0xcf:0xff;
}
static void diagram_draw(uint8_t *b,const CvDisplay *d)
{
    if(d->page==0) diagram_keys(b,d);
    else if(d->page==1) diagram_arp(b,d);
    else if(d->page==2) {
        if(d->family==2) diagram_wavetable(b,d);
        else if(d->family==3) diagram_fm(b,d);
        else diagram_legacy(b,d);
    } else if(d->family==2) { diagram_envelope(b,d,0); diagram_routing(b,1); }
    else if(d->family==3) { diagram_envelope(b,d,1); diagram_routing(b,0); }
    else {
        diagram_text(b,73,17,d->family?"REED":"VELVET");
        diagram_text(b,73,29,"TONE ON SOUND");
        diagram_text(b,73,43,"VOLUME ENVELOPE");
        diagram_text(b,73,52,"ON AMP");
    }
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
    uint32_t mask=diagram_mask(page,display.family);
    if(page==0) {
        dk_fw_draw_fill_rect(canvas,49,27,123,52,0);
        dk_fw_draw_fill_rect(canvas,23,0,100,25,0);
    } else dk_fw_draw_fill_rect(canvas,23,0,123,52,0);
    for(uint32_t i=0;i<8;++i) {
        /* The firmware already painted SOUND A/H and the track column.
         * Custom cells never clear or redraw any part of those controls. */
        if(page==0 && (i==0 || i==7)) continue;
        int32_t x=24+26*(int32_t)(i%4),y=i<4?27:0;
        if(mask&(1u<<i)) {
            /* The shared diagram is painted after all parameter labels. */
        } else if(display.rotary&(1u<<i)) {
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
        if(display.label[i][0]) {
            int values=view[0x190] || ip_src_focus_active(view+0x94,i);
            dk_fw_text_draw_box(canvas,(uint8_t *)(uintptr_t)ip_src_label_font,
                x+9,y,1,0,0,"%s",values?display.value[i]:display.label[i]);
        }
    }
    diagram_draw(canvas,&display);
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
