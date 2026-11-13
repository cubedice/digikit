#include <assert.h>
#include <stdint.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/src-lock-packed.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/src-lock-event.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/src-lock-apply.h"
#include "../../../modules/core/adapters/midi-events/src/cf/transport.h"
#include "../../../modules/core/adapters/midi-events/src/cf/lock-wire.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/keyboard.h"
#include <stddef.h>

static int32_t stored[ILP_LANES]={-1,-1,-1,-1,-1};
static unsigned writes, fail_write;
MiTransport mi_transport;
MiLedger mi_ledger;
static uint32_t masked;
uint32_t dkrt_irq_mask(void) { uint32_t old=masked; masked=1; return old; }
void dkrt_irq_restore(uint32_t old) { assert(masked); masked=old; }
static uint32_t admitted_tracks=1u<<1;
int instrument_sound_admitted(uint32_t track) { return track<16 && (admitted_tracks&(1u<<track)); }
static int route_ready=1;
int instrument_midi_ready(uint32_t track,uint32_t epoch,uint32_t generation)
{ (void)epoch; (void)generation; return instrument_sound_admitted(track) && route_ready; }
extern volatile uint32_t ip_lock_enabled,ip_lock_capture_error;
void ip_lock_capture(uint32_t track,IlpEvent *event);
int ip_lock_take(uint32_t track,IlpSnapshot *out);
void instrument_lock_discard(void);
static int code_index(uint32_t code)
{
    const uint8_t codes[ILP_LANES]={0xcb,0xcc,0xce,0xcf,0xd0};
    for(unsigned i=0;i<ILP_LANES;++i) if(codes[i]==code) return (int)i;
    return -1;
}
static int32_t get(void *ctx,uint32_t code,uint32_t step)
{ (void)ctx; assert(step==4 && code_index(code)>=0); return stored[code_index(code)]; }
static int set(void *ctx,uint32_t code,uint32_t step,uint32_t value)
{ (void)ctx; assert(step==4 && code_index(code)>=0); ++writes; if(fail_write) return 0; stored[code_index(code)]=(int32_t)value; return 1; }
static int clear(void *ctx,uint32_t code,uint32_t step)
{ (void)ctx; assert(step==4 && code_index(code)>=0); ++writes; if(fail_write) return 0; stored[code_index(code)]=-1; return 1; }
static int32_t grid[128][ILP_LANES];
static unsigned grid_writes,grid_fail_at;
static int32_t grid_get(void *ctx,uint32_t code,uint32_t step)
{ (void)ctx; assert(step<128 && code_index(code)>=0); return grid[step][code_index(code)]; }
static int grid_set(void *ctx,uint32_t code,uint32_t step,uint32_t value)
{ (void)ctx; if(++grid_writes==grid_fail_at) return 0; grid[step][code_index(code)]=(int32_t)value; return 1; }
static int grid_clear(void *ctx,uint32_t code,uint32_t step)
{ (void)ctx; if(++grid_writes==grid_fail_at) return 0; grid[step][code_index(code)]=-1; return 1; }

