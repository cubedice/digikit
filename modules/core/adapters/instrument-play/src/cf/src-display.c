#include "src-display.h"
#if defined(__m68k__) && defined(__GNUC__)
/* GCC otherwise lowers the bounded formatter to inline jump-table data. */
#pragma GCC optimize ("no-jump-tables")
#endif
static void text(char *out,uint32_t cap,const char *in)
{ uint32_t i=0; if(!cap) return; while(i+1<cap && in && in[i]) { out[i]=in[i]; ++i; } out[i]=0; }
static uint32_t length(const char *s,uint32_t cap)
{ uint32_t n=0; while(n<cap && ((const volatile char *)s)[n]) ++n; return n; }
static void number(char *out,uint32_t n)
{
    uint32_t at=0; if(n>=100) out[at++]=(char)('0'+n/100%10);
    if(n>=10) out[at++]=(char)('0'+n/10%10);
    out[at++]=(char)('0'+n%10); out[at]=0;
}
static void unit(char out[6],uint32_t n,const char *suffix)
{ number(out,n); uint32_t at=length(out,5); text(out+at,6-at,suffix); }
void cv_note(char out[6],uint32_t note,int valid)
{
    static const char *const names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if(!valid || note>127) { text(out,6,"--"); return; }
    const char *name=names[note%12]; uint32_t at=0;
    while(*name) out[at++]=*name++;
    if(note<12) { out[at++]='-'; out[at++]='1'; }
    else out[at++]=(char)('0'+note/12-1);
    out[at]=0;
}
static const char *const labels[18]={"CHORD","VOICING","COLOR","DRIFT","STRUM","ARP MIX",
    "RATE","ORDER","OCTAVES","GATE","SWING","RESTART","LATCH","SOURCE","ATTACK","RELEASE","TUNE","LEVEL"};
