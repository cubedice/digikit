/* Stream driver for the actual CF engine, physical FIFO and retained transport.
 * Assertions belong to the TypeScript scenarios, not a second musical model. */
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/keyboard.h"
#include "../../../modules/core/adapters/midi-events/src/cf/transport.h"
static KbState s;
static MiLedger ledger;
static MiTransport transport;
static uint32_t block;
static int result;
static uint8_t saved[KB_RECORD_BYTES];
static int mapping(uint32_t track,uint32_t epoch,uint32_t generation,
    const MiEvent *in,uint32_t count,MiEvent *out)
{
    (void)track;
    if (s.epoch!=epoch || s.generation!=generation) kb_reset(&s,epoch,generation);
    return kb_process(&s,in,count,out);
}
static void reset(void)
{
    mi_reset_track(&ledger,0);
    mi_reset_ack(&ledger,ledger.epoch,0,ledger.generations[0]);
    kb_reset(&s,ledger.epoch,ledger.generations[0]);
    transport.pending=0; transport.applied[0]=1;
    for (unsigned p=0;p<2;++p) for (unsigned c=0;c<16;++c)
        mi_route(&ledger,p,c,0,ledger.generations[0],0);
}
static void snapshot(const uint32_t *packet)
{
    printf("{\"result\":%d,\"mode\":%u,\"status\":%u,\"type\":%u,\"revision\":%u,\"session\":%u,\"queued\":%u,\"reset\":%u,\"voices\":[",
        result,s.mode,s.status,s.type,s.revision,s.session,ledger.count,ledger.reset_mask);
    unsigned n=0;
    for (unsigned i=0;i<4;++i) if(s.voices[i].token) {
        KbVoice *v=&s.voices[i];
        printf("%s[%u,%u,%u,%u,%u]",n++ ? ",":"",v->note,v->token,v->velocity,v->port,v->channel);
    }
    printf("],\"capture\":[");
    for(unsigned i=0;i<s.capture_count;++i) printf("%s%u",i ? ",":"",s.capture[i]);
    printf("],\"shape\":[");
    for(unsigned i=0;i<s.shapes[s.slot].count;++i) printf("%s%u",i ? ",":"",s.shapes[s.slot].intervals[i]);
    printf("],\"packet\":[");
    if(packet) for(unsigned i=0;i<32;++i) printf("%s%u",i ? ",":"",packet[i]);
    puts("]}");
}
int main(void)
{
    char line[100],cmd[24]; unsigned a,b,c,d;
    kb_init(&s); mi_init(&ledger); mt_init(&transport); transport.epoch=ledger.epoch; reset();
    while(fgets(line,sizeof(line),stdin)) {
        a=b=c=d=0; if(sscanf(line,"%23s %u %u %u %u",cmd,&a,&b,&c,&d)<1) continue;
        if(!strcmp(cmd,"on") || !strcmp(cmd,"off") || !strcmp(cmd,"cc") || !strcmp(cmd,"bend")) {
            uint8_t bytes[3]={(uint8_t)((!strcmp(cmd,"on")?0x90:!strcmp(cmd,"off")?0x80:!strcmp(cmd,"cc")?0xb0:0xe0)|a),(uint8_t)c,(uint8_t)d};
            result=mi_message(&ledger,b,bytes,3);
        } else if(!strcmp(cmd,"tick") || !strcmp(cmd,"hold")) {
            const uint32_t *p=mt_prepare_mapped(&transport,&ledger,transport.applied,++block,mapping);
            snapshot(p);
            if(p && !strcmp(cmd,"tick")) mt_ack(&transport,&ledger,ledger.epoch,p[3]);
        } else if(!strcmp(cmd,"ack")) result=mt_ack(&transport,&ledger,a,b);
        else if(!strcmp(cmd,"mode")) { s.mode=(uint8_t)a; reset(); }
        else if(!strcmp(cmd,"reset")) reset();
        else if(!strcmp(cmd,"latch")) kb_latch(&s,a);
        else if(!strcmp(cmd,"type")) { s.type=(uint8_t)a; s.update=1; }
        else if(!strcmp(cmd,"selector")) s.selector_channel=(uint8_t)a;
        else if(!strcmp(cmd,"voicing")) { s.voicing=(uint8_t)a; s.update=1; }
        else if(!strcmp(cmd,"learn")) { result=kb_learn_begin(&s); reset(); }
        else if(!strcmp(cmd,"save")) { result=kb_learn_save(&s,a ? a:s.session,b ? b:s.revision); if(!result) reset(); }
        else if(!strcmp(cmd,"cancel")) { kb_learn_cancel(&s); reset(); }
        else if(!strcmp(cmd,"encode")) kb_encode(&s,saved);
        else if(!strcmp(cmd,"corrupt")) saved[a]^=(uint8_t)b;
        else if(!strcmp(cmd,"resign")) {
            saved[44]=saved[45]=0;
            for(unsigned i=0;i<44;++i) { saved[44]+=saved[i]; saved[45]+=saved[44]; }
        }
        else if(!strcmp(cmd,"recover")) { mi_recover(&ledger); mt_init(&transport); transport.epoch=ledger.epoch; reset(); }
        else if(!strcmp(cmd,"decode")) result=kb_decode(&s,saved,a ? a:sizeof(saved));
        else if(!strcmp(cmd,"block")) block+=a;
        else if(!strcmp(cmd,"token")) s.next_token=a;
        else if(!strcmp(cmd,"directcc")) {
            MiEvent in={0},out[9]; memset(out,0x5a,sizeof(out));
            in.epoch=s.epoch; in.generation=s.generation; in.kind=MI_CC;
            in.note=(uint8_t)a; in.port=1;
            result=kb_process(&s,&in,1,out);
            for(unsigned i=0;i<sizeof(MiEvent);++i)
                if(((unsigned char *)&out[8])[i]!=0x5a) return 3;
        }
        else if(!strcmp(cmd,"snapshot")) snapshot(0);
        else return 2;
    }
    return 0;
}
