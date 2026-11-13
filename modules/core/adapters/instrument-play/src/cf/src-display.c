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
static const char *const labels[37]={"CHORD","VOICING","COLOR","DRIFT","STRUM","ARP MIX",
    "RATE","ORDER","OCTAVES","GATE","SWING","RESTART","","SOURCE","ATTACK","RELEASE",
    "WAVE","POSITION","SCAN DEPTH","SCAN RATE","SCAN SHAPE","SCAN RESTART","SCAN CLOCK",
    "TIMBRE ENV","TIMBRE ATTACK","TIMBRE RELEASE","VELOCITY","MOD WHEEL",
    "WT FM DEPTH","WT FM RATIO","WT FM ENVELOPE","WT FM MOD WHEEL","TUNE","LEVEL","WAVE","",""};
static const char *const fm_labels[12]={"ALGORITHM","CARRIER BALANCE","PAIR A RATIO","PAIR B RATIO",
    "FEEDBACK","FM ATTACK","FM DECAY","FM SUSTAIN","FM RELEASE","FM ENVELOPE","FM VELOCITY","FM MOD WHEEL"};
static int fm_mode(const CsState *s) { return ((s->metadata>>4)&3)==3; }
uint8_t cv_lock_mask(const CsState *s,const IlpSnapshot *locks)
{
    uint32_t family=(s->metadata>>4)&3,mask=0;
    if(!locks) return 0;
    for(uint32_t i=0;i<8;++i) {
        uint32_t control=cs_mapped_control(s,i);
        if(control==CS_TUNE || control==CS_LEVEL || control==CS_NONE) continue;
        uint32_t id=ilp_id_for(family,control);
        if(id==UINT32_MAX) continue;
        for(uint32_t j=0;j<locks->count && j<ILP_LANES;++j)
            if(locks->pair[j].id==id) { mask|=1u<<i; break; }
    }
    return (uint8_t)mask;
}
static int pending(const CsState *s,uint32_t id)
{ return id==CS_WAVE ? !!s->pending : id==CS_TUNE ? !!s->native_tune_pending : id<CP_CONTROLS && !!(s->pending&(1u<<id)); }
static const char *label(const CsState *s,uint32_t id)
{ return fm_mode(s) && id==CP_COLOR ? "FM DEPTH" : fm_mode(s) && id>=16 && id<28 ? fm_labels[id-16] : labels[id]; }
static void value(char out[8],const CsState *s,uint32_t id,const char *tune,const char *level)
{
    static const char *const chord[]={"MAJ","MAJ6","SUS2","MIN6","MIN","SUS4","ADD9","DOM7","DIM","MIN7","USER","MAJ7"};
    static const char *const voice[]={"CLS","OPEN","INV1","INV2"};
    static const char *const order[]={"UP","DOWN","UPDN","PLAY","RND"};
    static const char *const restart[]={"TRIG","PATT","CONT"};
    static const char *const rate[]={"--","1/4","1/8","1/8T","1/16","5/Q","1/16T","7/Q","1/32"};
    if(id==CS_TUNE || id==CS_LEVEL) { text(out,8,id==CS_TUNE?tune:level); return; }
    if(id==CS_NONE || id==CS_CLOCK || id==CS_BPM) { out[0]=0; return; }
    if(id==CS_WAVE) {
        static const char *const waves[]={"VELVT","REED","ANLG","GLASS","VOCAL","ORGAN","METAL","FM"};
        uint32_t wave=cs_choice_value(s,id);
        text(out,8,wave<8?waves[wave]:"--"); return;
    }
    uint32_t n=s->value[id]; const char *v=0;
    if(id==CP_WTFM_RATIO) {
        static const char *const ratios[]={"0.25","0.50","0.75","1.00","1.25","1.41","1.50","1.73","2.00","2.50","3.00","4.00","5.00","6.00","7.00","8.00"};
        text(out,8,n<16?ratios[n]:"--"); return;
    }
    if(id==CP_WTFM_DEPTH) { unit(out,n,"%"); return; }
    if(id==CP_WTFM_ENV || id==CP_WTFM_WHEEL) {
        if(n==100) text(out,8,"0%");
        else { out[0]=n<100?'-':'+'; unit(out+1,n<100?100-n:n-100,"%"); }
        return;
    }
    if(fm_mode(s) && id>=16 && id<28) {
        if(id==CP_FM_ALG) { static const char *const alg[]={"TWIN","STACK","SUM","CHAIN"}; text(out,8,n<4?alg[n]:"--"); }
        else if(id==CP_FM_RTA || id==CP_FM_RTB) {
            static const char *const ratios[]={"0.25","0.50","0.75","1.00","1.25","1.41","1.50","1.73","2.00","2.50","3.00","4.00","5.00","6.00","7.00","8.00"};
            text(out,8,n<16?ratios[n]:"--");
        } else if(id>=CP_FM_ENV) {
            if(n==100) text(out,8,"0%");
            else { out[0]=n<100?'-':'+'; unit(out+1,n<100?100-n:n-100,"%"); }
        } else if(id==CP_FM_BAL || id==CP_FM_FB || id==CP_FM_SUSTAIN) unit(out,n,"%");
        else number(out,n);
        return;
    }
    if(id>=CP_WT_TABLE && id<CP_CONTROLS && ((s->metadata>>4)&3)!=2) { text(out,8,"--"); return; }
    if(id==CP_WT_POS) { out[0]=(char)('1'+n/100); out[1]='.'; out[2]=(char)('0'+n/10%10); out[3]=(char)('0'+n%10); out[4]=0; return; }
    if(id==CP_WT_DEPTH) { unit(out,n,"%"); return; }
    if(id==CP_WT_ENV || id==CP_WT_VELOCITY || id==CP_WT_WHEEL) {
        if(n==100) text(out,8,"0%");
        else { out[0]=n<100?'-':'+'; unit(out+1,n<100?100-n:n-100,"%"); }
        return;
    }
    if(id==CP_WT_RATE) {
        if(s->value[CP_WT_CLOCK]) {
            static const char *const divisions[]={"--","4BAR","2BAR","1BAR","1/2","1/4","1/8","1/16"};
            text(out,8,n<=7?divisions[n]:"--"); return;
        }
        number(out,n/100); uint32_t at=length(out,7); out[at++]='.';
        out[at++]=(char)('0'+n/10%10); out[at++]=(char)('0'+n%10); out[at]=0; return;
    }
    if(id==CP_WT_SHAPE) { static const char *const shapes[]={"SINE","TRI","RAND"}; text(out,8,n<3?shapes[n]:"--"); return; }
    if(id==CP_WT_RESET) { static const char *const resets[]={"FREE","TRIG","PATT"}; text(out,8,n<3?resets[n]:"--"); return; }
    if(id==CP_WT_CLOCK) { text(out,8,n?"SYNC":"FREE"); return; }
    if(id==CP_CHORD) v=s->mode==KB_PLAY?"PLAY":n<12?chord[n]:"--";
    else if(id==CP_VOICING) v=s->mode==KB_PLAY?"--":s->learned?"LRND":n<4?voice[n]:"--";
    else if(id==CP_STRUM && s->mode==KB_PLAY) v="--";
    else if(id==CP_STRUM) { if(!n) v="OFF"; else { number(out,n); return; } }
    else if(id==CP_RATE) v=n<=8?rate[n]:"--";
    else if(id==CP_OCTAVES) {
        if(n==3) v="0";
        else { out[0]=n<3?'-':'+'; number(out+1,n<3?3-n:n-3); return; }
    }
    else if(id==CP_GATE) { unit(out,n,"%"); return; }
    else if(id==CP_SWING) {
        /* Existing DSP alternates 100+n / 100-n, so the long interval
         * occupies (100+n)/2 percent of a pair. Round only the display. */
        unit(out,(101+n)/2,"%"); return;
    }
    else if(id==CP_ORDER) v=n<5?order[n]:"--";
    else if(id==CP_RESTART) v=n<3?restart[n]:"--";
    else if(id==CP_SOURCE) v="KEYS";
    else if(id==CP_ARP_MIX && (n==0 || n==127)) v=n?"ARP":"PAD";
    else if(id==CP_ARP_MIX) { unit(out,(n*100+63)/127,"%"); return; }
    if(v) text(out,6,v); else number(out,n);
}
void cv_feedback(char out[32],const CsState *s,uint32_t encoder)
{
    static const char *const names[]={"Chord","Voicing","Color","Drift","Strum","Arp Mix",
        "Rate","Order","Octaves","Gate","Swing","Restart","","Source","Attack","Release",
        "Wave","Position","Scan Depth","Scan Rate","Scan Shape","Scan Restart","Scan Clock",
        "Timbre Env","Timbre Attack","Timbre Release","Velocity","Mod Wheel",
        "WT FM Depth","WT FM Ratio","WT FM Envelope","WT FM Mod Wheel"};
    uint32_t id=cs_mapped_control(s,encoder);
    out[0]=0;
    if(id>=CP_CONTROLS && id!=CS_WAVE) return;
    if(id>=CP_WT_TABLE && id<CP_CONTROLS && ((s->metadata>>4)&3)<2) { text(out,32,"WAVE: CHOOSE ANLG TO FM"); return; }
    if(s->mode==KB_PLAY && (id==CP_VOICING || id==CP_STRUM)) {
        text(out,32,"CHRD: CHOOSE A CHORD FIRST"); return;
    }
    if(s->learned && id==CP_VOICING) {
        text(out,32,"ROOT: LEARNED VOICING"); return;
    }
    const char *name=id==CS_WAVE ? "Wave" : fm_mode(s) && (id==CP_COLOR || id>=16) ? label(s,id) : names[id];
    text(out,32,name); uint32_t at=length(out,31); out[at++]='=';
    static const char *const voicings[]={"Closed","Open","Inversion 1","Inversion 2"};
    static const char *const orders[]={"Up","Down","Up/Down","As Played","Random"};
    static const char *const restarts[]={"Trigger","Pattern","Continuous"};
    char v[8]; value(v,s,id,"","");
    const char *full=v; uint32_t n=id<CP_CONTROLS ? s->value[id] : 0;
    if(id==CS_WAVE) {
        static const char *const waves[]={"VELVET","REED","ANALOG","GLASS","VOCAL","ORGAN","METAL","FM"};
        uint32_t wave=cs_choice_value(s,id); if(wave<8) full=waves[wave];
    }
    else if((id==CP_WTFM_RATIO || (fm_mode(s) && (id==CP_FM_RTA || id==CP_FM_RTB))) && (n==5 || n==7)) full=n==5?"SQRT2":"SQRT3";
    else if(id==CP_CHORD && s->mode==KB_PLAY) full="PLAY (YOUR NOTES)";
    else if(id==CP_VOICING && n<4) full=voicings[n];
    else if(id==CP_ORDER && n<5) full=orders[n];
    else if(id==CP_RESTART && n<3) full=restarts[n];
    text(out+at,32-at,full);
    at=length(out,31);
    if(!fm_mode(s) && id==CP_WT_RATE && !s->value[CP_WT_CLOCK]) { text(out+at,32-at," Hz"); at=length(out,31); }
    if(id==CP_STRUM && s->value[id]) { text(out+at,32-at," ms"); at=length(out,31); }
    if(pending(s,id)) text(out+at,32-at,"*");
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
    if(!is_detail_confirmed(s,now) || s->detail_schema<2 ||
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
                d->membership[rank]=(uint8_t)((!!k->keys[i].held) | ((!!k->keys[i].pedal)<<1));
    }
    d->pedal=(uint8_t)!!k->pedals;
}
void cv_build(CvDisplay *d,const CsState *s,const IsStatus *status,uint32_t now,
    const char *tune,const char *level,int save_error)
{
    for(uint32_t i=0;i<sizeof(*d);++i) ((volatile uint8_t *)d)[i]=0;
    uint32_t page=s->page[s->track<16?s->track:0];
    int fresh=is_fresh(status,now) && status->track==s->track && status->generation==s->generation && status->mode==s->mode;
    d->page=(uint8_t)page; d->family=(uint8_t)((s->metadata>>4)&3); d->fresh=(uint8_t)fresh;
    d->focus_id=cs_focused(s,now)?s->focus_id:CS_NONE;
    for(uint32_t i=0;i<CP_CONTROLS;++i) d->raw[i]=s->value[i];
    for(uint32_t i=0;i<8;++i) {
        uint32_t id=cs_mapped_control(s,i);
        if(id>CS_WAVE) continue;
        static const char *const short_labels[]={"CHRD","VOIC","COLR","DRFT","STRM","MIX",
            "RATE","ORDR","OCTS","GATE","SWNG","RST","","SRC","ATK","REL",
            "TABL","POS","DEP","RATE","SHAP","RST","CLK","ENV","ATK","REL","VEL","WHEL",
            "DEP","RAT","ENV","WHEL","TUNE","LEV","WAVE"};
        static const char *const fm_short[]={"ALG","BAL","RTA","RTB","FDBK","ATK","DEC","SUS","REL","ENV","VEL","WHEL"};
        const char *short_label=fm_mode(s) && id==CP_COLOR ? "DEP" : fm_mode(s) && id>=16 && id<28 ? fm_short[id-16] : short_labels[id];
        if(!fm_mode(s) && id==CP_WT_ENV) short_label="PENV";
        if(!fm_mode(s) && id==CP_WTFM_ENV) short_label="FENV";
        if(!fm_mode(s) && id==CP_WTFM_DEPTH) short_label="FM";
        text(d->label[i],8,!fm_mode(s) && id==CP_WT_RATE && !s->value[CP_WT_CLOCK]?"HZ":short_label);
        value(d->value[i],s,id,tune,level);
        if((id<CS_TUNE || id>=CS_WAVE) && (!fresh || !is_detail_confirmed(status,now) ||
            pending(s,id))) d->pending|=(uint8_t)(1u<<i);
        if(id==CP_COLOR || id==CP_DRIFT || id==CP_ARP_MIX || id==CP_GATE || id==CP_SWING ||
            (id==CP_STRUM && s->mode!=KB_PLAY) ||
            (((s->metadata>>4)&3)==2 && (id==CP_WT_POS || id==CP_WT_DEPTH ||
                id==CP_WT_ATTACK || id==CP_WT_RELEASE || id==CP_WT_ENV || id==CP_WT_VELOCITY || id==CP_WTFM_DEPTH || id==CP_WTFM_ENV || id==CP_WTFM_WHEEL)) ||
            (fm_mode(s) && id>=16 && id<28 && id!=CP_FM_ALG && id!=CP_FM_RTA && id!=CP_FM_RTB)) {
            uint32_t max=id>=CP_WT_TABLE ? cs_choice_max(s,id) : id==CP_GATE?100:id==CP_SWING?75:id==CP_STRUM?500:127;
            d->rotary|=(uint8_t)(1u<<i); d->knob[i]=(uint8_t)(s->value[id]*127/max);
        }
    }
    text(d->mode,6,s->mode==KB_PLAY?"K:PLY":s->mode==KB_ROOT?"K:RT":"K:LRN");
    for(uint32_t i=0;i<4;++i) cv_note(d->notes[i],0,0);
    if(!fresh) { d->fault=1; text(d->heading,22,"LINK LOST"); return; }
    uint32_t flags=status->fast[1],count=(flags>>20)&7;
    d->pedal=(uint8_t)((flags>>8)&1);
    uint8_t pitches[4],ids[4]; uint32_t n=0;
    for(uint32_t i=0;i<4;++i) if(flags&(1u<<(16+i))) {
        uint8_t pitch=(uint8_t)(status->fast[5]>>(i*8)); uint32_t at=n;
        while(at && pitches[at-1]>pitch) { pitches[at]=pitches[at-1]; ids[at]=ids[at-1]; --at; }
        pitches[at]=pitch; ids[at]=(uint8_t)i; ++n;
    }
    for(uint32_t i=0;i<n;++i) {
        d->pitches[i]=pitches[i];
        cv_note(d->notes[i],pitches[i],1); d->membership[i]=(uint8_t)(status->fast[6]>>(ids[i]*8))&3;
        uint32_t identity=(status->fast[6]>>(ids[i]*8+3))&3;
        if((flags&(1u<<27)) && (status->fast[7]&512) && ((status->fast[7]>>10)&3)==identity) d->highlight|=(uint8_t)(1u<<i);
    }
    d->note_count=(uint8_t)n;
    if(n) {
        chord_name(d->playing,pitches,n);
        if(d->playing[0]=='N') {
            text(d->playing,22,d->notes[0]);
            if(n>1) { uint32_t at=length(d->playing,18); d->playing[at++]='+'; number(d->playing+at,n-1); }
        }
    } else text(d->playing,22,(flags&(7u<<23))?"RELEASE":"NO NOTES");
    if(flags&(1u<<14)) { d->fault=2; d->highlight=0; text(d->heading,22,"VOICE ERR"); return; }
    if(save_error) { d->fault=3; text(d->heading,22,"NOT SAVED"); return; }
    uint32_t clock=(status->fast[9]>>8)&255;
    d->clock_state=(uint8_t)clock;
    d->arp_note=(uint8_t)status->fast[7];
    d->arp_active=(uint8_t)!!(status->fast[7]&256);
    if(clock==IS_CLOCK_WAIT || clock==IS_CLOCK_LOST || clock==IS_CLOCK_STOPPED)
        d->highlight=0;
    if((status->fast[8]&255) && clock==IS_CLOCK_LOST) {
        d->fault=4; text(d->heading,22,"CLK LOST"); return;
    }
    if(cs_focused(s,now)) {
        d->focused=1; d->highlight=0;
        uint32_t id=s->focus_id;
        if(s->mode==KB_PLAY && id==CP_CHORD) text(d->heading,22,"PLAY YOUR NOTES");
        else if(s->mode==KB_PLAY && (id==CP_VOICING || id==CP_STRUM)) text(d->heading,22,"CHOOSE CHRD FIRST");
        else if(s->learned && id==CP_VOICING) text(d->heading,22,"AS LEARNED");
        else if(id<=CS_WAVE) {
            text(d->heading,22,label(s,id)); uint32_t at=length(d->heading,21);
            d->heading[at++]=' '; char v[8]; value(v,s,id,tune,level); text(d->heading+at,22-at,v);
            at=length(d->heading,21);
            if(pending(s,id) && at+1<22) text(d->heading+at,22-at,"*");
        }
        if(id==CP_ARP_MIX) { d->mix_bar=1; d->mix_requested=(uint8_t)s->value[id]; d->mix_applied=(uint8_t)status->fast[8]; }
        return;
    }
    if(page && (status->fast[8]&255) && clock==IS_CLOCK_STOPPED) text(d->heading,22,"STOPPED");
    else if(page && (status->fast[8]&255) && clock==IS_CLOCK_LOST) text(d->heading,22,"CLK LOST");
    else if(page && (status->fast[8]&255) && clock==IS_CLOCK_WAIT) text(d->heading,22,"WAIT CLK");
    else if(!count) text(d->heading,22,(flags&(7u<<23))?"RELEASE":"NO NOTES");
    else if(page==3) text(d->heading,22,fm_mode(s)?"FM ENVELOPE":"WAVETABLE FM");
    else if(page==2) text(d->heading,22,fm_mode(s)?"FM":((s->metadata>>4)&3)==2?"WAVETABLE":((s->metadata>>4)&3)==1?"REED":"VELVET");
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
