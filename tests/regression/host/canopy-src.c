#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/src-state.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/status.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/src-display.h"
static IsStatus status;
static uint32_t fast[12], detail[64], now=100;
static uint32_t detail_schema=1;
static int queued,queue_error,continuous;
static uint32_t queue_age=1;
static int read_status(uint32_t *out,uint32_t words,uint32_t *age)
{
    assert(words==12);
    if(queue_error) { int result=queue_error; queue_error=0; return result; }
    if(!queued && !continuous) return 0;
    if(queued) --queued;
    ++fast[0]; memcpy(out,fast,sizeof(fast)); *age=queue_age; return 12;
}
static void start(void)
{
    is_select(&status,0,12,0,0);
    fast[0]=1000; fast[2]=12; fast[3]=5; fast[4]=8; fast[11]=9;
    assert(is_receive(&status,fast,now)==1);
}
static int fragment(unsigned index)
{
    uint32_t w[12]={0}; w[0]=++fast[0]; w[1]=0x40000000; w[2]=12;
    w[3]=fast[11]; w[4]=(detail_schema<<16)|0xb00|index; w[5]=fast[3];
    for(unsigned i=0;i<(index==10?4:6);++i) w[6+i]=detail[index*6+i];
    return is_receive(&status,w,now);
}
static void complete(void) { for(unsigned i=0;i<11;++i) assert(fragment(i)==(i==10?1:0)); }
int main(int argc,char **argv)
{
    assert(argc==2); const char *scenario=argv[1];
    CsState ui; cs_init(&ui);
    uint32_t token=cs_context(&ui,0,12,1,0,KB_ROOT,0,0,0),id=255;
    ui.pending=0; /* Fixture starts after an applied initial bank. */
    if(!strcmp(scenario,"token-ownership")) {
        CvDisplay display; KbState keyboard={0}; start();
        fast[0]++; fast[1]=(3u<<16)|(2u<<20); fast[5]=60|(60u<<8); fast[6]=8u<<8;
        assert(is_receive(&status,fast,now)==1);
        detail_schema=2; detail[38]=11; detail[39]=12; complete();
        keyboard.generation=12; keyboard.mode=KB_PLAY;
        keyboard.voices[0]=(KbVoice){.token=11,.source=100,.note=60};
        keyboard.voices[1]=(KbVoice){.token=12,.source=101,.note=60};
        keyboard.keys[0]=(KbKey){.token=100,.held=1};
        keyboard.keys[1]=(KbKey){.token=101,.pedal=1};
        cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        cv_build(&display,&ui,&status,now,"0","100",0);
        cv_ownership(&display,&status,&keyboard,now);
        assert(display.membership[0]==1 && display.membership[1]==2);
        keyboard.voices[0].token=13; cv_ownership(&display,&status,&keyboard,now);
        assert(display.membership[0]==0 && display.membership[1]==2);
        keyboard.generation=13; display.membership[0]=0;
        cv_ownership(&display,&status,&keyboard,now); assert(!display.membership[0]);
    } else if(!strcmp(scenario,"poll-freshness")) {
        start(); queued=2; is_poll(&status,100,read_status);
        assert(!status.fast_valid && status.poll_valid && !queued);
        queued=1; is_poll(&status,120,read_status);
        assert(status.fast_at==100 && is_fresh(&status,249) && !is_fresh(&status,250));
        /* A stopped audio callback cannot age the queued frame counter. A UI
         * stall must nevertheless discard it, without stamping it fresh. */
        queued=1; is_poll(&status,300,read_status); assert(!status.fast_valid);
        queued=1; is_poll(&status,310,read_status); assert(status.fast_at==300);
        queue_age=225; queued=1; is_poll(&status,320,read_status);
        assert(status.fast_at==300);
    } else if(!strcmp(scenario,"poll-loss")) {
        start(); is_poll(&status,100,read_status);
        queued=1; is_poll(&status,110,read_status); assert(status.fast_valid);
        queue_error=-3; queued=2; is_poll(&status,120,read_status);
        assert(!status.fast_valid && status.poll_valid && !queued);
        queued=1; is_poll(&status,130,read_status); assert(status.fast_valid);
        queue_error=-2; is_poll(&status,140,read_status);
        assert(!status.fast_valid && !status.poll_valid);
        continuous=1; is_poll(&status,150,read_status);
        assert(!status.fast_valid && !status.poll_valid);
        continuous=0; is_poll(&status,UINT32_MAX-10,read_status);
        queued=1; is_poll(&status,5,read_status);
        assert(status.fast_at==UINT32_MAX-10 && is_fresh(&status,5));
    } else if(!strcmp(scenario,"metadata")) {
        uint32_t revision=ui.revision;
        cs_metadata(&ui,123); assert(ui.revision==revision+1 && ui.pending==0xffff);
        cs_metadata(&ui,123); assert(ui.revision==revision+1);
        uint32_t values[32]={0}; for(unsigned i=0;i<16;++i) values[i]=ui.value[i];
        cs_applied(&ui,12,revision,values); assert(ui.pending==0xffff);
        cs_applied(&ui,12,revision+1,values); assert(!ui.pending);
        ui.revision=UINT32_MAX; cs_metadata(&ui,124); assert(ui.metadata==123);
    } else if(!strcmp(scenario,"controls")) {
        assert(cs_edit(&ui,token,3,12,10,&id)==CS_EDIT && id==CP_COLOR && ui.value[id]==76);
        assert(cs_edit(&ui,token,0,1,20,&id)==CS_NATIVE && id==CS_TUNE);
        assert(cs_edit(&ui,token,7,-1,20,&id)==CS_NATIVE && id==CS_LEVEL);
        token=cs_context(&ui,0,12,1,1,KB_ROOT,0,0,0);
        assert(cs_edit(&ui,token,0,1,30,&id)==CS_EDIT && id==CP_RATE && ui.value[id]==5);
        assert(ui.value[CP_ARP_MIX]==0);
        assert(cs_edit(&ui,token,7,1,30,&id)==CS_READONLY && ui.value[CP_SOURCE]==0);
    } else if(!strcmp(scenario,"stale")) {
        uint32_t old=token; token=cs_context(&ui,0,12,1,1,KB_ROOT,0,0,0);
        assert(cs_edit(&ui,old,3,1,0,&id)==CS_STALE);
        old=token; token=cs_context(&ui,1,12,1,0,KB_ROOT,0,0,0);
        assert(cs_edit(&ui,old,3,1,0,&id)==CS_STALE && ui.page[0]==1);
        old=token; token=cs_context(&ui,1,13,1,0,KB_ROOT,0,0,0);
        assert(cs_edit(&ui,old,3,1,0,&id)==CS_STALE);
        old=token; cs_context(&ui,1,13,0,0,KB_ROOT,0,0,0);
        assert(cs_edit(&ui,old,3,1,0,&id)==CS_STALE && !cs_focused(&ui,0));
    } else if(!strcmp(scenario,"readonly")) {
        token=cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        for(unsigned i=0;i<3;++i) { unsigned knob=i==2?5:i+1; assert(cs_edit(&ui,token,knob,1,0,&id)==CS_READONLY); }
        token=cs_context(&ui,0,12,1,0,KB_ROOT,0,1,0);
        assert(cs_edit(&ui,token,1,1,0,&id)==CS_READONLY);
        assert(cs_edit(&ui,token,2,1,0,&id)==CS_READONLY);
        token=cs_context(&ui,0,12,1,1,KB_ROOT,1,0,0);
        assert(cs_edit(&ui,token,4,1,0,&id)==CS_READONLY);
        token=cs_context(&ui,0,12,1,0,KB_ROOT,0,0,1);
        for(unsigned i=0;i<8;++i) assert(cs_edit(&ui,token,i,1,0,&id)==CS_READONLY);
        assert(!cs_focused(&ui,0));
    } else if(!strcmp(scenario,"clamp")) {
        assert(cs_edit(&ui,token,1,INT32_MAX,0,&id)==CS_EDIT && ui.value[CP_CHORD]==11);
        assert(cs_edit(&ui,token,1,INT32_MIN,0,&id)==CS_EDIT && ui.value[CP_CHORD]==0);
        assert(cs_edit(&ui,token,1,-1,0,&id)==CS_READONLY);
        assert(cs_edit(&ui,token,2,1,0,&id)==CS_EDIT && ui.value[CP_VOICING]==1);
        assert(cs_edit(&ui,token,2,INT32_MAX,0,&id)==CS_EDIT && ui.value[CP_VOICING]==3);
        ui.revision=UINT32_MAX;
        assert(cs_edit(&ui,token,3,1,0,&id)==CS_READONLY && ui.value[CP_COLOR]==64);
    } else if(!strcmp(scenario,"focus")) {
        assert(cs_edit(&ui,token,3,1,UINT32_MAX-100,&id)==CS_EDIT);
        assert(cs_focused(&ui,648) && !cs_focused(&ui,649));
        assert(ui.pending==(1u<<CP_COLOR));
        assert(cs_edit(&ui,token,3,1,700,&id)==CS_EDIT && cs_focused(&ui,1400));
        cs_context(&ui,0,12,1,1,KB_ROOT,0,0,0); assert(!cs_focused(&ui,1400));
    } else if(!strcmp(scenario,"applied")) {
        uint32_t bank[32]={0};
        assert(cs_edit(&ui,token,3,1,0,&id)==CS_EDIT); bank[CP_COLOR]=65;
        cs_applied(&ui,13,ui.revision,bank); assert(ui.pending);
        cs_applied(&ui,12,ui.revision-1,bank); assert(ui.pending);
        assert(cs_edit(&ui,token,3,1,0,&id)==CS_EDIT);
        cs_applied(&ui,12,ui.revision,bank); assert(ui.pending);
        bank[CP_COLOR]=66; cs_applied(&ui,12,ui.revision,bank); assert(!ui.pending);
    } else if(!strcmp(scenario,"freshness")) {
        start(); assert(is_fresh(&status,249) && !is_fresh(&status,250));
        assert(is_receive(&status,fast,200)==-1 && status.fast_at==100);
        fast[0]--; assert(is_receive(&status,fast,200)==-1);
        is_select(&status,1,12,0,0); assert(!is_fresh(&status,100));
        assert(is_receive(&status,fast,100)==-1);
    } else if(!strcmp(scenario,"pitch-zero")) {
        start(); ++fast[0]; fast[1]=(3u<<16)|(2u<<20); fast[5]=127u<<8;
        fast[6]=1|(9u<<8); assert(is_receive(&status,fast,110)==1);
        ++fast[0]; fast[5]=128u<<8; assert(is_receive(&status,fast,120)==-1);
        assert(status.fast[5]==127u<<8 && status.fast_at==110);
    } else if(!strcmp(scenario,"malformed")) {
        start(); uint32_t bad[12];
        const unsigned words[]={1,1,1,5,6,7,7,8,9,2};
        const uint32_t bits[]={1u<<28,1u<<6,5u<<20,1,1,1u<<12,512,1u<<24,3,1};
        for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);++i) {
            memcpy(bad,fast,sizeof(bad)); bad[0]++; bad[words[i]]^=bits[i];
            assert(is_receive(&status,bad,110)==-1 && status.fast_at==100);
        }
    } else if(!strcmp(scenario,"details")) {
        start(); detail[2]=76; complete(); assert(is_detail_confirmed(&status,100));
        assert(status.detail[2]==76);
        ++fast[0]; ++fast[3]; ++fast[11]; assert(is_receive(&status,fast,110)==1);
        assert(!is_detail_confirmed(&status,110) && status.detail[2]==76 && is_fresh(&status,110));
        now=110; detail[2]=90; complete(); assert(is_detail_confirmed(&status,110) && status.detail[2]==90);
    } else if(!strcmp(scenario,"fragment-validation")) {
        start(); assert(fragment(0)==0); detail[0]=1; assert(fragment(0)==-1 && !status.detail_valid);
        assert(fragment(10)==0); detail[63]=1;
        for(unsigned i=0;i<10;++i) assert(fragment(i)==(i==9?1:0));
        /* Previously copied final fragment is immutable; a subsequent snapshot
         * with the same revision and changed data must not replace it. */
        for(unsigned i=0;i<10;++i) assert(fragment(i)==0);
        assert(fragment(10)==-1 && status.detail[63]==0);
    } else if(!strcmp(scenario,"fragment-framing")) {
        start(); uint32_t w[12]={1001,0x40000000,12,9,0x10b00,5,0,0,0,0,0,0};
        const uint32_t framing[]={0x10b0b,0x10a00,0x30b00,0x1010b00};
        for(unsigned i=0;i<4;++i) { w[4]=framing[i]; assert(is_receive(&status,w,now)==-1 && !status.assembling); }
        w[4]=0x10b0a; w[10]=1; assert(is_receive(&status,w,now)==-1);
        w[10]=0; w[11]=1; assert(is_receive(&status,w,now)==-1);
        w[11]=0; w[3]=8; assert(is_receive(&status,w,now)==-1);
        w[3]=9; w[5]=4; assert(is_receive(&status,w,now)==-1);
        assert(!status.detail_valid && !status.assembling && status.fast_at==now);
    } else if(!strcmp(scenario,"timeout")) {
        start(); assert(fragment(0)==0); assert(!is_retry(&status,249));
        now=250; ++fast[0]; assert(is_receive(&status,fast,now)==1);
        assert(is_retry(&status,now) && !status.assembling && !is_retry(&status,399));
        now=400; ++fast[0]; assert(is_receive(&status,fast,now)==1); assert(is_retry(&status,now));
        for(unsigned i=1;i<11;++i) assert(fragment(i)==0);
        assert(!status.detail_valid); assert(fragment(0)==1);
    } else if(!strcmp(scenario,"learn-identity")) {
        is_select(&status,0,12,2,17); fast[0]=100; fast[1]=2u<<6; fast[2]=12; fast[3]=5; fast[11]=9;
        assert(is_receive(&status,fast,now)==1);
        detail[33]=17; detail[34]=3; detail[35]=60|(64u<<8); detail[36]=2|(3u<<4)|(60u<<8);
        complete(); assert(status.capture_revision==3);
        is_select(&status,0,12,2,18); assert(!status.detail_valid);
        ++fast[0]; assert(is_receive(&status,fast,now)==1);
        for(unsigned i=0;i<10;++i) assert(fragment(i)==0);
        assert(fragment(10)==-1 && !status.detail_valid);
    } else if(!strcmp(scenario,"wrap")) {
        start(); status.fast[0]=UINT32_MAX; status.fast[3]=UINT32_MAX; status.fast[4]=UINT32_MAX; status.fast[11]=UINT32_MAX;
        fast[0]=0; fast[3]=0; fast[4]=0; fast[11]=0;
        assert(is_receive(&status,fast,UINT32_MAX-10)==1);
        assert(is_fresh(&status,138) && !is_fresh(&status,139));
        fast[0]=1; fast[3]=UINT32_MAX; assert(is_receive(&status,fast,1)==-1);
    } else if(!strcmp(scenario,"display-units")) {
        CvDisplay display; start(); cs_context(&ui,0,12,1,0,KB_ROOT,0,0,0);
        ui.value[CP_STRUM]=500; ui.value[CP_ARP_MIX]=64;
        cv_build(&display,&ui,&status,now,"-12","100",0);
        assert(!strcmp(display.value[5],"500ms") && !strcmp(display.value[6],"50%"));
        assert(!strcmp(display.label[2],"VOICE"));
        cs_context(&ui,0,12,1,1,KB_ROOT,0,0,0);
        cv_build(&display,&ui,&status,now,"-12","100",0);
        assert(!strcmp(display.value[0],"1/16") && !strcmp(display.value[2],"1OCT"));
        assert(!strcmp(display.value[3],"50%") && !strcmp(display.value[4],"50%"));
        ui.value[CP_RATE]=6; ui.value[CP_SWING]=75;
        cv_build(&display,&ui,&status,now,"-12","100",0);
        assert(!strcmp(display.value[0],"1/16T") && !strcmp(display.value[4],"88%"));
        cs_context(&ui,0,12,1,1,KB_ROOT,1,0,0);
        cv_build(&display,&ui,&status,now,"-12","100",0);
        assert(!strcmp(display.value[0],"6STP") && !strcmp(display.value[4],"--"));
    } else if(!strcmp(scenario,"display-capacity")) {
        CvDisplay display; start(); ++fast[0];
        fast[1]=(7u<<16)|(3u<<20)|(1u<<11);
        fast[5]=60|(64u<<8)|(67u<<16); fast[6]=1|(9u<<8)|(17u<<16); fast[8]=2u<<16;
        assert(is_receive(&status,fast,now)==1); cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        cv_build(&display,&ui,&status,now,"0","100",0);
        assert(!strcmp(display.heading,"C> +2"));
    } else if(!strcmp(scenario,"display-notes")) {
        CvDisplay display; start(); ++fast[0];
        fast[1]=(7u<<16)|(3u<<20)|(1u<<26)|(1u<<27);
        fast[5]=67|(60u<<8)|(64u<<16); fast[6]=17|(1u<<8)|(10u<<16);
        fast[7]=76|256|512|(1u<<10); fast[8]=64;
        assert(is_receive(&status,fast,now)==1);
        cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        cv_build(&display,&ui,&status,now,"0","100",0);
        assert(!strcmp(display.heading,"C") && !strcmp(display.notes[0],"C4") && !strcmp(display.notes[2],"G4"));
        assert(display.highlight==2 && display.membership[1]==2);
        assert(!strcmp(display.value[1],"AUTO") && !strcmp(display.value[2],"--"));
        cs_context(&ui,0,12,1,1,KB_PLAY,0,0,0);
        cv_build(&display,&ui,&status,now,"0","100",0); assert(!strcmp(display.heading,"ARP E5"));
        cv_build(&display,&ui,&status,now+150,"0","100",0);
        assert(!strcmp(display.heading,"LINK LOST") && !strcmp(display.notes[0],"--") && !display.highlight);
        cv_note(display.notes[0],0,1); assert(!strcmp(display.notes[0],"C-1"));
        cv_note(display.notes[0],1,1); assert(!strcmp(display.notes[0],"C#-1"));
        cv_note(display.notes[0],127,1); assert(!strcmp(display.notes[0],"G9"));
    } else if(!strcmp(scenario,"wall-clock")) {
        extern uint32_t cc_milliseconds(uint32_t,uint32_t,uint32_t,uint32_t);
        const uint64_t ticks[]={0,131999,132000,0xffffffffULL,0x100000000ULL,
            132000ULL*0xffffffffULL,132000ULL*0x100000000ULL,0x123456789abcdef0ULL};
        for(unsigned i=0;i<sizeof(ticks)/sizeof(ticks[0]);++i) {
            uint64_t t=ticks[i];
            assert(cc_milliseconds((uint32_t)(t>>32),(uint32_t)t,17,17)==(uint32_t)(t/132000));
            assert(cc_milliseconds((uint32_t)(t>>32),(uint32_t)t,0xffff0000,66000)==(uint32_t)((t+131536)/132000));
        }
        // A stalled UI sees elapsed hardware time without an audio callback.
        assert(cc_milliseconds(0,132000000,100,19800100)==1150);
    } else if(!strcmp(scenario,"display-clock")) {
        CvDisplay display; start();
        cs_context(&ui,0,12,1,1,KB_PLAY,1,0,0);
        fast[1]=(1u<<16)|(1u<<20)|(1u<<26)|(1u<<27);
        fast[5]=60; fast[6]=0; fast[7]=60|256|512; fast[8]=100;
        const char *heading[]={"WAIT CLK","CLK LOST","STOPPED"};
        const unsigned states[]={IS_CLOCK_WAIT,IS_CLOCK_LOST,IS_CLOCK_STOPPED};
        for(unsigned i=0;i<3;++i) {
            ++fast[0]; fast[9]=1|(states[i]<<8)|(3u<<16)|(1u<<24);
            assert(is_receive(&status,fast,now)==1);
            cv_build(&display,&ui,&status,now,"0","100",0);
            assert(!strcmp(display.heading,heading[i]) && !display.highlight);
            assert(!strcmp(display.notes[0],"C4"));
        }
        ++fast[0]; fast[9]=1|(5u<<8); assert(is_receive(&status,fast,now)==-1);
    } else if(!strcmp(scenario,"duplicate-tone")) {
        CvDisplay display; start(); ++fast[0];
        fast[1]=(3u<<16)|(2u<<20)|(1u<<26)|(1u<<27);
        fast[5]=60|(60u<<8); fast[6]=17|(9u<<8); fast[7]=72|256|512|(1u<<10); fast[8]=100;
        assert(is_receive(&status,fast,now)==1); cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        cv_build(&display,&ui,&status,now,"0","100",0); assert(display.highlight==2);
        ++fast[0]; fast[6]=9|(9u<<8); assert(is_receive(&status,fast,now)==-1);
    } else if(!strcmp(scenario,"display-focus")) {
        CvDisplay display; start(); token=cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        assert(cs_edit(&ui,token,1,1,100,&id)==CS_READONLY);
        cv_build(&display,&ui,&status,now,"-12","100",0); assert(!strcmp(display.heading,"YOUR NOTES"));
        assert(cs_edit(&ui,token,6,32,100,&id)==CS_EDIT);
        cv_build(&display,&ui,&status,now,"-12","100",0);
        assert(display.mix_bar && display.mix_requested==32 && display.mix_applied==0);
        cv_build(&display,&ui,&status,now,"-12","100",1); assert(!strcmp(display.heading,"NOT SAVED") && !display.mix_bar);
    } else if(!strcmp(scenario,"preview")) {
        CvDisplay display; uint8_t pixels[1024]={0}; start(); ++fast[0];
        fast[1]=(7u<<16)|(3u<<20); fast[5]=60|(64u<<8)|(67u<<16); fast[6]=1|(9u<<8)|(17u<<16);
        assert(is_receive(&status,fast,now)==1); cs_context(&ui,0,12,1,0,KB_PLAY,0,0,0);
        cv_build(&display,&ui,&status,now,"0","100",0); cv_paint(pixels,&display);
        puts("P1\n128 64");
        for(unsigned y=0;y<64;++y) { for(unsigned x=0;x<128;++x) printf("%u ",!!(pixels[y*16+x/8]&(128u>>(x&7)))); puts(""); }
        return 0;
    } else if(!strcmp(scenario,"pixels")) {
        CvDisplay display; uint8_t guarded[1026]; memset(guarded,255,sizeof(guarded));
        start(); cv_build(&display,&ui,&status,now,"-12","100",0); cv_paint(guarded+1,&display);
        assert(guarded[0]==255 && guarded[1025]==255);
        for(unsigned i=0;i<160;++i) assert(guarded[1+i]==255);
        for(unsigned y=10;y<64;++y) assert((guarded[1+y*16+15]&15)==15);
        unsigned ink=0; for(unsigned i=160;i<1024;++i) if(guarded[1+i]) ++ink;
        assert(ink>100 && ink<700);
    } else { fprintf(stderr,"unknown scenario %s\n",scenario); return 2; }
    puts("ok"); return 0;
}
