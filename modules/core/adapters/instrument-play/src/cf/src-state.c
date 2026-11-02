#include "src-state.h"
static const uint16_t low[CP_CONTROLS]={0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,0};
static const uint16_t high[CP_CONTROLS]={11,3,127,127,500,127,8,4,3,100,75,2,1,0,127,127};
static const uint8_t controls[2][8]={
    {CS_TUNE,CP_CHORD,CP_VOICING,CP_COLOR,CP_DRIFT,CP_STRUM,CP_ARP_MIX,CS_LEVEL},
    {CP_RATE,CP_ORDER,CP_OCTAVES,CP_GATE,CP_SWING,CP_RESTART,CP_LATCH,CP_SOURCE}
};
void cs_init(CsState *s)
{
    for(uint32_t i=0;i<sizeof(*s);++i) ((volatile uint8_t *)s)[i]=0;
    s->token=1; s->revision=1; s->focus_id=CS_NONE;
    s->value[CP_COLOR]=64; s->value[CP_RATE]=4; s->value[CP_OCTAVES]=1;
    s->value[CP_GATE]=50; s->value[CP_ATTACK]=5; s->value[CP_RELEASE]=15;
}
uint32_t cs_context(CsState *s,uint32_t track,uint32_t generation,int visible,
    uint32_t page,uint32_t mode,uint32_t clock,int learned,int modal)
{
    if(track>=16 || page>1 || mode>KB_LEARN || clock>2) { visible=0; track=0; page=0; }
    if(s->track!=track || s->generation!=generation || s->visible!=!!visible ||
        s->page[track]!=page || s->mode!=mode || s->clock!=clock ||
        s->learned!=!!learned || s->modal!=!!modal) {
        /* Exhaustion fails closed instead of reusing a live event token. */
        if(s->token==UINT32_MAX) { s->visible=0; s->focus=0; return 0; }
        ++s->token; s->focus=0; s->focus_id=CS_NONE;
    }
    if(s->generation!=generation) s->pending=0xffff;
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
    for(uint32_t i=0;i<CP_CONTROLS;++i) s->value[i]=values[i];
    ++s->revision; s->pending|=changed;
}
uint32_t cs_control(uint32_t page,uint32_t encoder)
{ return page<2 && encoder<8 ? controls[page][encoder] : CS_NONE; }
int cs_edit(CsState *s,uint32_t token,uint32_t encoder,int32_t detents,uint32_t now,uint32_t *id)
{
    if(!token || token!=s->token || !s->generation || !s->visible || encoder>=8 || !detents) return CS_STALE;
    uint32_t control=cs_control(s->page[s->track],encoder);
    if(id) *id=control;
    if(s->modal || s->mode==KB_LEARN) return CS_READONLY;
    s->focus=1; s->focus_id=(uint8_t)control; s->focus_at=now;
    if(control==CS_TUNE || control==CS_LEVEL) return CS_NATIVE;
    if(control==CP_SOURCE || (s->mode==KB_PLAY &&
        (control==CP_CHORD || control==CP_VOICING || control==CP_STRUM)) ||
        (s->learned && (control==CP_CHORD || control==CP_VOICING)) ||
        (s->clock==1 && control==CP_SWING)) return CS_READONLY;
    /* Clamp before addition; even a malformed INT_MIN event cannot overflow. */
    int32_t room=(int32_t)high[control]-s->value[control];
    if(detents>room) detents=room;
    room=(int32_t)low[control]-s->value[control];
    if(detents<room) detents=room;
    if(!detents || s->revision==UINT32_MAX) return CS_READONLY;
    s->value[control]=(uint16_t)((int32_t)s->value[control]+detents);
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
}
