#include "keyboard.h"

static void zero(void *p, uint32_t n) { volatile uint8_t *b=p; while (n--) *b++=0; }
void kb_init(KbState *s)
{
    zero(s,sizeof(*s)); s->next_token=1; s->phrase=1; s->selector_base=24;
    s->selector_channel=MI_NONE;
}
void kb_reset(KbState *s, uint32_t epoch, uint32_t generation)
{
    zero(s->keys,sizeof(s->keys)); zero(s->voices,sizeof(s->voices));
    s->epoch=epoch; s->generation=generation; s->next_token=1; s->phrase=1; s->pedals=0;
    s->status=KB_OK; s->update=0;
}
static int selector(const KbState *s, const MiEvent *e)
{
    return s->mode == KB_ROOT && (s->selector_channel != MI_NONE ?
        e->port!=MI_PANEL &&
        e->channel == s->selector_channel :
        e->note >= s->selector_base && e->note < s->selector_base+12);
}
static int active(const KbKey *k)
{ return k->token && !k->stolen && (k->held || k->pedal); }
static void collect(KbState *s)
{
    uint32_t i;
    for (i=0;i<MI_KEYS;++i)
        if (s->keys[i].token && !s->keys[i].held && !active(&s->keys[i]))
            zero(&s->keys[i],sizeof(KbKey));
}
static void capture(KbState *s, uint8_t note)
{
    uint32_t i,j;
    for (i=0;i<s->capture_count;++i) if (s->capture[i]==note) return;
    if (s->capture_count==4) { s->status=KB_FULL; return; }
    i=s->capture_count++;
    while (i && s->capture[i-1]>note) { s->capture[i]=s->capture[i-1]; --i; }
    s->capture[i]=note;
    j=s->revision+1;
    if (!j) { s->status=KB_EXHAUSTED; return; }
    s->revision=j;
}
static int note(KbState *s, const MiEvent *e)
{
    uint32_t i, n=0, oldest=MI_KEYS, free=MI_KEYS;
    if (selector(s,e)) return 0;
    if (e->kind==MI_NOTE_OFF) {
        for (i=0;i<MI_KEYS;++i) if (s->keys[i].token==e->token) {
            KbKey *k=&s->keys[i]; k->held=0;
            k->pedal=k->port!=MI_PANEL && !!(s->pedals & (1u<<(k->port*16+k->channel)));
            break;
        }
        collect(s); return 0;
    }
    if (!e->token) return -1;
    for (i=0;i<MI_KEYS;++i) if (s->keys[i].token==e->token) return -1;
    uint32_t sounding=0;
    for (i=0;i<MI_KEYS;++i) sounding+=!!active(&s->keys[i]);
    if (s->mode==KB_ROOT || !sounding) {
        if (s->phrase==0x7fffffffu) { s->status=KB_EXHAUSTED; return -1; }
        ++s->phrase;
    }
    for (i=0;i<MI_KEYS;++i) {
        KbKey *k=&s->keys[i];
        if (!k->token && free==MI_KEYS) free=i;
        if (active(k)) {
            ++n;
            if (oldest==MI_KEYS || k->token<s->keys[oldest].token) oldest=i;
        }
    }
    if (free==MI_KEYS) return -1;
    if (s->mode!=KB_ROOT && n>=4) s->keys[oldest].stolen=1;
    KbKey *k=&s->keys[free];
    k->token=e->token; k->note=e->note; k->velocity=e->value;
    k->port=e->port; k->channel=e->channel; k->held=1;
    k->extra_count=s->stage_count;
    for (i=0;i<KB_EXTRA;++i) k->extra[i]=i<s->stage_count ? s->stage[i] : 0;
    if (s->mode==KB_LEARN) capture(s,e->note);
    return 0;
}
static void controller(KbState *s, const MiEvent *e)
{
    uint32_t i, bit=e->port==MI_PANEL ? 0 : 1u<<(e->port*16+e->channel);
    if (e->note==64) {
        if (e->value>=64) s->pedals|=bit; else s->pedals&=~bit;
    } else if (e->note==120 || e->note==121) s->pedals&=~bit;
    for (i=0;i<MI_KEYS;++i) {
        KbKey *k=&s->keys[i];
        if (!k->token || k->port!=e->port || k->channel!=e->channel) continue;
        if (e->note==120) zero(k,sizeof(*k));
        else if (e->note==123) { k->held=0; k->pedal=!!(s->pedals&bit); }
        else if (e->note==121 || (e->note==64 && e->value<64)) k->pedal=0;
    }
    collect(s);
}
static void voice(KbVoice *v, uint32_t source, uint8_t pitch, const KbKey *k)
{
    v->source=source; v->token=0; v->note=pitch; v->velocity=k->velocity;
    v->port=k->port; v->channel=k->channel;
}
static uint32_t desired(KbState *s, KbVoice v[4])
{
    static const uint8_t shapes[12][5]={
        {3,0,4,7}, {4,0,4,7,9}, {3,0,2,7}, {4,0,3,7,9},
        {3,0,3,7}, {3,0,5,7}, {4,0,4,7,14}, {4,0,4,7,10},
        {3,0,3,6}, {4,0,3,7,10}, {0}, {4,0,4,7,11}
    };
    uint32_t i,j,n=0; KbKey *root=0;
    zero(v,4*sizeof(*v));
    for (i=0;i<MI_KEYS;++i) if (active(&s->keys[i])) {
        KbKey *k=&s->keys[i];
        if (s->mode!=KB_ROOT) {
            if (n==4) break;
            voice(&v[n++],k->token,k->note,k);
            /* A recorded chord adds its other notes after its own. */
            for (j=0;j<k->extra_count && j<KB_EXTRA && n<4;++j) {
                int32_t pitch=(int32_t)k->note+k->extra[j];
                if (pitch>=0 && pitch<=127) voice(&v[n++],k->token,(uint8_t)pitch,k);
            }
        } else if (!root || (k->held && !root->held) ||
            (k->held==root->held && k->token>root->token)) root=k;
    }
    if (!root) return n;
    if (root->extra_count) {
        /* A recorded chord plays its notes, not the Sound's chord shape. */
        voice(&v[n++],0,root->note,root);
        for (j=0;j<root->extra_count && j<KB_EXTRA && n<4;++j) {
            int32_t pitch=(int32_t)root->note+root->extra[j];
            if (pitch>=0 && pitch<=127) voice(&v[n++],0,(uint8_t)pitch,root);
        }
        return n;
    }
    const uint8_t *intervals=shapes[s->type]+1;
    n=shapes[s->type][0];
    if (s->type==10) {
        const KbShape *shape=s->preset_shape_active ? &s->preset_shape : &s->shapes[s->slot];
        n=shape->count; intervals=shape->intervals;
    }
    if (s->status==KB_RANGE || s->status==KB_EMPTY) s->status=KB_OK;
    if (!n) s->status=KB_EMPTY;
    for (i=0;i<n;++i) {
        uint32_t pitch=root->note+intervals[i];
        /* USER intervals already contain the learned voicing. Retain the
         * generated-chord preference, but do not apply it to this shape. */
        if (s->type!=10 && s->voicing==1 && i==1) pitch+=12;
        if (s->type!=10 && s->voicing>=2 && i<(uint32_t)(s->voicing-1)) pitch+=12;
        if (pitch>127) { s->status=KB_RANGE; return 0; }
        voice(&v[i],0,(uint8_t)pitch,root);
    }
    return n;
}
static int same(const KbVoice *a, const KbVoice *b)
{ return a->source==b->source && a->note==b->note && a->velocity==b->velocity && a->port==b->port && a->channel==b->channel; }
static void event(KbState *s, KbVoice *v, uint8_t kind, MiEvent *e)
{
    zero(e,sizeof(*e)); e->epoch=s->epoch; e->generation=s->generation;
    e->token=v->token; e->kind=kind; e->note=v->note; e->value=v->velocity;
    e->port=v->port; e->channel=v->channel;
}
int kb_process(KbState *s, const MiEvent *in, uint32_t count, MiEvent out[8])
{
    KbVoice want[4]; uint32_t i,j,n,used=0,phrase=s->phrase;
    if (count>8 || s->status==KB_EXHAUSTED || s->mode>KB_LEARN ||
        s->type>=12 || s->slot>=8 || s->voicing>3 ||
        (s->selector_channel!=MI_NONE && s->selector_channel>15)) return -1;
    if (!count && !s->update) return 0;
    for (i=0;i<count;++i) {
        const MiEvent *e=&in[i];
        /* The transport batches one track; the caller routes only the admitted one here. */
        if (e->epoch!=s->epoch || e->generation!=s->generation ||
            e->port>MI_PANEL || e->channel>15 || e->note>127 || e->value>127 ||
            (e->port==MI_PANEL && (e->channel || (e->kind>MI_NOTE_OFF &&
                !(e->kind==MI_CC && e->note==120)))) ||
            e->kind<MI_NOTE_ON || e->kind>MI_BEND ||
            (count>1 && e->kind>MI_NOTE_OFF)) return -1;
    }
    /* The complete accepted note batch chooses type before resolving roots. */
    for (i=0;i<count;++i) if (in[i].kind==MI_NOTE_ON && selector(s,&in[i]))
        s->type=in[i].note%12;
    for (i=0;i<count;++i) {
        if (in[i].kind<=MI_NOTE_OFF) { if (note(s,&in[i])) return -1; }
        else if (in[i].kind==MI_CC) controller(s,&in[i]);
    }
    n=desired(s,want);
    int route_release=0;
    if (count==1 && in[0].kind==MI_CC && (in[0].note==120 || in[0].note==121)) {
        route_release=1;
        for (i=0;i<n;++i) if (want[i].port==in[0].port && want[i].channel==in[0].channel)
            route_release=0;
        if (in[0].note==120 || route_release) {
            mi_event_copy(&out[used],&in[0]); out[used].token=0;
            if (in[0].note==121) out[used].note=123;
            ++used;
        }
    }
    for (i=0;i<4;++i) if (s->voices[i].token) {
        for (j=0;j<n;++j) if (!want[j].token && same(&s->voices[i],&want[j])) {
            want[j].token=s->voices[i].token; break;
        }
        if (j==n && !(route_release && s->voices[i].port==in[0].port &&
            s->voices[i].channel==in[0].channel)) {
            if (used>=8) return -1;
            event(s,&s->voices[i],MI_NOTE_OFF,&out[used++]);
        }
    }
    for (i=0;i<n;++i) if (!want[i].token) {
        if (used>=8) return -1;
        if (!s->next_token) { s->status=KB_EXHAUSTED; return -1; }
        want[i].token=s->next_token++;
        event(s,&want[i],MI_NOTE_ON,&out[used++]);
    }
    if (count==1 && (in[0].kind==MI_BEND ||
        (in[0].kind==MI_CC && (in[0].note==1 || in[0].note==121)))) {
        if (used>=8) return -1;
        mi_event_copy(&out[used],&in[0]); out[used].token=0;
        if (in[0].kind==MI_CC && in[0].note==121) {
            out[used].kind=MI_BEND; out[used].note=0; out[used].value=64;
        }
        ++used;
        if (in[0].kind==MI_CC && in[0].note==121) {
            /* Pedal ownership is resolved above, so forward only the
             * wheel reset, alongside centered bend, on this same route. */
            if (used>=8) return -1;
            mi_event_copy(&out[used],&in[0]); out[used].token=0;
            out[used].note=1; out[used].value=0; ++used;
        }
    }
    for (i=0;i<4;++i) {
        volatile uint8_t *to=(volatile uint8_t *)&s->voices[i];
        const volatile uint8_t *from=(const volatile uint8_t *)&want[i];
        for (j=0;j<sizeof(KbVoice);++j) to[j]=from[j];
    }
    // A new root phrase can retain every common tone. Publish its
    // identity even when there are no audible note differences.
    if (!used && phrase!=s->phrase) {
        zero(&out[0],sizeof(out[0]));
        out[0].kind=MI_COMMIT; out[0].epoch=s->epoch;
        out[0].generation=s->generation; used=1;
    }
    for (i=0;i<used;++i)
        out[i].sequence=s->phrase | (s->mode==KB_ROOT ? 0x80000000u : 0);
    s->update=0;
    return (int)used;
}
int kb_process_locked(KbState *s,const MiEvent *in,uint32_t count,MiEvent out[8])
{
    if(count!=1 || !in || !in[0].lock_valid || s->mode==KB_LEARN)
        return kb_process(s,in,count,out);
    if(in[0].kind!=MI_NOTE_ON || in[0].port!=MI_PANEL || in[0].lock_count>MI_LOCKS) return -1;
    uint8_t old_mode=s->mode,old_type=s->type,old_voicing=s->voicing;
    uint32_t chord=UINT32_MAX,voicing=UINT32_MAX,seen=0;
    int8_t extra[KB_EXTRA]; uint32_t extras=0;
    for(uint32_t i=0;i<in[0].lock_count;++i) {
        uint32_t id=in[0].locks[i]>>10,value=in[0].locks[i]&1023u;
        if(id==0) { if(chord!=UINT32_MAX || value>12) return -1; chord=value; }
        if(id==1) { if(voicing!=UINT32_MAX || value>3) return -1; voicing=value; }
        if(id>=KB_LOCK_NOTE_FIRST && id<KB_LOCK_NOTE_FIRST+KB_EXTRA) {
            uint32_t bit=1u<<(id-KB_LOCK_NOTE_FIRST);
            if((seen&bit) || value>2u*KB_LOCK_NOTE_CENTER) return -1;
            seen|=bit;
            int8_t interval=(int8_t)((int32_t)value-(int32_t)KB_LOCK_NOTE_CENTER);
            int duplicate=!interval;
            for(uint32_t j=0;j<extras;++j) if(extra[j]==interval) duplicate=1;
            if(!duplicate) extra[extras++]=interval;
        }
    }
    if(chord!=UINT32_MAX) {
        s->mode=chord ? KB_ROOT : KB_PLAY;
        if(chord) s->type=(uint8_t)(chord-1);
    }
    /* Recorded notes play as recorded, whatever the step's chord choice. */
    if(extras) s->mode=KB_PLAY;
    if(voicing!=UINT32_MAX) s->voicing=(uint8_t)voicing;
    s->stage_count=(uint8_t)extras;
    for(uint32_t j=0;j<KB_EXTRA;++j) s->stage[j]=j<extras ? extra[j] : 0;
    int result=kb_process(s,in,count,out);
    s->stage_count=0;
    if(chord!=UINT32_MAX || extras) { s->mode=old_mode; s->type=old_type; }
    if(voicing!=UINT32_MAX) s->voicing=old_voicing;
    return result;
}
int kb_preview_locked(KbState *s,uint8_t pitch,const uint16_t *locks,uint32_t count,uint8_t pitches[4])
{
    if(!s || !pitches || count>MI_LOCKS || (count && !locks) || s->mode==KB_LEARN) return -1;
    kb_reset(s,1,1);
    s->selector_base=255; s->selector_channel=MI_NONE;
    MiEvent in; MiEvent out[8]; zero(&in,sizeof(in));
    in.epoch=1; in.generation=1; in.token=1; in.kind=MI_NOTE_ON;
    in.port=MI_PANEL; in.note=pitch; in.value=100; in.lock_valid=1; in.lock_count=(uint8_t)count;
    for(uint32_t i=0;i<count;++i) in.locks[i]=locks[i];
    int n=kb_process_locked(s,&in,1,out); if(n<0) return -1;
    uint32_t found=0;
    for(int i=0;i<n && found<4;++i) if(out[i].kind==MI_NOTE_ON) {
        uint8_t p=out[i].note; uint32_t at=found++;
        while(at && pitches[at-1]>p) { pitches[at]=pitches[at-1]; --at; }
        pitches[at]=p;
    }
    return (int)found;
}
int kb_learn_begin(KbState *s)
{
    if (s->mode==KB_LEARN || s->session==UINT32_MAX) return -1;
    s->previous_mode=s->mode;
    s->mode=KB_LEARN; ++s->session;
    s->revision=1; s->capture_count=0; s->status=KB_OK; return 0;
}
int kb_learn_save(KbState *s, uint32_t session, uint32_t revision)
{
    uint32_t i;
    if (s->mode!=KB_LEARN || session!=s->session || revision!=s->revision) {
        s->status=KB_STALE; return -1;
    }
    if (!s->capture_count) { s->status=KB_EMPTY; return -1; }
    KbShape *shape=&s->shapes[s->slot]; zero(shape,sizeof(*shape));
    shape->count=s->capture_count;
    for (i=0;i<shape->count;++i) shape->intervals[i]=s->capture[i]-s->capture[0];
    s->preset_shape_active=0;
    s->dirty=1; s->mode=s->previous_mode; s->status=KB_OK; return 0;
}
void kb_learn_cancel(KbState *s)
{ if (s->mode==KB_LEARN) s->mode=s->previous_mode;
    s->capture_count=0; s->status=KB_OK; }
