/* Host tests of the CANOPY record extension and the Sound association store. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-assoc.h"

static uint32_t random_state=7;
static uint32_t next(void) { random_state=random_state*1664525u+1013904223u; return random_state; }

static CpPreset base_preset(void)
{
    CpPreset p;
    memset(&p,0,sizeof(p));
    p.bpm=CP_ARP_BPM_RESERVED; p.clock=CP_ARP_CLOCK;
    cp_wavetable_defaults(p.control); cp_fm_defaults(p.fm);
    p.control[CP_RATE]=4; p.control[CP_OCTAVES]=3; p.control[CP_GATE]=50;
    return p;
}
static void same_preset(const CpPreset *a,const CpPreset *b)
{
    uint8_t x[CP_RECORD_BYTES],y[CP_RECORD_BYTES];
    assert(!cp_encode(a,x) && !cp_encode(b,y));
    assert(!memcmp(x,y,sizeof(x)));
}

static void test_record(void)
{
    static const uint16_t high[32]={11,3,127,127,500,127,8,4,6,100,75,2,0,0,127,127,
        4,700,100,1000,2,2,1,200,127,127,200,200,100,15,200,200};
    static const uint16_t fm_high[13]={127,3,100,15,15,100,127,127,100,127,200,200,200};
    CpPreset p=base_preset(),got;
    uint8_t b[SR_BYTES];
    /* Positions: never 0 (a saved parameter) or 32 (the last load into
     * parameter 0), only zero-filled padding. */
    for(uint32_t i=0;i<SR_SLOTS;++i) {
        uint32_t position=(sr_offset(i)-0x1c)/2;
        assert(position!=0 && position!=32);
        assert((position<=28 && position%4==0) || (position>=77 && position<=89));
        for(uint32_t j=0;j<i;++j) assert(sr_offset(j)!=sr_offset(i));
    }
    assert(!sr_encode(&p,b) && sr_present(b) && !sr_decode(&got,b));
    same_preset(&p,&got);
    for(uint32_t bit=0;bit<SR_BYTES*8;++bit) {
        b[bit/8]^=(uint8_t)(1u<<(bit%8));
        CpPreset before; memset(&before,0x5a,sizeof(before)); memcpy(&got,&before,sizeof(got));
        assert(sr_decode(&got,b)<0 && !memcmp(&got,&before,sizeof(got)));
        b[bit/8]^=(uint8_t)(1u<<(bit%8));
    }
    for(uint32_t i=0;i<32;++i) {
        CpPreset q=p; q.control[i]=high[i];
        if(q.control[CP_WT_CLOCK] && q.control[CP_WT_RATE]>7) q.control[CP_WT_RATE]=7;
        assert(!sr_encode(&q,b) && !sr_decode(&got,b)); same_preset(&q,&got);
    }
    for(uint32_t i=0;i<13;++i) {
        CpPreset q=p; q.fm[i]=fm_high[i];
        assert(!sr_encode(&q,b) && !sr_decode(&got,b)); same_preset(&q,&got);
    }
    for(uint32_t n=0;n<400;++n) {
        CpPreset q=p;
        for(uint32_t i=0;i<32;++i) if(i!=CP_RESERVED12 && i!=CP_SOURCE) q.control[i]=(uint16_t)(next()%(high[i]+1u));
        for(uint32_t i=0;i<13;++i) q.fm[i]=(uint16_t)(next()%(fm_high[i]+1u));
        if(!q.control[CP_RATE]) q.control[CP_RATE]=1;
        if(!q.control[CP_GATE]) q.control[CP_GATE]=1;
        if(!q.control[CP_WT_RATE]) q.control[CP_WT_RATE]=1;
        if(q.control[CP_WT_CLOCK] && q.control[CP_WT_RATE]>7) q.control[CP_WT_RATE]=7;
        q.family=(uint8_t)(next()%4); q.mode=(uint8_t)(next()%2);
        q.shape.count=(uint8_t)(next()%5);
        uint8_t at=0;
        for(uint32_t i=1;i<q.shape.count;++i) { at=(uint8_t)(at+1+next()%20); q.shape.intervals[i]=at; }
        assert(cp_valid(&q));
        assert(!sr_encode(&q,b) && !sr_decode(&got,b)); same_preset(&q,&got);
    }
    /* Stock records carry zeros: no extension. Other formats are carried
     * but never decoded. */
    memset(b,0,sizeof(b)); assert(!sr_present(b) && sr_decode(&got,b)<0);
    b[0]=0xc0; assert(!sr_present(b));
    b[0]=0xc2; assert(sr_present(b) && sr_decode(&got,b)<0);
    /* Scatter writes only the twenty halfwords, gather inverts it. */
    uint8_t record[0x455],before[0x455],back[SR_BYTES];
    for(uint32_t i=0;i<sizeof(record);++i) record[i]=(uint8_t)next();
    memcpy(before,record,sizeof(record));
    assert(!sr_encode(&p,b));
    sr_scatter(record,b);
    for(uint32_t i=0;i<sizeof(record);++i) {
        int slot=0;
        for(uint32_t n=0;n<SR_SLOTS;++n) if(i==sr_offset(n) || i==sr_offset(n)+1) slot=1;
        if(!slot) assert(record[i]==before[i]);
    }
    sr_gather(record,back);
    assert(!memcmp(back,b,sizeof(b)));
}

