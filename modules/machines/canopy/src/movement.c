#include "movement.h"
#include <stddef.h>

/* Byte copies avoid freestanding compilers emitting an implicit libc call. */
static void copy(void *to, const void *from, uint32_t n)
{
    volatile uint8_t *d=to;
    const volatile uint8_t *p=from;
    while(n--) *d++=*p++;
}
static void clear(void *to, uint32_t n)
{
    volatile uint8_t *d=to;
    while(n--) *d++=0;
}
static void event(CmEvent *out, uint32_t *n, uint32_t kind, uint32_t lane, const CmNote *v)
{
    out[*n].kind=kind; out[*n].lane=lane;
    copy(&out[*n].note,v,sizeof(*v)); ++*n;
}
static void release(CmState *s,CmEvent *out,uint32_t *n)
{
    if(s->arp.token) event(out,n,CM_OFF,CM_ARP,&s->arp);
    s->arp.token=0; s->gate_left=0;
}
void cm_init(CmState *s,uint32_t seed)
{
    clear(s,sizeof(*s));
    s->random=seed ? seed : 1;
    s->next_token=1;
    s->config.rate=1; s->config.octaves=1; s->config.gate=50;
    s->config.bpm=120;
}
int cm_configure(CmState *s,const CmConfig *c)
{
    if(c->clock>CM_LOCAL || c->order>CM_RANDOM || c->restart>CM_CONTINUE ||
       !c->octaves || c->octaves>3 || !c->gate || c->gate>100 || c->swing>75 ||
       c->bpm<30 || c->bpm>300 || c->strum>750 || c->mix>127 || !c->rate ||
       (c->clock==CM_MASTER ? c->rate>16 || c->swing!=0 :
        c->rate!=1 && c->rate!=2 && c->rate!=3 && c->rate!=4 && c->rate!=6 && c->rate!=8))
        return -1;
    if(c->clock!=s->config.clock || c->rate!=s->config.rate ||
       (!s->config.mix && c->mix)) s->reset_pending=1;
    copy(&s->config,c,sizeof(*c));
    return 0;
}
uint32_t cm_panic(CmState *s,CmEvent out[CM_MAX_EVENTS])
{
    uint32_t n=0;
    release(s,out,&n);
    for(uint32_t i=0;i<4;++i) {
        if(s->pads[i].token) event(out,&n,CM_OFF,CM_PAD,&s->pads[i]);
        s->pads[i].token=s->pending[i].token=s->chord[i].token=0;
    }
    s->count=0; s->initialized=0; s->started=0; s->phase=0;
    s->status=CM_STOPPED;
    return n;
}
static int equal(const CmNote *a,const CmNote *b)
{
    return a->token==b->token && a->note==b->note && a->velocity==b->velocity &&
        a->port==b->port && a->channel==b->channel;
}
static int contains(const CmInput *in,const CmNote *v)
{
    for(uint32_t i=0;i<in->count;++i) if(equal(&in->notes[i],v)) return 1;
    return 0;
}
static void chord(CmState *s,const CmInput *in,CmEvent *out,uint32_t *n)
{
    /* Every revision cancels the old strum. Already sounding common tones
     * retain their identity; pending tones are scheduled from the new commit. */
    for(uint32_t i=0;i<4;++i) {
        if(s->pads[i].token && !contains(in,&s->pads[i])) {
            event(out,n,CM_OFF,CM_PAD,&s->pads[i]); s->pads[i].token=0;
        }
        s->pending[i].token=0;
    }
    for(uint32_t i=0;i<in->count;++i) {
        uint32_t found=0,slot=0;
        for(uint32_t j=0;j<4;++j) {
            if(equal(&in->notes[i],&s->pads[j]) || equal(&in->notes[i],&s->pending[j])) found=1;
            if(!s->pads[j].token && !s->pending[j].token) slot=j;
        }
        if(found) continue;
        uint32_t delay=in->generated && in->count>1 ? s->config.strum*i/(in->count-1) : 0;
        if(delay) {
            copy(&s->pending[slot],&in->notes[i],sizeof(CmNote));
            s->pending_at[slot]=in->block+delay;
        } else {
            copy(&s->pads[slot],&in->notes[i],sizeof(CmNote));
            event(out,n,CM_ON,CM_PAD,&s->pads[slot]);
        }
    }
    s->count=in->count;
    for(uint32_t i=0;i<4;++i) copy(&s->chord[i],&in->notes[i],sizeof(CmNote));
    s->revision=in->revision;
}
static void onset(CmState *s,CmEvent *out,uint32_t *n)
{
    CmNote notes[12]; uint32_t count=0;
    for(uint32_t oct=0;oct<s->config.octaves;++oct)
        for(uint32_t i=0;i<s->count;++i) {
            uint32_t pitch=s->chord[i].note+12*oct;
            if(pitch>127) continue; /* omit unplayable octave extensions */
            copy(&notes[count],&s->chord[i],sizeof(CmNote));
            notes[count++].note=(uint8_t)pitch;
        }
    if(!count || !s->config.mix) return;
    if(s->config.order!=CM_AS_PLAYED) {
        /* Stable bounded insertion sort (at most twelve candidates). */
        for(uint32_t i=1;i<count;++i) {
            CmNote v; copy(&v,&notes[i],sizeof(v)); uint32_t j=i;
            while(j && notes[j-1].note>v.note) {
                copy(&notes[j],&notes[j-1],sizeof(v)); --j;
            }
            copy(&notes[j],&v,sizeof(v));
        }
    }
    uint32_t index=s->ordinal%count;
    if(s->config.order==CM_DOWN) index=count-1-index;
    if(s->config.order==CM_UP_DOWN && count>1) {
        index=s->ordinal%(2*count-2);
        if(index>=count) index=2*count-2-index;
    }
    if(s->config.order==CM_RANDOM) {
        uint32_t r=s->random;
        r^=r<<13; r^=r>>17; r^=r<<5; s->random=r;
        index=r%count;
    }
    if(!s->next_token) { s->status=CM_EXHAUSTED; return; }
    copy(&s->arp,&notes[index],sizeof(CmNote));
    s->arp.token=s->next_token++;
    ++s->ordinal;
    event(out,n,CM_ON,CM_ARP,&s->arp);
}
int cm_tick(CmState *s,const CmInput *in,CmEvent out[CM_MAX_EVENTS])
{
    if(!in->revision || in->count>4 || in->generated>1 || in->valid>1 || in->running>1) return -1;
    if(s->initialized && in->block-s->block!=1) return -1;
    for(uint32_t i=0;i<in->count;++i) {
        const CmNote *v=&in->notes[i];
        if(!v->token || v->note>127 || !v->velocity || v->velocity>127 || v->port>1 || v->channel>15)
            return -1;
        for(uint32_t j=0;j<i;++j) if(in->notes[j].token==v->token) return -1;
    }
    if(s->initialized && in->revision==s->revision) {
        if(in->count!=s->count) return -1;
        for(uint32_t i=0;i<in->count;++i) if(!equal(&in->notes[i],&s->chord[i])) return -1;
    }
    uint32_t n=0,first=!s->initialized;
    uint32_t reset=first || s->reset_pending || in->generation!=s->generation ||
        (s->config.restart==CM_TRIG && in->phrase!=s->phrase) ||
        (s->config.restart==CM_PATTERN && in->pattern!=s->pattern);
    uint32_t boundary=first || in->boundary!=s->boundary;
    uint32_t stopped=s->initialized && s->running && !in->running;
    if(reset) {
        release(s,out,&n);
        s->ordinal=0; s->phase=0; s->parity=1; s->step_count=0; s->started=0;
        s->period=0; s->boundary_block=in->block; s->clock_healthy=0;
        s->reset_pending=0;
    }
    if(s->gate_left && !--s->gate_left) release(s,out,&n);
    if(!s->config.mix || !in->count) release(s,out,&n);
    if(first || in->revision!=s->revision) chord(s,in,out,&n);
    /* STOP cancels pending generated pad onsets, not held keyboard pads. */
    if(stopped) for(uint32_t i=0;i<4;++i) s->pending[i].token=0;
    for(uint32_t i=0;i<4;++i) if(s->pending[i].token &&
        (int32_t)(in->block-s->pending_at[i])>=0) {
        copy(&s->pads[i],&s->pending[i],sizeof(CmNote));
        s->pending[i].token=0;
        event(out,&n,CM_ON,CM_PAD,&s->pads[i]);
    }
    uint32_t due=0,period=1;
    if(s->config.clock==CM_LOCAL) {
        /* Rational phase: at 1500 blocks/s one quarter is 90000/BPM blocks.
         * Alternating thresholds preserve the exact pair duration including
         * triplets; carry the remainder instead of rounding each interval. */
        uint32_t increment=s->config.bpm*s->config.rate*100;
        uint32_t threshold=90000*(100+(s->parity ? -(int32_t)s->swing : (int32_t)s->swing));
        s->phase+=increment;
        if(!s->started || s->phase>=threshold) {
            if(s->started) s->phase-=threshold;
            else s->phase=0;
            s->parity^=1;
            /* Apply swing at pair boundaries so a live edit cannot shrink
             * the interval underneath its accumulated phase. */
            if(!s->parity) s->swing=s->config.swing;
            threshold=90000*(100+(s->parity ? -(int32_t)s->swing : (int32_t)s->swing));
            period=(threshold+increment-1)/increment;
            due=1; s->started=1;
        }
        s->status=CM_RUNNING;
    } else {
        uint32_t gap=in->block-s->boundary_block;
        uint32_t stale=!boundary && gap>12000; /* hard bound: eight seconds */
        uint32_t missed=boundary && !first && !reset && in->boundary-s->boundary!=1;
        if(!in->running || !in->valid || stale || missed) {
            release(s,out,&n); s->started=0; s->clock_healthy=0; s->period=0;
            s->status=!in->running ? CM_STOPPED : stale || missed ? CM_CLOCK_LOST : CM_WAIT_CLOCK;
        } else {
            if(boundary) {
                if(s->clock_healthy && !reset && gap && gap<=12000) s->period=gap;
                s->clock_healthy=1;
                s->boundary_block=in->block;
                if(!s->started || ++s->step_count>=s->config.rate) {
                    s->step_count=0; s->started=1; due=1;
                    /* No tempo inference from raw firmware deadlines. First
                     * gate is conservatively 1 block; later gates use only
                     * observed audio-block distance, capped at eight seconds. */
                    period=s->period ? s->period*s->config.rate : 1;
                    if(period>12000) period=12000;
                }
            }
            if(s->clock_healthy) s->status=CM_RUNNING;
            else if(s->status!=CM_CLOCK_LOST) s->status=CM_WAIT_CLOCK;
        }
    }
    if(due) {
        release(s,out,&n); onset(s,out,&n);
        if(s->arp.token) {
            s->gate_left=period*s->config.gate/100;
            if(!s->gate_left) s->gate_left=1;
        }
    }
    if(!s->next_token && !s->arp.token) s->status=CM_EXHAUSTED;
    /* All releases precede all onsets, including cross-lane replacements.
     * A stable partition of at most ten records also preserves chord order. */
    for(uint32_t i=1;i<n;++i) if(out[i].kind==CM_OFF) {
        CmEvent e; copy(&e,&out[i],sizeof(e)); uint32_t j=i;
        while(j && out[j-1].kind==CM_ON) {
            copy(&out[j],&out[j-1],sizeof(e)); --j;
        }
        copy(&out[j],&e,sizeof(e));
    }
    s->block=in->block; s->generation=in->generation; s->pattern=in->pattern;
    s->boundary=in->boundary; s->phrase=in->phrase; s->running=in->running; s->initialized=1;
    return (int)n;
}
