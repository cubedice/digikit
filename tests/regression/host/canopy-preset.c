#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/preset.h"
int main(void)
{
    CpPreset p={.control={10,3,127,64,500,127,8,4,3,100,75,2,0,0,127,127},
        .bpm=300,.family=1,.mode=KB_ROOT,.clock=2,.shape={4,{0,7,16,127}}};
    CpPreset decoded={0}; uint8_t bytes[CP_RECORD_BYTES];
    cp_wavetable_defaults(p.control);
    cp_fm_defaults(p.fm);
    if(cp_encode(&p,bytes)) return 1;
    char line[80],cmd[24]; unsigned a,b; int result=0;
    while(fgets(line,sizeof(line),stdin)) {
        a=b=0; if(sscanf(line,"%23s %u %u",cmd,&a,&b)<1) continue;
        if(!strcmp(cmd,"byte") && a<CP_RECORD_BYTES && b<256) bytes[a]=(uint8_t)b;
        else if(!strcmp(cmd,"legacy")) {
            bytes[4]=(uint8_t)a; bytes[5]=a<=2?16:28;
            for(unsigned i=a<=2?51:a==3?92:119;i<CP_RECORD_BYTES;++i) bytes[i]=0;
        }
        else if(!strcmp(cmd,"resign")) {
            uint32_t h=2166136261u;
            unsigned end=bytes[4]<=2?60:bytes[4]==3?92:bytes[4]==4?124:140;
            for(unsigned i=0;i<end;++i) h=(h^bytes[i])*16777619u;
            for(unsigned i=0;i<4;++i) bytes[end+i]=(uint8_t)(h>>(24-8*i));
        } else if(!strcmp(cmd,"decode")) result=cp_decode(&decoded,bytes,a);
        else if(!strcmp(cmd,"encode")) result=cp_encode(&decoded,bytes);
        else if(!strcmp(cmd,"reserved")) decoded.control[CP_RESERVED12]=(uint16_t)a;
        else if(!strcmp(cmd,"record")) {
            printf("{\"version\":%u,\"reserved\":%u}\n",bytes[4],(uint32_t)bytes[32]*256+bytes[33]);
        }
        else if(!strcmp(cmd,"snapshot")) {
            printf("{\"result\":%d,\"controls\":[",result);
            for(unsigned i=0;i<CP_CONTROLS;++i) printf("%s%u",i ? ",":"",decoded.control[i]);
            printf("],\"bpm\":%u,\"family\":%u,\"mode\":%u,\"clock\":%u,\"shape\":[",decoded.bpm,decoded.family,decoded.mode,decoded.clock);
            for(unsigned i=0;i<decoded.shape.count;++i) printf("%s%u",i ? ",":"",decoded.shape.intervals[i]);
            printf("],\"fm\":[");
            for(unsigned i=0;i<13;++i) printf("%s%u",i?",":"",decoded.fm[i]);
            puts("]}");
        } else return 2;
    }
}