static void value(char out[8],const CsState *s,uint32_t id,const char *tune,const char *level)
{
    static const char *const chord[]={"MAJ","MAJ6","SUS2","MIN6","MIN","SUS4","ADD9","DOM7","DIM","MIN7","USER","MAJ7"};
    static const char *const voice[]={"CLOSE","OPEN","INV1","INV2"};
    static const char *const order[]={"UP","DOWN","UP/DN","PLAY","RND"};
    static const char *const restart[]={"TRIG","PATT","CONT"};
    static const char *const rate[]={"--","1/4","1/8","1/8T","1/16","5/Q","1/16T","7/Q","1/32"};
    if(id==CS_TUNE || id==CS_LEVEL) { text(out,8,id==CS_TUNE?tune:level); return; }
    uint32_t n=s->value[id]; const char *v=0;
    if(id==CP_CHORD) v=s->mode==KB_PLAY?"AUTO":s->learned?"USER":n<12?chord[n]:"--";
    else if(id==CP_VOICING) v=s->mode==KB_PLAY?"--":s->learned?"ASLRN":n<4?voice[n]:"--";
    else if(id==CP_STRUM && s->mode==KB_PLAY) v="--";
    else if(id==CP_STRUM) { if(!n) v="OFF"; else { unit(out,n,"ms"); return; } }
    else if(id==CP_RATE) {
        if(s->clock==1) { unit(out,n,"STP"); return; }
        v=n<=8?rate[n]:"--";
    }
    else if(id==CP_OCTAVES) { unit(out,n,"OCT"); return; }
    else if(id==CP_GATE) { unit(out,n,"%"); return; }
    else if(id==CP_SWING) {
        if(s->clock==1) v="--";
        /* Existing DSP alternates 100+n / 100-n, so the long interval
         * occupies (100+n)/2 percent of a pair. Round only the display. */
        else { unit(out,(101+n)/2,"%"); return; }
    }
    else if(id==CP_ORDER) v=n<5?order[n]:"--";
    else if(id==CP_RESTART) v=n<3?restart[n]:"--";
    else if(id==CP_SOURCE) v="KEYS";
    else if(id==CP_LATCH) v=n?"ON":"OFF";
    else if(id==CP_ARP_MIX && (n==0 || n==127)) v=n?"ARP":"PAD";
    else if(id==CP_ARP_MIX) { unit(out,(n*100+63)/127,"%"); return; }
    if(v) text(out,6,v); else number(out,n);
}
static void chord_name(char out[22],const uint8_t *pitch,uint32_t count)
{
    /* Exact pitch-class sets only: inversions may use a slash bass, ambiguous
     * and extended sets deliberately retain a literal note heading. */
    static const uint16_t masks[]={0x091,0x089,0x085,0x0a1,0x049,0x491,0x891,0x489};
    static const char *const suffix[]={"","m","sus2","sus4","dim","7","maj7","m7"};
    static const char *const pc[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    text(out,22,"NOTES"); if(!count) return;
    for(uint32_t root=0;root<12;++root) {
        uint32_t mask=0;
        for(uint32_t i=0;i<count;++i) mask|=1u<<((pitch[i]+12-root)%12);
        for(uint32_t j=0;j<8;++j) if(mask==masks[j]) {
            text(out,22,pc[root]); uint32_t at=length(out,21);
            text(out+at,22-at,suffix[j]); at=length(out,21);
            if(pitch[0]%12!=root) { out[at++]='/'; text(out+at,22-at,pc[pitch[0]%12]); }
            return;
        }
    }
}
void cv_ownership(CvDisplay *d,const IsStatus *s,const KbState *k,uint32_t now)
{
    if(!is_detail_confirmed(s,now) || s->detail_schema!=2 ||
        s->generation!=k->generation || s->mode!=k->mode) return;
    for(uint32_t slot=0;slot<4;++slot) {
        if(!(s->fast[1]&(1u<<(16+slot)))) continue;
        uint32_t pitch=(s->fast[5]>>(slot*8))&255,rank=0;
        for(uint32_t j=0;j<4;++j) if(s->fast[1]&(1u<<(16+j))) {
            uint32_t other=(s->fast[5]>>(j*8))&255;
            if(other<pitch || (other==pitch && j<slot)) ++rank;
        }
        d->membership[rank]=0;
        uint32_t token=s->detail[38+slot];
        if(!token) continue;
        for(uint32_t v=0;v<4;++v) if(k->voices[v].token==token && k->voices[v].note==pitch)
            for(uint32_t i=0;i<MI_KEYS;++i) if(k->keys[i].token && k->keys[i].token==k->voices[v].source)
                d->membership[rank]=(uint8_t)((!!k->keys[i].held) | ((!!k->keys[i].pedal)<<1) | ((!!k->keys[i].latched)<<2));
    }
    d->pedal=(uint8_t)!!k->pedals; d->latch=k->latch?1:0;
    for(uint32_t i=0;i<MI_KEYS;++i) if(k->keys[i].token && k->keys[i].latched) d->latch=2;
}
void cv_build(CvDisplay *d,const CsState *s,const IsStatus *status,uint32_t now,
    const char *tune,const char *level,int save_error)
{
    for(uint32_t i=0;i<sizeof(*d);++i) ((volatile uint8_t *)d)[i]=0;
    uint32_t page=s->page[s->track<16?s->track:0];
    int fresh=is_fresh(status,now) && status->track==s->track && status->generation==s->generation && status->mode==s->mode;
    for(uint32_t i=0;i<8;++i) {
        uint32_t id=cs_control(page,i);
        if(id>=18) continue;
        text(d->label[i],8,id==CP_VOICING?"VOICE":id==CP_OCTAVES?"OCTS":labels[id]);
        value(d->value[i],s,id,tune,level);
        if(id<=CS_TUNE && (!fresh || !is_detail_confirmed(status,now) || (s->pending&(1u<<id)))) d->pending|=(uint8_t)(1u<<i);
    }
    text(d->mode,6,s->mode==KB_PLAY?"K:PLY":s->mode==KB_ROOT?"K:RT":"K:LRN");
    for(uint32_t i=0;i<4;++i) cv_note(d->notes[i],0,0);
    if(!fresh) { text(d->heading,22,"LINK LOST"); return; }
    uint32_t flags=status->fast[1],count=(flags>>20)&7;
    d->latch=(flags&(1u<<10))?2:(flags&(1u<<9))?1:0; d->pedal=(uint8_t)((flags>>8)&1);
    uint8_t pitches[4],ids[4]; uint32_t n=0;
    for(uint32_t i=0;i<4;++i) if(flags&(1u<<(16+i))) {
        uint8_t pitch=(uint8_t)(status->fast[5]>>(i*8)); uint32_t at=n;
        while(at && pitches[at-1]>pitch) { pitches[at]=pitches[at-1]; ids[at]=ids[at-1]; --at; }
        pitches[at]=pitch; ids[at]=(uint8_t)i; ++n;
    }
    for(uint32_t i=0;i<n;++i) {
        cv_note(d->notes[i],pitches[i],1); d->membership[i]=(uint8_t)(status->fast[6]>>(ids[i]*8))&7;
        uint32_t identity=(status->fast[6]>>(ids[i]*8+3))&3;
        if((flags&(1u<<27)) && (status->fast[7]&512) && ((status->fast[7]>>10)&3)==identity) d->highlight|=(uint8_t)(1u<<i);
    }
    if(flags&(1u<<14)) { d->highlight=0; text(d->heading,22,"VOICE ERR"); return; }
    if(save_error) { text(d->heading,22,"NOT SAVED"); return; }
    uint32_t clock=(status->fast[9]>>8)&255;
    if(clock==IS_CLOCK_WAIT || clock==IS_CLOCK_LOST || clock==IS_CLOCK_STOPPED)
        d->highlight=0;
    if(cs_focused(s,now)) {
        d->focused=1; d->highlight=0;
        uint32_t id=s->focus_id;
        if(s->mode==KB_PLAY && (id==CP_CHORD || id==CP_VOICING || id==CP_STRUM)) text(d->heading,22,"YOUR NOTES");
        else if(s->learned && (id==CP_CHORD || id==CP_VOICING)) text(d->heading,22,"AS LEARNED");
        else if(id<18) {
            text(d->heading,22,labels[id]); uint32_t at=length(d->heading,21);
            d->heading[at++]=' '; char v[8]; value(v,s,id,tune,level); text(d->heading+at,22-at,v);
            at=length(d->heading,21);
            if(id<=CS_TUNE && (s->pending&(1u<<id)) && at+1<22) text(d->heading+at,22-at,"*");
        }
        if(id==CP_ARP_MIX) { d->mix_bar=1; d->mix_requested=(uint8_t)s->value[id]; d->mix_applied=(uint8_t)status->fast[8]; }
        return;
    }
    if(page && (status->fast[8]&255) && clock==IS_CLOCK_STOPPED) text(d->heading,22,"STOPPED");
    else if(page && (status->fast[8]&255) && clock==IS_CLOCK_LOST) text(d->heading,22,"CLK LOST");
    else if(page && (status->fast[8]&255) && clock==IS_CLOCK_WAIT) text(d->heading,22,"WAIT CLK");
    else if(!count) text(d->heading,22,(flags&(7u<<23))?"RELEASE":"NO NOTES");
    else if(!page) chord_name(d->heading,pitches,n);
    else if(!(status->fast[8]&255)) text(d->heading,22,"ARP OFF");
    else if(status->fast[7]&256) { text(d->heading,22,"ARP "); cv_note(d->heading+4,status->fast[7]&255,1); }
    else text(d->heading,22,"ARP --");
    if(!page) {
        uint32_t at=length(d->heading,21);
        if(flags&(1u<<11)) { text(d->heading+at,22-at,">"); ++at; }
        uint32_t excluded=(status->fast[8]>>16)&255;
        if(excluded && at+5<22) {
            d->heading[at++]=' '; d->heading[at++]='+'; number(d->heading+at,excluded);
        }
    }
}
/* Small original 3x5 glyphs, row-major packed bits. The larger 5x7 face uses
 * replicated strokes; all strings are bounded and each cell clips separately. */
static uint16_t glyph(char c)
{
    static const uint16_t digits[]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    static const uint16_t letters[]={0x2bed,0x6bae,0x7927,0x6b6e,0x79a7,0x79a4,0x796f,0x5bed,0x7497,
        0x124e,0x5bad,0x4927,0x5fed,0x5ffd,0x7b6f,0x7be4,0x7b7b,0x7bad,0x79cf,0x7492,
        0x5b6f,0x5b6a,0x5bfd,0x5aad,0x5a92,0x72a7};
    if(c>='0' && c<='9') return digits[c-'0'];
    if(c>='A' && c<='Z') return letters[c-'A'];
    if(c=='m') return 0x01fd;
    if(c=='a') return 0x03df;
    if(c=='j') return 0x024e;
    if(c=='s') return 0x078f;
    if(c=='u') return 0x016f;
    if(c=='d') return 0x13ef;
    if(c=='i') return 0x2092;
    if(c=='b') return 0x49ae;
    if(c=='#') return 0x5f7d;
    if(c==':') return 0x0410;
    if(c=='.') return 0x0002;
    if(c=='-') return 0x01c0;
    if(c=='/') return 0x12a4;
    if(c=='+') return 0x05d0;
    if(c=='*') return 0x0baa;
    if(c=='%') return 0x52a5;
    if(c=='>') return 0x4454;
    return 0;
}
static void pixel(uint8_t *p,int x,int y,int set)
{
    if(x<0 || x>=124 || y<10 || y>=64) return;
    uint32_t i=(uint32_t)y*16+(uint32_t)x/8; uint8_t mask=(uint8_t)(128u>>(x&7));
    if(set) p[i]|=mask; else p[i]&=(uint8_t)~mask;
}
static void draw(uint8_t *p,const char *s,int x,int y,int large,int right)
{
    int width=large?5:3,height=large?7:5;
    for(uint32_t n=0;s[n] && n<22;++n,x+=width+1) {
        uint16_t g=glyph(s[n]);
        for(int row=0;row<height;++row) for(int col=0;col<width;++col)
            if(x+col<right && (g&(1u<<(14-(row*5/height)*3-col*3/width)))) pixel(p,x+col,y+row,1);
    }
}
void cv_paint(uint8_t p[1024],const CvDisplay *d)
{
    for(int y=10;y<64;++y) for(int x=0;x<124;++x) pixel(p,x,y,0);
    for(uint32_t i=0;i<8;++i) {
        int x=(int)(i%4)*31,y=i<4?11:48;
        uint32_t chars=length(d->value[i],7); int large=chars<=5;
        int inset=(31-(int)chars*(large?6:4)+1)/2;
        draw(p,d->label[i],x,y,0,x+30); draw(p,d->value[i],x+inset,y+7,large,x+30);
        if(d->pending&(1u<<i)) draw(p,"*",x+28,y,0,x+31);
    }
    draw(p,d->heading,2,28,1,79); draw(p,d->mode,80,29,0,100);
    draw(p,"L",103,29,0,108); draw(p,"P",115,29,0,120);
    if(d->latch==1) {
        for(int x=101;x<109;++x) { pixel(p,x,27,1); pixel(p,x,35,1); }
        for(int y=27;y<=35;++y) { pixel(p,101,y,1); pixel(p,108,y,1); }
    }
    for(uint32_t i=0;i<4;++i) {
        if(d->focused) break;
        int x=(int)i*31;
        draw(p,d->notes[i],x+2,37,1,x+30);
        for(int col=2;col<26;++col) {
            int line=(d->membership[i]&1) || ((d->membership[i]&2) && !(col&1)) ||
                (!(d->membership[i]&3) && (d->membership[i]&4) && col>=10 && col<18);
            if(line) pixel(p,x+col,45,1);
        }
        if(d->highlight&(1u<<i)) for(int y=36;y<45;++y) for(int col=1;col<27;++col) {
            int px=x+col; uint32_t at=(uint32_t)y*16+(uint32_t)px/8; p[at]^=(uint8_t)(128u>>(px&7));
        }
    }
    if(d->latch==2 || d->pedal) for(int y=27;y<=35;++y) for(int x=101;x<=121;++x) {
        if((x<=109 && d->latch==2) || (x>=113 && d->pedal)) p[y*16+x/8]^=(uint8_t)(128u>>(x&7));
    }
    if(d->mix_bar) {
        for(int y=36;y<46;++y) for(int x=0;x<124;++x) pixel(p,x,y,0);
        draw(p,"PAD",2,38,0,20); draw(p,"ARP",110,38,0,124);
        for(int x=21;x<=103;++x) pixel(p,x,42,1);
        int requested=21+d->mix_requested*82/127,applied=21+d->mix_applied*82/127;
        for(int y=37;y<44;++y) pixel(p,applied,y,1);
        pixel(p,requested,36,1); pixel(p,requested-1,35,1); pixel(p,requested+1,35,1);
    }
}