void kb_encode(const KbState *s, uint8_t b[KB_RECORD_BYTES])
{
    uint32_t i,j; zero(b,KB_RECORD_BYTES); b[0]='C'; b[1]='P'; b[2]=1; b[3]=8;
    for (i=0;i<8;++i) { b[4+i*5]=s->shapes[i].count;
        for (j=0;j<4;++j) b[5+i*5+j]=s->shapes[i].intervals[j]; }
    for (i=0;i<44;++i) { b[44]+=b[i]; b[45]+=b[44]; }
}
int kb_decode(KbState *s, const uint8_t *b, uint32_t length)
{
    uint32_t i,j; uint8_t a=0,c=0;
    if (length!=KB_RECORD_BYTES || b[0]!='C' || b[1]!='P' || b[2]!=1 || b[3]!=8 || b[46] || b[47]) return -1;
    for (i=0;i<44;++i) { a+=b[i]; c+=a; }
    if (a!=b[44] || c!=b[45]) return -1;
    for (i=0;i<8;++i) {
        const uint8_t *p=b+4+i*5;
        if (p[0]>4 || p[1]) return -1;
        for (j=0;j<4;++j) if ((j>=p[0] && p[j+1]) || p[j+1]>127 ||
            (j && j<p[0] && p[j+1]<=p[j])) return -1;
    }
    for (i=0;i<8;++i) { s->shapes[i].count=b[4+i*5];
        for (j=0;j<4;++j) s->shapes[i].intervals[j]=b[5+i*5+j]; }
    return 0;
}
