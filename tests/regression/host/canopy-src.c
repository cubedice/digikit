#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/src-state.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/status.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/src-display.h"
static IsStatus status;
static uint32_t fast[12], detail[64], now=100;
static void start(void)
{
    is_select(&status,0,12,0,0);
    fast[0]=1000; fast[2]=12; fast[3]=5; fast[4]=8; fast[11]=9;
    assert(is_receive(&status,fast,now)==1);
}
static int fragment(unsigned index)
{
    uint32_t w[12]={0}; w[0]=++fast[0]; w[1]=0x40000000; w[2]=12;
    w[3]=fast[11]; w[4]=0x10b00|index; w[5]=fast[3];
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
    if(!strcmp(scenario,"controls")) {
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
        const uint32_t framing[]={0x10b0b,0x10a00,0x20b00,0x1010b00};
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
