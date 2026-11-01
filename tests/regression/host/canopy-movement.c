/* Stream driver for the movement policy; musical assertions live in Vitest. */
#include <stdio.h>
#include <string.h>
#include "../../../modules/machines/canopy/src/movement.h"
static CmState state;
static CmInput input;
static struct { uint32_t before; CmEvent events[CM_MAX_EVENTS]; uint32_t after; } buffer;
static void output(int result)
{
    printf("{\"block\":%u,\"result\":%d,\"status\":%u,\"ordinal\":%u,\"phase\":%u,\"events\":[",
        input.block,result,state.status,state.ordinal,state.phase);
    for(int i=0;i<result;++i) {
        const CmEvent *e=&buffer.events[i];
        printf("%s[%u,%u,%u,%u,%u,%u,%u]",i ? ",":"",e->kind,e->lane,e->note.token,
            e->note.note,e->note.velocity,e->note.port,e->note.channel);
    }
    puts("]}");
}
int main(void)
{
    cm_init(&state,1234); input.generation=1; input.pattern=1;
    buffer.before=0x12345678; buffer.after=0x87654321;
    char command[32];
    while(scanf("%31s",command)==1) {
        if(!strcmp(command,"config")) {
            CmConfig c;
            if(scanf("%u%u%u%u%u%u%u%u%u%u",&c.clock,&c.rate,&c.order,&c.octaves,
                &c.gate,&c.swing,&c.restart,&c.bpm,&c.strum,&c.mix)!=10) return 2;
            int r=cm_configure(&state,&c); if(r) output(r);
        } else if(!strcmp(command,"chord")) {
            if(scanf("%u%u%u%u",&input.revision,&input.phrase,&input.generated,&input.count)!=4 || input.count>4)
                return 2;
            memset(input.notes,0,sizeof(input.notes));
            for(unsigned i=0;i<input.count;++i) {
                unsigned note,velocity,port,channel;
                if(scanf("%u%u%u%u%u",&input.notes[i].token,&note,&velocity,&port,&channel)!=5) return 2;
                input.notes[i].note=(uint8_t)note; input.notes[i].velocity=(uint8_t)velocity;
                input.notes[i].port=(uint8_t)port; input.notes[i].channel=(uint8_t)channel;
            }
        } else if(!strcmp(command,"clock")) {
            if(scanf("%u%u%u%u%u",&input.valid,&input.running,&input.generation,&input.boundary,&input.pattern)!=5)
                return 2;
        } else if(!strcmp(command,"block")) {
            if(scanf("%u",&input.block)!=1) return 2;
        } else if(!strcmp(command,"token")) {
            if(scanf("%u",&state.next_token)!=1) return 2;
        } else if(!strcmp(command,"tick")) {
            unsigned n; if(scanf("%u",&n)!=1 || n>1000000) return 2;
            while(n--) {
                int r=cm_tick(&state,&input,buffer.events);
                if(r) output(r);
                if(r<0) break;
                ++input.block;
            }
        } else if(!strcmp(command,"panic")) {
            output((int)cm_panic(&state,buffer.events));
        } else if(!strcmp(command,"state")) output(0);
        else return 2;
        if(buffer.before!=0x12345678 || buffer.after!=0x87654321) return 3;
    }
    return 0;
}
