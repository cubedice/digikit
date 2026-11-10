#include "src-state.h"
static const uint16_t low[CP_CONTROLS]={0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};
static const uint16_t high[CP_CONTROLS]={11,3,127,127,500,127,8,4,6,100,75,2,1,0,127,127,4,700,100,1000,2,2,1,200,127,127,200,200,100,15,200,200};
static const uint16_t fm_high[12]={3,100,15,15,100,127,127,100,127,200,200,200};
static const uint8_t controls[4][8]={
    {CS_TUNE,CP_CHORD,CP_VOICING,CP_COLOR,CP_DRIFT,CP_STRUM,CP_ARP_MIX,CS_LEVEL},
    {CP_RATE,CP_ORDER,CP_OCTAVES,CP_GATE,CP_SWING,CP_RESTART,CP_LATCH,CP_ARP_MIX},
    {CS_TUNE,CS_WAVE,CP_WT_POS,CP_WT_DEPTH,CP_WT_RATE,CP_WT_SHAPE,CP_WT_RESET,CS_LEVEL},
    {CP_FM_ATTACK,CP_FM_DECAY,CP_FM_SUSTAIN,CP_FM_RELEASE,CP_FM_ENV,CP_FM_VELOCITY,CP_FM_WHEEL,CS_LEVEL}
};
static uint32_t family(const CsState *s) { return (s->metadata>>4)&3; }
uint32_t cs_pages(const CsState *s) { return family(s)>=2 ? 4 : 3; }
static void remember(CsState *s)
{
    if(family(s)==3) {
        s->fm[0]=s->value[CP_COLOR];
        for(uint32_t i=0;i<12;++i) s->fm[i+1]=s->value[16+i];
    } else {
        s->legacy_color=s->value[CP_COLOR];
        for(uint32_t i=0;i<12;++i) s->wavetable[i]=s->value[16+i];
    }
}
static void activate(CsState *s)
{
    s->value[CP_COLOR]=family(s)==3 ? s->fm[0] : s->legacy_color;
    for(uint32_t i=0;i<12;++i) s->value[16+i]=family(s)==3 ? s->fm[i+1] : s->wavetable[i];
}
static uint32_t minimum(const CsState *s,uint32_t id)
{ return id==CS_WAVE || (family(s)==3 && id>=16) ? 0 : low[id]; }
static void list_reset(CsState *s)
{
    for(uint32_t i=0;i<8;++i) {
        ((volatile int8_t *)s->list_delta)[i]=0;
        ((volatile uint8_t *)s->list_id)[i]=CS_NONE;
    }
}
static int is_list(const CsState *s,uint32_t control)
{
    if(control==CS_WAVE) return 1;
    if(control==CP_WTFM_RATIO) return 1;
    if(family(s)==3 && control>=16 && control<28) return control==CP_FM_ALG || control==CP_FM_RTA || control==CP_FM_RTB;
    return control==CP_CHORD || control==CP_VOICING || control==CP_RATE ||
        control==CP_ORDER || control==CP_OCTAVES || control==CP_RESTART ||
        control==CP_LATCH || control==CP_WT_SHAPE ||
        control==CP_WT_RESET || (control==CP_WT_RATE && s->value[CP_WT_CLOCK]);
}
static int32_t list_steps(CsState *s,uint32_t encoder,uint32_t control,int32_t movement)
{
    int32_t prior=s->list_delta[encoder];
    if(s->list_id[encoder]!=control || (movement>0 && prior<0) || (movement<0 && prior>0)) prior=0;
    /* Divide before adding the remainder so INT_MIN/MAX remain safe. Event
     * timestamps and coarse/fine flags do not accelerate a discrete list. */
    int32_t steps=movement/CS_LIST_TICKS;
    prior+=movement%CS_LIST_TICKS;
    steps+=prior/CS_LIST_TICKS;
    s->list_delta[encoder]=(int8_t)(prior%CS_LIST_TICKS);
    s->list_id[encoder]=(uint8_t)control;
    return steps;
}
void cs_init(CsState *s)
{
    for(uint32_t i=0;i<sizeof(*s);++i) ((volatile uint8_t *)s)[i]=0;
    cp_wavetable_defaults(s->value);
    s->token=1; s->revision=1; s->focus_id=CS_NONE;
    s->native_tune=0x4000;
    s->value[CP_COLOR]=64; s->value[CP_RATE]=4; s->value[CP_OCTAVES]=3;
    s->value[CP_GATE]=50; s->value[CP_ATTACK]=5; s->value[CP_RELEASE]=15;
    cp_fm_defaults(s->fm); remember(s);
    list_reset(s);
}
uint32_t cs_context(CsState *s,uint32_t track,uint32_t generation,int visible,
    uint32_t page,uint32_t mode,uint32_t clock,int learned,int modal)
{
    if(track>=16 || page>=cs_pages(s) || mode>KB_LEARN || clock>2) { visible=0; track=0; page=0; }
    if(s->track!=track || s->generation!=generation || s->visible!=!!visible ||
        s->page[track]!=page || s->mode!=mode || s->clock!=clock ||
        s->learned!=!!learned || s->modal!=!!modal) {
        /* Exhaustion fails closed instead of reusing a live event token. */
        if(s->token==UINT32_MAX) { s->visible=0; s->focus=0; return 0; }
        ++s->token; s->focus=0; s->focus_id=CS_NONE; list_reset(s);
    }
    if(s->generation!=generation) { s->pending=UINT32_MAX; s->native_tune_pending=1; }
    s->track=track; s->generation=generation; s->visible=(uint8_t)!!visible;
    s->page[track]=(uint8_t)page; s->mode=(uint8_t)mode; s->clock=(uint8_t)clock;
    s->learned=(uint8_t)!!learned; s->modal=(uint8_t)!!modal;
    return s->visible && generation ? s->token : 0;
}
void cs_sync(CsState *s,const uint16_t values[CP_CONTROLS])
{
    uint32_t changed=0;
    for(uint32_t i=0;i<CP_CONTROLS;++i) {
        uint32_t max=family(s)==3 && i>=16 && i<28 ? fm_high[i-16] : high[i];
        if(values[i]<minimum(s,i) || values[i]>max) return;
        if(values[i]!=s->value[i]) changed|=1u<<i;
    }
    if(family(s)!=3 && values[CP_WT_CLOCK] && values[CP_WT_RATE]>7) return;
    if(!changed || s->revision==UINT32_MAX) return;
    list_reset(s);
    for(uint32_t i=0;i<CP_CONTROLS;++i) s->value[i]=values[i];
    remember(s);
    ++s->revision; s->pending|=changed;
}
uint32_t cs_control(uint32_t page,uint32_t encoder)
{ return page<4 && encoder<8 ? controls[page][encoder] : CS_NONE; }
uint32_t cs_mapped_control(const CsState *s,uint32_t encoder)
{
    if(family(s)==2 && s->page[s->track]==3 && encoder<8) {
        static const uint8_t wtfm[8]={CS_TUNE,CP_WTFM_DEPTH,CP_WTFM_RATIO,CP_WTFM_ENV,CP_WT_ATTACK,CP_WT_RELEASE,CP_WTFM_WHEEL,CS_LEVEL};
        return wtfm[encoder];
    }
    uint32_t id=cs_control(s->page[s->track],encoder);
    if(family(s)==3 && s->page[s->track]==2 && encoder>=2 && encoder<=6) return CP_FM_ALG+encoder-2;
    return id==CP_COLOR && ((s->metadata>>4)&3)==2 ? CP_WT_POS : id;
}
uint32_t cs_choice_value(const CsState *s,uint32_t control)
{
    if(control==CP_CHORD) return s->mode==KB_PLAY ? 0 : 1u+s->value[CP_CHORD];
    if(control==CS_WAVE) {
        uint32_t f=family(s);
        return f<2 ? f : f==3 ? 7 : 2u+s->value[CP_WT_TABLE];
    }
    return control<CP_CONTROLS ? s->value[control] : 0;
}
uint32_t cs_choice_max(const CsState *s,uint32_t control)
{
    if(control==CP_CHORD) return 12;
    if(control==CS_WAVE) return 7;
    if(family(s)==3 && control>=16 && control<28) return fm_high[control-16];
    if(control==CP_WT_RATE && s->value[CP_WT_CLOCK]) return 7;
    return control<CP_CONTROLS ? high[control] : 0;
}
void cs_metadata(CsState *s,uint32_t metadata)
{
    if(s->metadata==metadata || s->revision==UINT32_MAX) return;
    if((s->metadata^metadata)&0x30) {
        s->focus=0; list_reset(s);
        if(s->token==UINT32_MAX) { s->visible=0; return; }
        ++s->token;
    }
    int changed_family=(s->metadata^metadata)&0x30;
    s->metadata=metadata;
    if(changed_family) activate(s);
    ++s->revision; s->pending=UINT32_MAX;
}
void cs_import(CsState *s,const CpPreset *p)
{
    /* Choose the incoming family before range-checking its active bank. Never
     * reinterpret queued WT values as FM ratios/envelopes during a preset load. */
    s->legacy_color=p->control[CP_COLOR];
    for(uint32_t i=0;i<12;++i) s->wavetable[i]=p->control[16+i];
    for(uint32_t i=0;i<13;++i) s->fm[i]=p->fm[i];
    cs_metadata(s,p->mode|((uint32_t)p->clock<<2)|((uint32_t)p->family<<4)|((uint32_t)p->bpm<<8));
    uint16_t active[CP_CONTROLS];
    for(uint32_t i=0;i<CP_CONTROLS;++i) active[i]=p->control[i];
    if(p->family==3) {
        active[CP_COLOR]=p->fm[0];
        for(uint32_t i=0;i<12;++i) active[16+i]=p->fm[i+1];
    }
    cs_sync(s,active);
}
void cs_native_tune(CsState *s,uint32_t raw)
{
    if(raw<0x400 || raw>0x7c00 || raw==s->native_tune || s->revision==UINT32_MAX) return;
    s->native_tune=raw; ++s->revision; s->native_tune_pending=1;
}
int cs_edit(CsState *s,uint32_t token,uint32_t encoder,int32_t detents,uint32_t now,uint32_t *id)
{
    if(!token || token!=s->token || !s->generation || !s->visible || encoder>=8 || !detents) return CS_STALE;
    uint32_t control=cs_mapped_control(s,encoder);
    if(id) *id=control;
    if(s->modal || s->mode==KB_LEARN) return CS_READONLY;
    s->focus=1; s->focus_id=(uint8_t)control; s->focus_at=now;
    if(control==CS_TUNE || control==CS_LEVEL) return CS_NATIVE;
    if((control>=16 && control<CP_CONTROLS && family(s)<2) || control==CP_SOURCE || (s->mode==KB_PLAY &&
        (control==CP_VOICING || control==CP_STRUM)) ||
        /* A learned shape fixes its voicing, not the choice of chord. */
        (s->learned && control==CP_VOICING) ||
        (s->clock==1 && control==CP_SWING)) return CS_READONLY;
    /* Clamp before addition; even a malformed INT_MIN event cannot overflow. */
    uint32_t maximum=cs_choice_max(s,control),current=cs_choice_value(s,control);
    int discrete=is_list(s,control);
    if(discrete) {
        /* Turning further into an endpoint must not bank motion that makes the
         * next reversal feel delayed or causes a later external edit to jump. */
        if((detents>0 && current>=maximum) || (detents<0 && current<=minimum(s,control))) {
            s->list_delta[encoder]=0; return CS_READONLY;
        }
        detents=list_steps(s,encoder,control,detents);
    } else { s->list_delta[encoder]=0; s->list_id[encoder]=CS_NONE; }
    int32_t room=(int32_t)maximum-(int32_t)current;
    if(detents>room) detents=room;
    room=(int32_t)minimum(s,control)-(int32_t)current;
    if(detents<room) detents=room;
    if(!detents || s->revision==UINT32_MAX) return CS_READONLY;
    uint32_t next=(uint32_t)((int32_t)current+detents),metadata=s->metadata;
    uint8_t mode=s->mode,learned=s->learned;
    if(control==CP_CHORD) {
        mode=next ? KB_ROOT : KB_PLAY;
        learned=(uint8_t)(next==11);
        metadata=(metadata&~3u)|mode;
    } else if(control==CS_WAVE) metadata=(metadata&~0x30u)|((next<2 ? next : next==7 ? 3u : 2u)<<4);
    int context_changed=mode!=s->mode || learned!=s->learned || ((metadata^s->metadata)&0x30);
    if(context_changed && s->token==UINT32_MAX) { s->visible=0; return CS_READONLY; }
    if(control==CP_CHORD) {
        if(next) s->value[control]=(uint16_t)(next-1);
    } else if(control!=CS_WAVE) s->value[control]=(uint16_t)next;
    s->mode=mode; s->learned=learned; s->metadata=metadata;
    if(control==CS_WAVE) {
        activate(s);
        if(next>=2 && next<7) s->value[CP_WT_TABLE]=(uint16_t)(next-2);
    }
    remember(s);
    if(context_changed) { ++s->token; list_reset(s); s->pending=UINT32_MAX; }
    if(discrete && (next==maximum || next==minimum(s,control))) s->list_delta[encoder]=0;
    ++s->revision; s->pending|=control==CS_WAVE ? UINT32_MAX : 1u<<control;
    return CS_EDIT;
}
int cs_focused(const CsState *s,uint32_t now)
{ return s->visible && !s->modal && s->focus && now-s->focus_at<CS_FOCUS_MS; }
void cs_applied(CsState *s,uint32_t generation,uint32_t revision,const uint32_t values[32],uint32_t tune)
{
    if(generation!=s->generation || revision!=s->revision) return;
    for(uint32_t i=0;i<CP_CONTROLS;++i)
        if(values[i]==s->value[i]) s->pending&=~(1u<<i);
    if(tune==s->native_tune) s->native_tune_pending=0;
}