int main(void)
{
    uint16_t encoded;
    uint32_t control,value;
    const int max[ILP_ID_LIMIT]={12,3,127,127,500,127,8,4,6,100,75,2,0,0,127,127,
        4,700,100,1000,2,2,1,200,127,127,200,200,100,15,200,200,
        -1,-1,7,2,300,-1,-1,-1,127,3,100,15,15,100,127,127,100,127,200,200,200,
        96,96,96,-1,-1,-1,-1,-1,-1};
    /* Every valid control/value pair round-trips, including the long WT
     * ranges and the separate FM bank. Stock UI locks never match the tag. */
    for(uint32_t id=0;id<ILP_ID_LIMIT;++id) {
        for(uint32_t v=0;max[id]>=0 && v<=(uint32_t)max[id];++v) {
            assert(ilp_pack(id,v,&encoded));
            assert(encoded<=0x7fff && (encoded&3)==1);
            assert(ilp_unpack(encoded,&control,&value));
            assert(control==id && value==v);
        }
        if(max[id]>=0) assert(!ilp_pack(id,(uint32_t)max[id]+1,&encoded));
        else assert(!ilp_pack(id,0,&encoded));
    }
    for(unsigned i=0;i<128;++i) assert(!ilp_unpack((int32_t)(i<<8),&control,&value));
    for(uint32_t family=0;family<4;++family) for(uint32_t c=0;c<37;++c) {
        uint32_t id=ilp_id_for(family,c);
        if(id!=UINT32_MAX) assert(ilp_control_for(family,id)==c);
    }
    assert(ilp_id_for(2,17)==17 && ilp_id_for(3,17)==42);
    assert(ilp_id_for(0,2)==2 && ilp_id_for(3,2)==ILP_FM_DEPTH);
    assert(ilp_control_for(3,17)==UINT32_MAX && ilp_control_for(2,42)==UINT32_MAX);
    assert(ilp_id_for(0,17)==UINT32_MAX && ilp_id_for(4,0)==UINT32_MAX);
    assert(ilp_pack(ILP_RATE,1000,&encoded));
    encoded=0x1234;
    assert(!ilp_pack(ILP_ID_LIMIT,0,&encoded) && encoded==0x1234);
    assert(!ilp_pack(2,128,&encoded) && encoded==0x1234);
    assert(!ilp_pack(ILP_RATE,1001,&encoded) && encoded==0x1234);
    assert(!ilp_pack(2,0,0));
    control=123; value=456;
    assert(!ilp_unpack(-1,&control,&value) && control==123 && value==456);
    assert(!ilp_unpack(0x8000,&control,&value) && control==123 && value==456);
    assert(!ilp_unpack(0,0,&value) && !ilp_unpack(0,&control,0));
    int32_t slots[ILP_LANES]={-1,-1,-1,-1,-1};
    assert(ilp_edit(slots,17,700,0)==1);
    assert(ilp_edit(slots,19,1000,0)==1);
    assert(ilp_edit(slots,2,127,0)==1);
    assert(ilp_edit(slots,10,75,0)==1);
    assert(ilp_edit(slots,50,200,0)==1);
    int32_t saved[ILP_LANES];
    for(unsigned i=0;i<ILP_LANES;++i) saved[i]=slots[i];
    assert(ilp_edit(slots,3,10,0)==ILP_FULL);
    for(unsigned i=0;i<ILP_LANES;++i) assert(slots[i]==saved[i]);
    assert(ilp_edit(slots,19,512,0)==1 && ilp_unpack(slots[1],&control,&value));
    assert(control==19 && value==512);
    assert(ilp_edit(slots,17,0,1)==1 && slots[0]==-1);
    assert(ilp_edit(slots,17,0,1)==0);
    assert(ilp_edit(slots,3,10,0)==1 && ilp_unpack(slots[0],&control,&value));
    assert(control==3 && value==10);
    slots[4]=slots[0];
    for(unsigned i=0;i<ILP_LANES;++i) saved[i]=slots[i];
    assert(ilp_edit(slots,3,11,0)==ILP_DUPLICATE);
    for(unsigned i=0;i<ILP_LANES;++i) assert(slots[i]==saved[i]);
    slots[4]=0x8000;
    assert(ilp_edit(slots,3,11,0)==ILP_INVALID && slots[4]==0x8000);
    assert(ilp_edit(slots,32,0,1)==ILP_INVALID);
    assert(ilp_edit(0,3,11,0)==ILP_INVALID);
    IlpStorage store={0,get,set,clear};
    assert(ilp_store_edit(&store,4,17,700,0)==1 && writes==1);
    assert(ilp_store_edit(&store,4,17,700,0)==1 && writes==1);
    assert(ilp_store_edit(&store,4,19,1000,0)==1 && writes==2);
    stored[3]=0x1400; /* an existing stock lock is never overwritten */
    assert(ilp_store_edit(&store,4,2,5,0)==ILP_INVALID && writes==2 && stored[3]==0x1400);
    stored[3]=-1; fail_write=1;
    assert(ilp_store_edit(&store,4,2,5,0)==ILP_STORAGE_FULL && stored[2]==-1);
    fail_write=0;
    assert(ilp_store_edit(&store,4,17,0,1)==1 && stored[0]==-1);
    assert(ilp_store_edit(&store,128,17,0,1)==ILP_INVALID);
    IlpStorage grid_store={0,grid_get,grid_set,grid_clear};
    for(unsigned s=0;s<128;++s) for(unsigned i=0;i<ILP_LANES;++i) grid[s][i]=-1;
    const uint32_t selected[4]={(1u<<4)|(1u<<5),0,0,0};
    grid[5][0]=0x1400; /* preflight rejects a foreign lane before any write */
    assert(ilp_store_edit_steps(&grid_store,selected,2,42,0)==ILP_INVALID);
    assert(grid_writes==0 && grid[4][0]==-1 && grid[5][0]==0x1400);
    grid[5][0]=-1; grid_fail_at=2;
    assert(ilp_store_edit_steps(&grid_store,selected,2,42,0)==ILP_STORAGE_FULL);
    assert(grid[4][0]==-1 && grid[5][0]==-1); /* first write rolled back */
    grid_fail_at=0;
    assert(ilp_store_edit_steps(&grid_store,selected,2,42,0)==1);
    assert(grid[4][0]==grid[5][0] && grid[4][0]>=0);
    assert(ilp_store_edit_steps(&grid_store,selected,2,0,1)==1);
    assert(grid[4][0]==-1 && grid[5][0]==-1);
    assert(offsetof(IlpEvent,count)==8 && offsetof(IlpEvent,entry)==20 && sizeof(IlpEventEntry)==8);
    IlpEvent event={.count=4}; IlpSnapshot snapshot={.count=99};
    assert(ilp_pack(17,700,&encoded));
    event.entry[0]=(IlpEventEntry){34,0x6400,0x12345678};
    event.entry[1]=(IlpEventEntry){26,(int16_t)encoded,11};
    event.entry[2]=(IlpEventEntry){27,0x1400,22}; /* untouched stock lock */
    assert(ilp_pack(50,200,&encoded));
    event.entry[3]=(IlpEventEntry){31,(int16_t)encoded,33};
    assert(ilp_filter_event(&event,0,&snapshot)==0 && event.count==4 && snapshot.count==99);
    assert(ilp_filter_event(&event,1,&snapshot)==1 && event.count==2 && snapshot.count==2);
    assert(snapshot.pair[0].id==17 && snapshot.pair[0].value==700);
    assert(snapshot.pair[1].id==50 && snapshot.pair[1].value==200);
    assert(event.entry[0].index==34 && event.entry[0].rest==0x12345678);
    assert(event.entry[1].index==27 && event.entry[1].value==0x1400 && event.entry[1].rest==22);
    assert(ilp_pack(2,42,&encoded));
    event.count=2; event.entry[0]=(IlpEventEntry){26,(int16_t)encoded,0};
    event.entry[1]=(IlpEventEntry){27,(int16_t)encoded,0};
    snapshot.count=99;
    assert(ilp_filter_event(&event,1,&snapshot)==-2 && event.count==2 && snapshot.count==99);
    event.count=72;
    assert(ilp_filter_event(&event,1,&snapshot)==-1 && event.count==72 && snapshot.count==99);
    assert(ilp_filter_event(0,1,&snapshot)==-1 && ilp_filter_event(&event,1,0)==-1);
    uint32_t base[32]={11,2},applied[32]={0};
    base[18]=42; base[2]=42; base[19]=10u<<16; base[22]=1u<<16;
    snapshot.count=3;
    snapshot.pair[0]=(IlpPair){2,100};
    snapshot.pair[1]=(IlpPair){19,1000};
    snapshot.pair[2]=(IlpPair){4,101};
    assert(ilp_apply(base,&snapshot,applied)==1);
    assert(base[18]==42 && base[19]==(10u<<16) && base[2]==42);
    assert((applied[18]&65535)==100 && applied[2]==100);
    assert((applied[19]>>16)==7 && (applied[20]&65535)==101 && applied[15]==151);
    snapshot.count=1; snapshot.pair[0]=(IlpPair){42,88}; /* inactive FM balance */
    assert(ilp_apply(base,&snapshot,applied)==1 && applied[17]==base[17]);
    base[1]=3;
    assert(ilp_apply(base,&snapshot,applied)==1 && (applied[17]>>16)==88);
    snapshot.pair[0]=(IlpPair){34,2};
    applied[0]=0x12345678;
    assert(ilp_apply(base,&snapshot,applied)==ILP_APPLY_UNSUPPORTED && applied[0]==0x12345678);
    event.count=1; assert(ilp_pack(17,700,&encoded));
    event.entry[0]=(IlpEventEntry){26,(int16_t)encoded,0};
    ip_lock_enabled=0; ip_lock_capture(1,&event); /* disabled path is stock */
    assert(event.count==1 && !ip_lock_take(1,&snapshot));
    ip_lock_enabled=1;
    ip_lock_capture(2,&event); /* unadmitted track remains completely stock */
    assert(event.count==1 && !ip_lock_take(2,&snapshot));
    route_ready=0; ip_lock_capture(1,&event); /* incomplete DSP route is untouched */
    assert(event.count==1 && !ip_lock_take(1,&snapshot));
    route_ready=1;
    ip_lock_capture(1,&event);
    assert(event.count==0 && ip_lock_take(1,&snapshot));
    assert(snapshot.count==1 && snapshot.pair[0].id==17 && snapshot.pair[0].value==700);
    assert(!ip_lock_take(1,&snapshot));
    event.count=0; ip_lock_capture(1,&event); /* unlocked trig clears the overlay */
    assert(ip_lock_take(1,&snapshot) && snapshot.count==0);
    event.count=1; event.entry[0]=(IlpEventEntry){26,(int16_t)encoded,0};
    ip_lock_capture(1,&event);
    instrument_lock_discard(); /* a due lock-only event cannot poison a later note */
    assert(!ip_lock_take(1,&snapshot));
    event.count=1; event.entry[0]=(IlpEventEntry){26,(int16_t)encoded,0};
    ip_lock_capture(1,&event);
    ip_lock_capture(1,0); /* the later unlocked event wins in the same block */
    assert(ip_lock_take(1,&snapshot) && snapshot.count==0);
    event.count=72; ip_lock_capture(1,&event);
    assert(ip_lock_capture_error==1 && !ip_lock_take(1,&snapshot) && !masked);
    admitted_tracks|=1u<<2;
    event.count=1; event.entry[0]=(IlpEventEntry){26,(int16_t)encoded,0};
    ip_lock_capture(1,&event);
    IlpEvent other={.count=1}; uint16_t other_packed;
    assert(ilp_pack(2,42,&other_packed));
    other.entry[0]=(IlpEventEntry){27,(int16_t)other_packed,0};
    ip_lock_capture(2,&other);
    assert(ip_lock_take(2,&snapshot) && snapshot.count==1 && snapshot.pair[0].value==42);
    assert(ip_lock_take(1,&snapshot) && snapshot.count==1 && snapshot.pair[0].value==700);

    /* Two rapid trigs must keep their own snapshots across note-off batching
     * and packet retries; an unlocked trig explicitly retires prior locks. */
    MiLedger ledger; MiTransport transport; uint32_t revisions[MI_TRACKS]={0};
    revisions[1]=1; mi_init(&ledger); mt_init(&transport);
    assert(mi_route(&ledger,MI_PANEL,0,1,1,0)==0);
    assert(mi_seq_trig(&ledger,1,60,100));
    uint16_t pair[2]={(uint16_t)((17u<<10)|700u),(uint16_t)((19u<<10)|1000u)};
    assert(mi_seq_attach_locks(&ledger,1,pair,2));
    assert(mi_seq_trig(&ledger,1,62,100));
    assert(mi_seq_attach_locks(&ledger,1,0,0));
    const uint32_t *packet=mt_prepare(&transport,&ledger,revisions,0);
    assert(packet && (packet[1]&MT_LOCKED) && transport.lock_valid && transport.lock_count==2);
    assert(transport.locks[0]==pair[0] && transport.locks[1]==pair[1]);
    uint32_t side[ML_WORDS],n=99,source_valid=99; uint16_t decoded[MI_LOCKS]={0},source_out[MI_SOURCE_VALUES]={0};
    assert(ml_encode(&transport,packet,side));
    assert(ml_decode(side,packet,decoded,&n,source_out,&source_valid) && n==2 && !source_valid && decoded[0]==pair[0] && decoded[1]==pair[1]);
    side[5]^=1; n=99;
    assert(!ml_decode(side,packet,decoded,&n,source_out,&source_valid) && n==99);
    side[5]^=1;
    uint32_t wrong[MT_WORDS]; for(unsigned i=0;i<MT_WORDS;++i) wrong[i]=packet[i];
    ++wrong[3];
    assert(!ml_decode(side,wrong,decoded,&n,source_out,&source_valid) && n==99);
    assert(!ml_encode(&transport,wrong,side));
    uint32_t message=packet[3];
    assert(mt_prepare(&transport,&ledger,revisions,1)[3]==message && transport.locks[1]==pair[1]);
    assert(mt_ack(&transport,&ledger,ledger.epoch,message));
    packet=mt_prepare(&transport,&ledger,revisions,2);
    assert(packet && !(packet[1]&MT_LOCKED) && !transport.lock_valid); /* previous note-off */
    assert(mt_ack(&transport,&ledger,ledger.epoch,packet[3]));
    packet=mt_prepare(&transport,&ledger,revisions,3);
    assert(packet && transport.lock_valid && transport.lock_count==0);
    assert(mt_ack(&transport,&ledger,ledger.epoch,packet[3]) && !ledger.count);
    mi_init(&ledger); mt_init(&transport);
    assert(mi_route(&ledger,MI_PANEL,0,1,1,0)==0 && mi_seq_trig(&ledger,1,60,100));
    uint16_t wave=(uint16_t)((34u<<10)|7u),source[MI_SOURCE_VALUES];
    for(unsigned i=0;i<MI_SOURCE_VALUES;++i) source[i]=(uint16_t)(10+i);
    assert(mi_seq_attach_locks(&ledger,1,&wave,1));
    assert(mi_seq_attach_source(&ledger,1,source));
    packet=mt_prepare(&transport,&ledger,revisions,0);
    assert(packet && ml_encode(&transport,packet,side));
    assert(ml_decode(side,packet,decoded,&n,source_out,&source_valid) && source_valid && n==1);
    for(unsigned i=0;i<MI_SOURCE_VALUES;++i) assert(source_out[i]==source[i]);

    KbState keyboard; MiEvent input={0},output[8];
    kb_init(&keyboard); kb_reset(&keyboard,1,1);
    input.epoch=1; input.generation=1; input.token=1; input.kind=MI_NOTE_ON;
    input.port=MI_PANEL; input.note=60; input.value=100;
    input.lock_valid=1; input.lock_count=2;
    input.locks[0]=1; /* ROOT major: 0 is PLAY, 1 is the first chord */
    input.locks[1]=(uint16_t)((1u<<10)|1u); /* first inversion */
    assert(kb_process_locked(&keyboard,&input,1,output)==3);
    assert(output[0].note==60 && output[1].note==76 && output[2].note==67);
    assert(keyboard.mode==KB_PLAY && keyboard.type==0 && keyboard.voicing==0);
    input.kind=MI_NOTE_OFF; input.lock_valid=0;
    assert(kb_process_locked(&keyboard,&input,1,output)==3);
    input.kind=MI_NOTE_ON; input.token=2; input.note=62;
    assert(kb_process_locked(&keyboard,&input,1,output)==1 && output[0].note==62);
    assert(keyboard.mode==KB_PLAY && keyboard.voicing==0);

    /* A recorded chord: the trig's note plus chord-note locks (IDs 53..55,
     * interval + 48). It plays exactly those notes, even when the Sound is a
     * ROOT chord, and releases them together. */
    kb_init(&keyboard); kb_reset(&keyboard,1,1);
    keyboard.mode=KB_ROOT; keyboard.type=4;
    input=(MiEvent){0};
    input.epoch=1; input.generation=1; input.token=10; input.kind=MI_NOTE_ON;
    input.port=MI_PANEL; input.note=67; input.value=100;
    input.lock_valid=1; input.lock_count=3;
    input.locks[0]=(uint16_t)((53u<<10)|(48u-7u)); /* C below G */
    input.locks[1]=(uint16_t)((2u<<10)|84u);       /* COLOR, for the DSP */
    input.locks[2]=(uint16_t)((54u<<10)|(48u-3u)); /* E */
    assert(kb_process_locked(&keyboard,&input,1,output)==3);
    assert(output[0].note==67 && output[1].note==60 && output[2].note==64);
    for(unsigned i=0;i<3;++i) assert(output[i].kind==MI_NOTE_ON && output[i].value==100);
    assert(keyboard.mode==KB_ROOT && keyboard.type==4 && !keyboard.stage_count);
    input.kind=MI_NOTE_OFF; input.lock_valid=0; input.lock_count=0;
    assert(kb_process_locked(&keyboard,&input,1,output)==3);
    for(unsigned i=0;i<3;++i) assert(output[i].kind==MI_NOTE_OFF);
    /* An unlocked trig in the same Sound still plays its ROOT chord. */
    input.kind=MI_NOTE_ON; input.token=11; input.note=60;
    assert(kb_process_locked(&keyboard,&input,1,output)==3);
    assert(output[0].note==60 && output[1].note==63 && output[2].note==67);
    input.kind=MI_NOTE_OFF;
    assert(kb_process_locked(&keyboard,&input,1,output)==3);
    /* Unison and out-of-range notes are dropped; a duplicate ID or a value
     * past +48 rejects the event. */
    keyboard.mode=KB_PLAY;
    input.kind=MI_NOTE_ON; input.token=12; input.note=120; input.lock_valid=1; input.lock_count=3;
    input.locks[0]=(uint16_t)((53u<<10)|48u);
    input.locks[1]=(uint16_t)((54u<<10)|(48u+12u));
    input.locks[2]=(uint16_t)((55u<<10)|(48u-5u));
    assert(kb_process_locked(&keyboard,&input,1,output)==2);
    assert(output[0].note==120 && output[1].note==115);
    input.kind=MI_NOTE_OFF; input.lock_valid=0;
    assert(kb_process_locked(&keyboard,&input,1,output)==2);
    input.kind=MI_NOTE_ON; input.token=13; input.note=60; input.lock_valid=1; input.lock_count=2;
    input.locks[0]=(uint16_t)((53u<<10)|52u); input.locks[1]=(uint16_t)((53u<<10)|55u);
    assert(kb_process_locked(&keyboard,&input,1,output)==-1);
    input.lock_count=1; input.locks[0]=(uint16_t)((55u<<10)|97u);
    assert(kb_process_locked(&keyboard,&input,1,output)==-1);

    /* The DSP sideband carries only DSP locks: chord-note IDs stay on the CF. */
    mi_init(&ledger); mt_init(&transport);
    assert(mi_route(&ledger,MI_PANEL,0,1,1,0)==0 && mi_seq_trig(&ledger,1,67,100));
    uint16_t mixed[3]={(uint16_t)((53u<<10)|41u),(uint16_t)((2u<<10)|84u),(uint16_t)((54u<<10)|45u)};
    assert(mi_seq_attach_locks(&ledger,1,mixed,3));
    packet=mt_prepare(&transport,&ledger,revisions,0);
    assert(packet && transport.lock_valid && transport.lock_count==1 && transport.locks[0]==mixed[1]);
    assert(ml_encode(&transport,packet,side) && ml_decode(side,packet,decoded,&n,source_out,&source_valid));
    assert(n==1 && decoded[0]==mixed[1]);
    return 0;
}
