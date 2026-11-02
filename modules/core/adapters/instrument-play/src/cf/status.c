#include "status.h"
static int newer(uint32_t a,uint32_t b) { uint32_t d=a-b; return d && d<0x80000000u; }
static void copy(uint32_t *out,const uint32_t *in,uint32_t count)
{ for(uint32_t i=0;i<count;++i) ((volatile uint32_t *)out)[i]=in[i]; }
void is_select(IsStatus *s,uint32_t track,uint32_t generation,uint32_t mode,uint32_t session)
{
    if(s->track==track && s->generation==generation && s->mode==mode && s->session==session) return;
    for(uint32_t i=0;i<sizeof(*s);++i) ((volatile uint8_t *)s)[i]=0;
    s->track=track; s->generation=generation; s->mode=mode; s->session=session;
}
static int fast_valid(const IsStatus *s,const uint32_t *w)
{
    uint32_t flags=w[1],valid=(flags>>16)&15,count=0,identities=0;
    if((flags&0xf0000000u) || ((flags>>4)&3)>1 || ((flags>>6)&3)!=s->mode ||
        ((flags>>20)&7)>4 || (w[7]&0xf000) ||
        (w[8]>>24) || (w[8]&255)>127 || (w[9]&255)>2 ||
        ((w[9]>>8)&255)>IS_CLOCK_STOPPED || ((w[9]>>16)&255)>3 ||
        ((w[9]>>24)&255)>8) return 0;
    for(uint32_t i=0;i<4;++i) {
        uint32_t pitch=(w[5]>>(8*i))&255,member=(w[6]>>(8*i))&255;
        if(valid&(1u<<i)) {
            uint32_t identity=1u<<((member>>3)&3);
            if(pitch>127 || (member&0xe0) || (identities&identity)) return 0;
            identities|=identity; ++count;
        }
        else if(pitch || member) return 0;
    }
    if(count!=((flags>>20)&7)) return 0;
    if(w[7]&256) { if((w[7]&255)>127) return 0; }
    else if(w[7]&0xfff) return 0;
    if((w[7]&512) && !(identities&(1u<<((w[7]>>10)&3)))) return 0;
    if((flags&(1u<<27)) && (!(flags&(1u<<26)) || !(w[7]&256) || !(w[8]&255))) return 0;
    return 1;
}
static int detail_valid(const IsStatus *s,const uint32_t *w)
{
    /* 0..31 control bank; 32 pending chord; 33 session; 34 capture revision;
     * 35 pitches; 36 count/valid mask/root/full; 37 shape revision. */
    uint32_t flags=w[36],count=flags&7,mask=(flags>>4)&15,root=(flags>>8)&255;
    if(count>4 || (flags&0xfffe0008u) || mask!=((1u<<count)-1) || root>127 ||
        ((flags&(1u<<16)) && count!=4)) return 0;
    if(w[33]!=s->session || (!s->session && (w[34] || w[35] || flags))) return 0;
    if(s->session && s->detail_valid && w[34]!=s->capture_revision && !newer(w[34],s->capture_revision)) return 0;
    uint32_t previous=0;
    for(uint32_t i=0;i<4;++i) {
        uint32_t pitch=(w[35]>>(8*i))&255;
        if(i<count) { if(pitch>127 || (i && pitch<=previous) || (!i && pitch!=root)) return 0; previous=pitch; }
        else if(pitch) return 0;
    }
    if(!count && root) return 0;
    if(s->assembly_schema==2) {
        for(uint32_t i=38;i<42;++i) if(w[i])
            for(uint32_t j=38;j<i;++j) if(w[i]==w[j]) return 0;
    }
    for(uint32_t i=s->assembly_schema==2?42:38;i<IS_DETAIL_WORDS;++i) if(w[i]) return 0;
    return 1;
}
int is_receive(IsStatus *s,const uint32_t w[IS_WORDS],uint32_t now)
{
    if(!s->generation || s->track>15 || s->mode>2 || w[2]!=s->generation || (w[1]&15)!=s->track) return -1;
    if((w[1]>>30)==IS_FAST) {
        if(!fast_valid(s,w) || (s->fast_valid && !newer(w[0],s->fast[0]))) return -1;
        if(s->fast_valid && ((w[3]!=s->fast[3] && !newer(w[3],s->fast[3])) ||
            (w[4]!=s->fast[4] && !newer(w[4],s->fast[4])) ||
            (w[11]!=s->fast[11] && !newer(w[11],s->fast[11])))) return -1;
        copy(s->fast,w,IS_WORDS); s->fast_at=now; s->fast_valid=1;
        if(s->assembling && (s->assembly_revision!=w[11] || s->assembly_control!=w[3])) s->assembling=0;
        return 1;
    }
    if((w[1]&0xfffffff0u)!=0x40000000u || !is_fresh(s,now) || w[3]!=s->fast[11] ||
        w[5]!=s->fast[3] || (w[0]!=s->fast[0] && !newer(w[0],s->fast[0]))) return -1;
    uint32_t index=w[4]&255;
    uint32_t schema=w[4]>>16;
    if((schema!=1 && schema!=2) || (w[4]&0xff00ff00u)!=0x00000b00u || index>=IS_FRAGMENTS || (index==10 && (w[10] || w[11]))) return -1;
    if(s->assembling && now-s->started_at>=IS_TIMEOUT_MS) s->assembling=0;
    if(!s->assembling) {
        /* A transfer may be joined mid-stream; completion still needs all 11
         * fragments. A new revision supersedes an incomplete old snapshot. */
        s->assembly_revision=w[3]; s->assembly_control=w[5]; s->assembly_block=w[0];
        s->fragments=0; s->started_at=now; s->assembling=1;
        s->assembly_schema=schema;
    }
    if(s->assembly_schema!=schema || s->assembly_revision!=w[3] || s->assembly_control!=w[5] ||
        (w[0]!=s->assembly_block && !newer(w[0],s->assembly_block))) return -1;
    uint32_t n=index==10 ? 4 : 6;
    if(s->fragments&(1u<<index)) {
        for(uint32_t i=0;i<n;++i) if(s->assembly[index*6+i]!=w[6+i]) { s->assembling=0; return -1; }
        return 0; /* Identical duplicate cannot extend the assembly deadline. */
    }
    copy(s->assembly+index*6,w+6,n); s->fragments|=1u<<index;
    if(s->fragments!=0x7ff) return 0;
    s->assembling=0;
    if(!detail_valid(s,s->assembly)) return -1;
    if(s->detail_valid && s->detail_revision==w[3]) {
        if(s->detail_control!=w[5] || s->detail_schema!=schema) return -1;
        for(uint32_t i=0;i<IS_DETAIL_WORDS;++i) if(s->detail[i]!=s->assembly[i]) return -1;
    }
    copy(s->detail,s->assembly,IS_DETAIL_WORDS);
    s->detail_revision=w[3]; s->detail_control=w[5]; s->capture_revision=s->detail[34];
    s->detail_at=now; s->detail_valid=1;
    s->detail_schema=schema;
    return 1;
}
int is_fresh(const IsStatus *s,uint32_t now)
{ return s->fast_valid && now-s->fast_at<IS_TIMEOUT_MS; }
static void invalidate(IsStatus *s)
{ s->fast_valid=0; s->detail_valid=0; s->assembling=0; s->retried=0; }
void is_poll(IsStatus *s,uint32_t now,IsRead read)
{
    uint32_t words[IS_WORDS],age=0;
    int accept=s->poll_valid && now-s->poll_at<IS_TIMEOUT_MS;
    if(!accept) invalidate(s);
    /* The adapter has 64 entries plus a possible loss notification. Bounded
     * even if the audio interrupt continues publishing during this drain. */
    for(uint32_t i=0;i<66;++i) {
        int result=read(words,IS_WORDS,&age);
        if(!result) { s->poll_at=now; s->poll_valid=1; return; }
        if(result==-3) { invalidate(s); accept=0; continue; }
        if(result!=IS_WORDS) { invalidate(s); s->poll_valid=0; return; }
        if(accept && age<225) (void)is_receive(s,words,s->poll_at);
    }
    /* No empty-queue boundary: there is no arrival lower bound for next time. */
    invalidate(s); s->poll_valid=0;
}
int is_detail_confirmed(const IsStatus *s,uint32_t now)
{ return is_fresh(s,now) && s->detail_valid && s->detail_revision==s->fast[11] && s->detail_control==s->fast[3]; }
int is_retry(IsStatus *s,uint32_t now)
{
    if(s->assembling && now-s->started_at>=IS_TIMEOUT_MS) s->assembling=0;
    if(!is_fresh(s,now) || is_detail_confirmed(s,now) ||
        (s->assembling && now-s->started_at<IS_TIMEOUT_MS) ||
        (s->retried && now-s->retry_at<IS_TIMEOUT_MS)) return 0;
    s->retry_at=now; s->retried=1; return 1;
}
