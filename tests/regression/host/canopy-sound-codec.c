#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"

static const uint16_t high[32]={11,3,127,127,500,127,8,4,6,100,75,2,0,0,127,127,
    4,700,100,1000,2,2,1,200,127,127,200,200,100,15,200,200};
static const uint16_t low[32]={[CP_RATE]=1,[CP_GATE]=1,[CP_WT_RATE]=1};
static const uint16_t fm_high[13]={127,3,100,15,15,100,127,127,100,127,200,200,200};
static uint32_t random_state=1;
static uint32_t next(void) { random_state=random_state*1664525u+1013904223u; return random_state; }
static void record_roundtrip(const CpPreset *p)
{
    uint8_t record[0x455],before[0x455],expected[CP_RECORD_BYTES],actual[CP_RECORD_BYTES];
    for(uint32_t i=0;i<sizeof(record);++i) record[i]=(uint8_t)next();
    memcpy(before,record,sizeof(record));
    assert(!sc_sound_record_write(record,sizeof(record),p));
    for(uint32_t i=0;i<sizeof(record);++i) {
        uint32_t in_padding=0;
        for(uint32_t n=0;n<21;++n) {
            uint32_t slot=n<8 ? 4*(n+1) : 77+n-8;
            uint32_t pos=0x1c+2*slot;
            if(i==pos || i==pos+1) in_padding=1;
        }
        if(!in_padding) assert(record[i]==before[i]);
    }
    CpPreset got,unchanged;
    assert(!sc_sound_record_read(&got,record,sizeof(record)));
    assert(!cp_encode(p,expected) && !cp_encode(&got,actual));
    assert(!memcmp(expected,actual,sizeof(actual)));
    /* A corrupt or truncated record never publishes a partial configuration. */
    memset(&unchanged,0x5a,sizeof(unchanged));
    memcpy(&got,&unchanged,sizeof(got));
    record[0x1c+2*4]^=1;
    assert(sc_sound_record_read(&got,record,sizeof(record))<0);
    assert(!memcmp(&got,&unchanged,sizeof(got)));
    assert(sc_sound_record_read(&got,record,sizeof(record)-1)<0);
    assert(!memcmp(&got,&unchanged,sizeof(got)));
    memcpy(before,record,sizeof(record));
    assert(sc_sound_record_write(record,sizeof(record)-1,p)<0);
    assert(!memcmp(record,before,sizeof(record)));
}
static void roundtrip(const CpPreset *p)
{
    uint8_t packed[SC_BYTES],a[CP_RECORD_BYTES],b[CP_RECORD_BYTES];
    CpPreset got;
    assert(cp_valid(p));
    assert(!sc_encode(p,packed) && !sc_decode(&got,packed));
    assert(!cp_encode(p,a) && !cp_encode(&got,b));
    assert(!memcmp(a,b,sizeof(a)));
    static int checked_corruption;
    if(checked_corruption++) return;
    for(uint32_t bit=0;bit<SC_BYTES*8;++bit) {
        packed[bit/8]^=(uint8_t)(1u<<(bit%8));
        CpPreset before; memset(&before,0x5a,sizeof(before)); memcpy(&got,&before,sizeof(got));
        assert(sc_decode(&got,packed)<0 && !memcmp(&got,&before,sizeof(got)));
        packed[bit/8]^=(uint8_t)(1u<<(bit%8));
    }
}
int main(void)
{
    CpPreset p;
    memset(&p,0,sizeof(p));
    p.bpm=CP_ARP_BPM_RESERVED; p.clock=CP_ARP_CLOCK;
    cp_wavetable_defaults(p.control); cp_fm_defaults(p.fm);
    p.control[CP_RATE]=4; p.control[CP_OCTAVES]=3; p.control[CP_GATE]=50;
    roundtrip(&p);
    record_roundtrip(&p);
    for(uint32_t i=0;i<32;++i) for(uint32_t endpoint=0;endpoint<2;++endpoint) {
        CpPreset boundary=p;
        boundary.control[i]=endpoint ? high[i] : low[i];
        if(boundary.control[CP_WT_CLOCK] && boundary.control[CP_WT_RATE]>7) boundary.control[CP_WT_RATE]=7;
        roundtrip(&boundary);
    }
    for(uint32_t i=0;i<13;++i) {
        CpPreset boundary=p; boundary.fm[i]=fm_high[i]; roundtrip(&boundary);
        boundary.fm[i]=0; roundtrip(&boundary);
    }
    /* Exercise every representable endpoint and hundreds of independent
     * combinations of both remembered banks and a non-empty USER chord. */
    for(uint32_t sample=0;sample<500;++sample) {
        for(uint32_t i=0;i<32;++i)
            p.control[i]=(uint16_t)(low[i]+next()%(high[i]-low[i]+1u));
        if(p.control[CP_WT_CLOCK]) p.control[CP_WT_RATE]=(uint16_t)(1+next()%7);
        for(uint32_t i=0;i<13;++i) p.fm[i]=(uint16_t)(next()%(fm_high[i]+1u));
        p.family=(uint8_t)(sample%4); p.mode=(uint8_t)(sample%2);
        p.shape.count=4; p.shape.intervals[0]=0;
        p.shape.intervals[1]=(uint8_t)(1+sample%30);
        p.shape.intervals[2]=(uint8_t)(31+sample%30);
        p.shape.intervals[3]=(uint8_t)(61+sample%67);
        roundtrip(&p);
        record_roundtrip(&p);
    }
    uint8_t out[SC_BYTES]; memset(out,0x5a,sizeof(out));
    p.control[CP_GATE]=0;
    assert(sc_encode(&p,out)<0);
    for(uint32_t i=0;i<SC_BYTES;++i) assert(out[i]==0x5a);
    return 0;
}