/* A fake address space: kit table, gap, pool, then scratch memory standing in
 * for heap objects and stack buffers. */
enum { KITS_AT=0x40, POOL_AT=KITS_AT+SA_KITS*SA_KIT+0x1000+2, HEAP_AT=POOL_AT+SA_POOL*SA_SOUND+0x100,
       SPACE=HEAP_AT+0x20000 };
static uint8_t *space;
static uintptr_t kit_sound(uint32_t k,uint32_t t) { return (uintptr_t)space+KITS_AT+k*SA_KIT+SA_KIT_FIRST+t*SA_SOUND; }
static uintptr_t pool_sound(uint32_t i) { return (uintptr_t)space+POOL_AT+i*SA_SOUND; }
static uintptr_t heap(uint32_t off) { return (uintptr_t)space+HEAP_AT+off; }
static void stock_copy(SaStore *s,uintptr_t dst,uintptr_t src,uint32_t len)
{
    memcpy((void *)dst,(const void *)src,len);
    sa_copied(s,dst,src,len);
}
static void stock_clear(SaStore *s,uintptr_t dst,uint32_t len)
{
    memset((void *)dst,0,len);
    sa_cleared(s,dst,len);
}
static void payload(uint8_t out[SR_BYTES],uint32_t tag)
{
    CpPreset p=base_preset();
    p.control[CP_CHORD]=(uint16_t)(tag%12); p.control[CP_STRUM]=(uint16_t)(tag%500);
    p.family=(uint8_t)(tag%4);
    assert(!sr_encode(&p,out));
}
static int has(SaStore *s,uintptr_t sound,uint32_t tag)
{
    uint8_t got[SR_BYTES],want[SR_BYTES];
    payload(want,tag);
    return sa_get(s,sound,got,0) && !memcmp(got,want,SR_BYTES);
}
static int none(SaStore *s,uintptr_t sound)
{
    uint8_t got[SR_BYTES];
    return !sa_get(s,sound,got,0);
}

