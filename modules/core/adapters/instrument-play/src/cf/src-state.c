#include "src-state.h"
static const uint16_t low[CP_CONTROLS]={0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};
static const uint16_t high[CP_CONTROLS]={11,3,127,127,500,127,8,4,6,100,75,2,1,0,127,127,4,700,100,1000,2,2,1,200,127,127,200,200};
static const uint8_t controls[3][8]={
    {CS_TUNE,CP_CHORD,CP_VOICING,CP_COLOR,CP_DRIFT,CP_STRUM,CP_ARP_MIX,CS_LEVEL},
    {CP_RATE,CP_ORDER,CP_OCTAVES,CP_GATE,CP_SWING,CP_RESTART,CP_LATCH,CP_ARP_MIX},
    {CS_TUNE,CP_WT_TABLE,CP_WT_POS,CP_WT_DEPTH,CP_WT_RATE,CP_WT_SHAPE,CP_WT_RESET,CS_LEVEL}
};
static void list_reset(CsState *s)
{
    for(uint32_t i=0;i<8;++i) {
        ((volatile int8_t *)s->list_delta)[i]=0;
        ((volatile uint8_t *)s->list_id)[i]=CS_NONE;
    }
}
static int is_list(const CsState *s,uint32_t control)
{
    return control==CP_CHORD || control==CP_VOICING || control==CP_RATE ||
        control==CP_ORDER || control==CP_OCTAVES || control==CP_RESTART ||
        control==CP_LATCH || control==CP_WT_TABLE || control==CP_WT_SHAPE ||
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
    static const uint16_t wt[12]={0,0,0,10,0,0,0,100,5,15,100,100};
    for(uint32_t i=0;i<12;++i) s->value[CP_WT_TABLE+i]=wt[i];
    s->token=1; s->revision=1; s->focus_id=CS_NONE;
    s->native_tune=0x4000;
    s->value[CP_COLOR]=64; s->value[CP_RATE]=4; s->value[CP_OCTAVES]=3;
    s->value[CP_GATE]=50; s->value[CP_ATTACK]=5; s->value[CP_RELEASE]=15;
    list_reset(s);
}
uint32_t cs_context(CsState *s,uint32_t track,uint32_t generation,int visible,
    uint32_t page,uint32_t mode,uint32_t clock,int learned,int modal)
{
    if(track>=16 || page>2 || mode>KB_LEARN || clock>2) { visible=0; track=0; page=0; }
    if(s->track!=track || s->generation!=generation || s->visible!=!!visible ||
        s->page[track]!=page || s->mode!=mode || s->clock!=clock ||
        s->learned!=!!learned || s->modal!=!!modal) {
        /* Exhaustion fails closed instead of reusing a live event token. */
        if(s->token==UINT32_MAX) { s->visible=0; s->focus=0; return 0; }
        ++s->token; s->focus=0; s->focus_id=CS_NONE; list_reset(s);
    }
    if(s->generation!=generation) s->pending=((1u<<CP_CONTROLS)-1);
    s->track=track; s->generation=generation; s->visible=(uint8_t)!!visible;
    s->page[track]=(uint8_t)page; s->mode=(uint8_t)mode; s->clock=(uint8_t)clock;
    s->learned=(uint8_t)!!learned; s->modal=(uint8_t)!!modal;
    return s->visible && generation ? s->token : 0;
}
void cs_sync(CsState *s,const uint16_t values[CP_CONTROLS])
{
    uint32_t changed=0;
    for(uint32_t i=0;i<CP_CONTROLS;++i) {
        if(values[i]<low[i] || values[i]>high[i]) return;
        if(values[i]!=s->value[i]) changed|=1u<<i;
    }
    if(!changed || s->revision==UINT32_MAX) return;
    list_reset(s);
    for(uint32_t i=0;i<CP_CONTROLS;++i) s->value[i]=values[i];
    ++s->revision; s->pending|=changed;
}
uint32_t cs_control(uint32_t page,uint32_t encoder)
{ return page<3 && encoder<8 ? controls[page][encoder] : CS_NONE; }
uint32_t cs_mapped_control(const CsState *s,uint32_t encoder)
{
    uint32_t id=cs_control(s->page[s->track],encoder);
    return id==CP_COLOR && ((s->metadata>>4)&3)==2 ? CP_WT_POS : id;
}
uint32_t cs_choice_value(const CsState *s,uint32_t control)
{
    if(control==CP_CHORD) return s->mode==KB_PLAY ? 0 : 1u+s->value[CP_CHORD];
    if(control==CP_WT_TABLE) {
        uint32_t family=(s->metadata>>4)&3;
        return family<2 ? family : 2u+s->value[CP_WT_TABLE];
    }
    return control<CP_CONTROLS ? s->value[control] : 0;
}
uint32_t cs_choice_max(const CsState *s,uint32_t control)
{
    if(control==CP_CHORD) return 12;
    if(control==CP_WT_TABLE) return 6;
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
    s->metadata=metadata; ++s->revision; s->pending|=((1u<<CP_CONTROLS)-1);
}
void cs_native_tune(CsState *s,uint32_t raw)
{
    if(raw<0x400 || raw>0x7c00 || raw==s->native_tune || s->revision==UINT32_MAX) return;
    s->native_tune=raw; ++s->revision; s->pending|=1u<<CS_TUNE;
}
int cs_edit(CsState *s,uint32_t token,uint32_t encoder,int32_t detents,uint32_t now,uint32_t *id)
{
    if(!token || token!=s->token || !s->generation || !s->visible || encoder>=8 || !detents) return CS_STALE;
    uint32_t control=cs_mapped_control(s,encoder);
    if(id) *id=control;
    if(s->modal || s->mode==KB_LEARN) return CS_READONLY;
    s->focus=1; s->focus_id=(uint8_t)control; s->focus_at=now;
    if(control==CS_TUNE || control==CS_LEVEL) return CS_NATIVE;
    if((control>CP_WT_TABLE && ((s->metadata>>4)&3)!=2) || control==CP_SOURCE || (s->mode==KB_PLAY &&
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
        if((detents>0 && current>=maximum) || (detents<0 && current<=low[control])) {
            s->list_delta[encoder]=0; return CS_READONLY;
        }
        detents=list_steps(s,encoder,control,detents);
    } else { s->list_delta[encoder]=0; s->list_id[encoder]=CS_NONE; }
    int32_t room=(int32_t)maximum-(int32_t)current;
    if(detents>room) detents=room;
    room=(int32_t)low[control]-(int32_t)current;
    if(detents<room) detents=room;
    if(!detents || s->revision==UINT32_MAX) return CS_READONLY;
    uint32_t next=(uint32_t)((int32_t)current+detents),metadata=s->metadata;
    uint8_t mode=s->mode,learned=s->learned;
    if(control==CP_CHORD) {
        mode=next ? KB_ROOT : KB_PLAY;
        learned=(uint8_t)(next==11);
        metadata=(metadata&~3u)|mode;
    } else if(control==CP_WT_TABLE) metadata=(metadata&~0x30u)|((next<2 ? next : 2u)<<4);
    int context_changed=mode!=s->mode || learned!=s->learned || ((metadata^s->metadata)&0x30);
    if(context_changed && s->token==UINT32_MAX) { s->visible=0; return CS_READONLY; }
    if(control==CP_CHORD) {
        if(next) s->value[control]=(uint16_t)(next-1);
    } else if(control==CP_WT_TABLE) {
        if(next>=2) s->value[control]=(uint16_t)(next-2);
    } else s->value[control]=(uint16_t)next;
    s->mode=mode; s->learned=learned; s->metadata=metadata;
    if(context_changed) { ++s->token; list_reset(s); s->pending|=(1u<<CP_CONTROLS)-1; }
    if(discrete && (next==maximum || next==low[control])) s->list_delta[encoder]=0;
    ++s->revision; s->pending|=1u<<control;
    return CS_EDIT;
}
int cs_focused(const CsState *s,uint32_t now)
{ return s->visible && !s->modal && s->focus && now-s->focus_at<CS_FOCUS_MS; }
void cs_applied(CsState *s,uint32_t generation,uint32_t revision,const uint32_t values[32])
{
    if(generation!=s->generation || revision!=s->revision) return;
    for(uint32_t i=0;i<CP_CONTROLS;++i)
        if(values[i]==s->value[i]) s->pending&=~(1u<<i);
    if(values[CS_TUNE]==s->native_tune) s->pending&=~(1u<<CS_TUNE);
}
