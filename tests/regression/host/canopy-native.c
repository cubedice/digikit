#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/src-native.c"
static CsState state;
static uint32_t selected,alternate,now=100,type_id=9,generation=1,stock_turns,stock_keys,invalidations,polls;
static uint32_t manager[16],node[4],vtable[60],modal[24],modal_node[4];
uint8_t *dk_fw_view_brain_instance(void) { return (uint8_t *)manager; }
uint8_t *dk_fw_view_brain_view_manager(uint8_t *brain) { return brain; }
uint32_t dkrt_instrument_type(void) { return type_id; }
uint32_t dkrt_instrument_generation(void) { return generation; }
uint32_t ip_src_type(uint8_t *v) { (void)v; return selected?0:type_id; }
uint32_t ip_src_track(uint32_t p) { (void)p; return selected; }
uint8_t ip_src_alternate(uint32_t p) { (void)p; return (uint8_t)alternate; }
uint32_t instrument_src_now(void) { return now; }
void instrument_src_poll(uint32_t time) { assert(time==now); ++polls; }
uint32_t instrument_src_context(uint32_t track,uint32_t page,int visible)
{ return cs_context(&state,track,generation,visible,page,KB_PLAY,0,0,0); }
int instrument_src_encoder(uint32_t token,uint32_t encoder,int32_t turns,uint32_t time)
{ assert(time==now); uint32_t id; return cs_edit(&state,token,encoder,turns,time,&id); }
void instrument_src_display(CvDisplay *d,uint32_t time,const char *t,const char *l)
{ assert(time==now); assert(!strcmp(t,"-60.00") && !strcmp(l,"127")); memset(d,0,sizeof(*d)); strcpy(d->heading,"CANOPY"); strcpy(d->label[0],"TUNE"); strcpy(d->value[0],t); strcpy(d->label[7],"LEVEL"); strcpy(d->value[7],l); }
int32_t ip_src_value(uint8_t *v,uint32_t code,uint8_t *flags)
{ (void)v;*flags=0;return code==0xca?0x400:0x7f00; }
void ip_src_tune_format(int32_t raw,char *out) { assert(raw==0x400); strcpy(out,"-60.00"); }
void ip_src_level_format(int32_t raw,char *out) { assert(raw==0x7f00); strcpy(out,"127"); }
void ip_src_stock_attach(uint8_t *v,uint8_t *m) { (void)m; *(uint32_t *)(v+0x90)=0; }
void ip_src_stock_detach(uint8_t *v,uint8_t *m) { (void)v;(void)m; }
void ip_src_stock_draw(uint8_t *v,uint8_t *c) { (void)v;(void)c; }
void ip_src_stock_update(uint8_t *v) { (void)v; }
uint32_t ip_src_stock_key(uint8_t *v,uint8_t *e) { (void)v;(void)e; ++stock_keys; return 7; }
uint32_t ip_src_stock_turn(uint8_t *v,uint8_t *e) { (void)v;(void)e; ++stock_turns; return 7; }
uint32_t ip_src_stock_parameter(uint8_t *v,uint32_t e) { (void)v; return 1000+e; }
uint32_t ip_src_stock_event(uint8_t *controller,uint8_t *event)
{ (void)controller; (void)event; return 7; }
static void event(uint8_t *e,uint32_t encoder,int32_t delta,uint32_t stamp,uint32_t fine)
{
    uint32_t *w=(uint32_t *)e; w[2]=stamp;w[3]=encoder;w[4]=(uint32_t)delta;e[20]=(uint8_t)fine;
    assert(ip_src_event(0,e)==7);
}
void dk_fw_view_invalidate(uint8_t *v) { (void)v; ++invalidations; }
void dk_fw_draw_fill_rect(uint8_t *p,int32_t x0,int32_t y0,int32_t x1,int32_t y1,int32_t color)
{
    assert(x0>=0 && x1<124 && y0>=0 && y1<54);
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        uint8_t *px=&p[(63-y)*128+x];
        if(color<0) *px^=1; else *px=(uint8_t)(color&1);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2); static uint32_t v[160],e[6]; uint8_t canvas[8192];
    cs_init(&state); memset(canvas,0xaa,sizeof(canvas));
    ip_src_draw((uint8_t *)v,canvas);
    if(!strcmp(argv[1],"paint")) {
        for(int y=0;y<64;++y) for(int x=0;x<128;++x)
            if(y<10 || x>=124) assert(canvas[y*128+x]==0xaa);
            else assert(canvas[y*128+x]==!!(pixels[y*16+x/8]&(128u>>(x&7))));
        assert(mf_src_page_count(type_id)==2);
        now=139; ip_src_update((uint8_t *)v); assert(!invalidations);
        now=140; ip_src_update((uint8_t *)v); assert(invalidations==1);
    } else if(!strcmp(argv[1],"routing")) {
        assert(ip_src_parameter((uint8_t *)v,0)==0xca);
        assert(ip_src_parameter((uint8_t *)v,7)==0xd1);
        assert(ip_src_parameter((uint8_t *)v,2)==1002);
        event((uint8_t *)e,4,3,0,0); assert(ip_src_turn((uint8_t *)v,(uint8_t *)e)==1);
        assert(state.value[CP_COLOR]==67 && !stock_turns);
        event((uint8_t *)e,1,3,0,0); assert(ip_src_turn((uint8_t *)v,(uint8_t *)e)==7);
        assert(stock_turns==1);
        event((uint8_t *)e,2,3,0,0); ip_src_turn((uint8_t *)v,(uint8_t *)e);
        assert(state.value[CP_CHORD]==0 && stock_turns==1);
        selected=1; context((uint8_t *)v); event((uint8_t *)e,4,3,0,0);
        assert(ip_src_parameter((uint8_t *)v,0)==1000);
        assert(ip_src_turn((uint8_t *)v,(uint8_t *)e)==7 && stock_turns==2);
    } else if(!strcmp(argv[1],"delayed")) {
        event((uint8_t *)e,4,3,0,0); v[0x90/4]=1;
        ip_src_turn((uint8_t *)v,(uint8_t *)e);
        assert(state.value[CP_COLOR]==64 && state.value[CP_GATE]==50 && !stock_turns);
        event((uint8_t *)e,4,3,0,0); selected=1;
        ip_src_turn((uint8_t *)v,(uint8_t *)e); assert(!stock_turns);
    } else if(!strcmp(argv[1],"overflow")) {
        uint32_t events[65][6];
        for(unsigned i=0;i<65;++i) event((uint8_t *)events[i],4,1,0,0);
        ip_src_turn((uint8_t *)v,(uint8_t *)events[0]); assert(state.value[CP_COLOR]==64);
        ip_src_turn((uint8_t *)v,(uint8_t *)events[64]); assert(state.value[CP_COLOR]==65);
    } else if(!strcmp(argv[1],"tick")) {
        v[0]=(uint32_t)(uintptr_t)vtable; vtable[4]=(uint32_t)(uintptr_t)ip_src_draw;
        node[0]=(uint32_t)(uintptr_t)((uint8_t *)manager+0x14); node[2]=(uint32_t)(uintptr_t)v;
        manager[7]=(uint32_t)(uintptr_t)node;
        now=139; instrument_src_tick(); assert(!invalidations && polls==1);
        now=140; instrument_src_tick(); assert(invalidations==1 && published && polls==2);
        modal_node[0]=node[0]; modal_node[2]=(uint32_t)(uintptr_t)modal;
        manager[7]=(uint32_t)(uintptr_t)modal_node;
        instrument_src_tick(); assert(!published && invalidations==1 && polls==2);
        manager[7]=(uint32_t)(uintptr_t)node; instrument_src_tick(); assert(published && invalidations==2);
        selected=1; instrument_src_tick(); assert(!published && invalidations==2);
    } else if(!strcmp(argv[1],"attach")) {
        v[0x90/4]=1; ip_src_detach((uint8_t *)v,0);
        assert(!published); ip_src_attach((uint8_t *)v,0); assert(v[0x90/4]==1);
        v[0x90/4]=0; ip_src_draw((uint8_t *)v,canvas); assert(v[0x90/4]==1);
        generation=2; ip_src_attach((uint8_t *)v,0); assert(v[0x90/4]==0);
    } else if(!strcmp(argv[1],"track")) {
        v[0x90/4]=1; context((uint8_t *)v); selected=1; context((uint8_t *)v);
        v[0x90/4]=0; selected=0; context((uint8_t *)v); assert(v[0x90/4]==1);
    } else if(!strcmp(argv[1],"alternate")) {
        alternate=1; context((uint8_t *)v); event((uint8_t *)e,4,3,0,0);
        assert(ip_src_turn((uint8_t *)v,(uint8_t *)e)==7);
        generation=2; context((uint8_t *)v); assert(mf_src_page_count(type_id)==1);
    } else return 2;
    puts("ok"); return 0;
}