static void test_store(void)
{
    space=malloc(SPACE);
    assert(space);
    for(uint32_t i=0;i<SPACE;++i) space[i]=(uint8_t)next();
    SaStore s;
    uint32_t bytes=SA_SLOT_BYTES+8*sizeof(SaTemp);
    void *memory=malloc(bytes);
    assert(sa_init(&s,(uintptr_t)space+KITS_AT,(uintptr_t)space+POOL_AT,memory,bytes)==8);
    uint8_t a[SR_BYTES],b[SR_BYTES],c[SR_BYTES];
    payload(a,1); payload(b,2); payload(c,3);

    /* Geometry: only exact Sound starts have slots. */
    assert(sa_slot(&s,kit_sound(0,0))==0 && sa_slot(&s,kit_sound(128,15))==SA_KITS*SA_TRACKS-1);
    assert(sa_slot(&s,pool_sound(0))==SA_KITS*SA_TRACKS && sa_slot(&s,pool_sound(127))==SA_SLOTS-1);
    assert(sa_slot(&s,kit_sound(3,4)+2)<0 && sa_slot(&s,kit_sound(3,0)-SA_KIT_FIRST)<0);
    assert(sa_slot(&s,kit_sound(3,15)+SA_SOUND)<0 && sa_slot(&s,pool_sound(0)+SA_SOUND*SA_POOL)<0);
    assert(sa_slot(&s,heap(0))<0);

    /* Loads, sequence numbers and owner publication. */
    sa_replaced(&s,kit_sound(3,5),a);
    uint32_t seq=0; assert(has(&s,kit_sound(3,5),1));
    uint8_t scratch[SR_BYTES];
    assert(sa_get(&s,kit_sound(3,5),scratch,&seq) && seq);
    uint32_t mine=sa_put(&s,kit_sound(3,5),b);
    assert(mine>seq && sa_seq(&s,kit_sound(3,5))==mine && has(&s,kit_sound(3,5),2));
    assert(!sa_put(&s,heap(0),b));
    sa_replaced(&s,kit_sound(3,5),a);
    assert(sa_seq(&s,kit_sound(3,5))!=mine && has(&s,kit_sound(3,5),1));
    uint8_t zero[SR_BYTES]={0};
    sa_replaced(&s,kit_sound(3,6),zero); assert(none(&s,kit_sound(3,6)));

    /* Direct live copy and a copy through a clipboard object. */
    stock_copy(&s,kit_sound(3,6),kit_sound(3,5),SA_SOUND);
    assert(has(&s,kit_sound(3,6),1));
    stock_copy(&s,heap(0x10),kit_sound(3,5),SA_SOUND);
    sa_replaced(&s,kit_sound(3,5),b);   /* the source changes after the copy */
    stock_copy(&s,kit_sound(0,0),heap(0x10),SA_SOUND);
    assert(has(&s,kit_sound(0,0),1));
    stock_copy(&s,pool_sound(9),heap(0x10),SA_SOUND);   /* the clipboard pastes twice */
    assert(has(&s,pool_sound(9),1));

    /* A rewritten or reused buffer never yields its old extension. */
    stock_copy(&s,heap(0x800),kit_sound(3,5),SA_SOUND);
    ((uint8_t *)heap(0x800))[0x20]^=0xff;
    stock_copy(&s,kit_sound(1,1),heap(0x800),SA_SOUND);
    assert(none(&s,kit_sound(1,1)) && s.stats[SA_STAT_TEMP_STALE]==1);
    /* An unknown source carries nothing; it clears the destination. */
    sa_replaced(&s,kit_sound(1,2),a);
    stock_copy(&s,kit_sound(1,2),heap(0x4000),SA_SOUND);
    assert(none(&s,kit_sound(1,2)));
    /* A Sound without an extension replaces a sealed temporary. */
    stock_copy(&s,heap(0x10),kit_sound(1,2),SA_SOUND);
    stock_copy(&s,kit_sound(1,3),heap(0x10),SA_SOUND);
    assert(none(&s,kit_sound(1,3)));

    /* Partial copies and clears are edits, not replacements. */
    sa_replaced(&s,kit_sound(2,0),a);
    stock_copy(&s,kit_sound(2,0)+0x14,kit_sound(2,1)+0x14,0x8e);
    stock_clear(&s,kit_sound(2,0)+0x14,0x8e);
    assert(has(&s,kit_sound(2,0),1));
    stock_clear(&s,kit_sound(2,0),SA_SOUND);
    assert(none(&s,kit_sound(2,0)));

    /* A whole kit to the pattern clipboard and back; then undo through a
     * second buffer. Every track follows, including tracks with none. */
    for(uint32_t t=0;t<SA_TRACKS;++t) {
        if(t%3) { uint8_t p[SR_BYTES]; payload(p,10+t); sa_replaced(&s,kit_sound(5,t),p); }
        else sa_replaced(&s,kit_sound(5,t),0);
        uint8_t q[SR_BYTES]; payload(q,40+t); sa_replaced(&s,kit_sound(7,t),q);
    }
    uint32_t init_bytes=SA_SLOT_BYTES+40*sizeof(SaTemp);
    void *bigger=malloc(init_bytes);
    SaStore k;
    assert(sa_init(&k,(uintptr_t)space+KITS_AT,(uintptr_t)space+POOL_AT,bigger,init_bytes)==40);
    for(uint32_t t=0;t<SA_TRACKS;++t) {
        if(t%3) { uint8_t p[SR_BYTES]; payload(p,10+t); sa_replaced(&k,kit_sound(5,t),p); }
        uint8_t q[SR_BYTES]; payload(q,40+t); sa_replaced(&k,kit_sound(7,t),q);
    }
    uintptr_t clip=heap(0x1000), undo=heap(0x8000);
    stock_copy(&k,clip,kit_sound(5,0)-SA_KIT_FIRST,SA_KIT);
    stock_copy(&k,undo,kit_sound(7,0)-SA_KIT_FIRST,SA_KIT);
    stock_copy(&k,kit_sound(7,0)-SA_KIT_FIRST,clip,SA_KIT);
    for(uint32_t t=0;t<SA_TRACKS;++t) assert(t%3 ? has(&k,kit_sound(7,t),10+t) : none(&k,kit_sound(7,t)));
    stock_copy(&k,kit_sound(7,0)-SA_KIT_FIRST,undo,SA_KIT);
    for(uint32_t t=0;t<SA_TRACKS;++t) assert(has(&k,kit_sound(7,t),40+t));

    /* A swap through two stack buffers exchanges both associations. */
    sa_replaced(&k,kit_sound(9,0),a); sa_replaced(&k,kit_sound(9,1),b);
    stock_copy(&k,heap(0x10000),kit_sound(9,0),SA_SOUND);
    stock_copy(&k,heap(0x10800),kit_sound(9,1),SA_SOUND);
    stock_copy(&k,kit_sound(9,1),heap(0x10000),SA_SOUND);
    stock_copy(&k,kit_sound(9,0),heap(0x10800),SA_SOUND);
    assert(has(&k,kit_sound(9,0),2) && has(&k,kit_sound(9,1),1));
    /* Temp to temp copies keep the seal; a clear removes temporaries. */
    stock_copy(&k,heap(0x12000),heap(0x10000),SA_SOUND);
    stock_copy(&k,pool_sound(1),heap(0x12000),SA_SOUND);
    assert(has(&k,pool_sound(1),1));
    stock_clear(&k,heap(0x12000),SA_SOUND);
    stock_copy(&k,pool_sound(2),heap(0x12000),SA_SOUND);
    assert(none(&k,pool_sound(2)));
    /* Pool to pool with a misaligned range: only whole Sounds move. */
    sa_replaced(&k,pool_sound(20),c);
    stock_copy(&k,pool_sound(30)-2,pool_sound(20)-2,SA_SOUND+4);
    assert(has(&k,pool_sound(30),3));
    stock_copy(&k,pool_sound(40)+2,pool_sound(20)+2,SA_SOUND);
    assert(none(&k,pool_sound(40)));

    /* A long copy in the gap between the kit table and the pool, pages away
     * from every temporary, is filtered without touching any association. */
    {
        uintptr_t gap=(uintptr_t)space+KITS_AT+SA_KITS*SA_KIT;
        uint32_t passed=k.stats[SA_STAT_PASSED], copies=k.stats[SA_STAT_COPIES];
        stock_copy(&k,gap+0x600,gap,SA_SOUND);
        if(!k.far) assert(k.stats[SA_STAT_PASSED]==passed+1 && k.stats[SA_STAT_COPIES]==copies);
        assert(has(&k,pool_sound(1),1) && has(&k,kit_sound(9,0),2));
    }

    /* Capacity: eviction drops the oldest temporaries only. */
    for(uint32_t n=0;n<12;++n) stock_copy(&s,heap(0x2000+n*SA_SOUND),kit_sound(3,5),SA_SOUND);
    assert(s.stats[SA_STAT_EVICTIONS]>0);
    assert(none(&s,heap(0x2000)) && has(&s,heap(0x2000+11*SA_SOUND),2));
    stock_copy(&s,kit_sound(4,4),heap(0x2000+11*SA_SOUND),SA_SOUND);
    assert(has(&s,kit_sound(4,4),2));
    /* A copy out of a full ring never evicts its own source. */
    stock_copy(&s,heap(0x6000),heap(0x2000+4*SA_SOUND),SA_SOUND);
    stock_copy(&s,pool_sound(3),heap(0x2000+4*SA_SOUND),SA_SOUND);
    assert(has(&s,pool_sound(3),2));

    /* Too little memory: everything is a no-op. */
    SaStore empty;
    assert(sa_init(&empty,(uintptr_t)space+KITS_AT,(uintptr_t)space+POOL_AT,memory,SA_SLOT_BYTES)==0);
    sa_replaced(&empty,kit_sound(0,0),a);
    assert(none(&empty,kit_sound(0,0)) && !sa_put(&empty,kit_sound(0,0),a));
    free(bigger); free(memory); free(space);
}

int main(void)
{
    test_record();
    test_store();
    return 0;
}
