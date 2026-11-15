#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-share-pool.h"

static void make(uint8_t out[SC_BYTES],uint32_t value)
{
    for(uint32_t n=0;n<SC_BYTES;++n) out[n]=(uint8_t)(value+(n*7u));
}
static void is_snapshot(SsPool *pool,uint16_t handle,const uint8_t *expected)
{
    uint8_t actual[SC_BYTES];
    assert(!ss_read(pool,handle,actual));
    assert(!memcmp(actual,expected,SC_BYTES));
}

int main(void)
{
    SsPool pool; SsEntry storage[4]; uint8_t before[SC_BYTES],after[SC_BYTES],out[SC_BYTES];
    uint16_t source=0,copy=0,second=0,r1=0,r2=0,r3=0,r4=0;
    const uintptr_t a=0x1000,b=0x2000,c=0x3000;
    memset(storage,0x5a,sizeof(storage)); make(before,1); make(after,2);
    assert(ss_init(&pool,storage,0)<0);
    assert(ss_init(&pool,storage,SS_MAX_HANDLES+1u)<0);
    assert(!ss_init(&pool,storage,4));
    memset(out,0x6b,sizeof(out));
    assert(ss_read(&pool,1,out)<0 && out[0]==0x6b && out[SC_BYTES-1]==0x6b);
    assert(!ss_reserve(&pool,&r1) && r1==1);
    assert(ss_read(&pool,r1,out)<0 && out[0]==0x6b);
    assert(ss_create(&pool,r1,0,&source,before)<0 && !source);
    assert(!ss_create(&pool,r1,a,&source,before) && source==r1);
    assert(ss_create(&pool,r1,b,&copy,before)<0 && !copy);
    is_snapshot(&pool,source,before);

    /* Large numbers of ordinary edits use the existing exclusive handle,
     * need no reservation, and do not consume the finite handle space. */
    for(uint32_t n=0;n<100000;++n) {
        make(after,n);
        assert(!ss_edit(&pool,&source,a,r1,0,after));
    }
    assert(pool.count==1 && source==r1);
    is_snapshot(&pool,source,after);
    assert(ss_edit(&pool,&source,b,r1,0,before)<0);
    assert(ss_edit(&pool,&source,a,r1+1,0,before)<0);
    is_snapshot(&pool,source,after);
    assert(!ss_reserve(&pool,&r2) && r2==2);
    assert(!ss_edit(&pool,&source,a,r1,r2,after));
    assert(storage[r2-1].state==SS_RESERVED && source==r1);

    /* A copy must freeze before copying its handle. The two copies initially
     * observe exactly the same revision, including after a failed detach. */
    assert(ss_share(&pool,r1,b)<0);
    assert(!ss_share(&pool,r1,a));
    copy=source;
    assert(!ss_share(&pool,copy,b));
    assert(ss_edit(&pool,&source,a,r1,0,before)<0);
    assert(source==r1 && copy==r1);
    is_snapshot(&pool,copy,after);
    assert(ss_edit(&pool,&source,a,r1+1,r2,before)<0);
    assert(storage[r2-1].state==SS_RESERVED && source==r1);
    assert(!ss_edit(&pool,&source,a,r1,r2,before));
    assert(source==r2 && copy==r1);
    is_snapshot(&pool,source,before);
    is_snapshot(&pool,copy,after);
    assert(ss_edit(&pool,&copy,b,r1,r2,before)<0);
    is_snapshot(&pool,copy,after);

    /* Copies of a frozen revision can detach independently. */
    second=copy;
    assert(!ss_reserve(&pool,&r3) && r3==3);
    assert(!ss_edit(&pool,&copy,b,r1,r3,before));
    assert(copy==r3 && second==r1);
    is_snapshot(&pool,second,after);
    assert(!ss_share(&pool,source,a));
    assert(!ss_reserve(&pool,&r4) && r4==4);
    assert(!ss_edit(&pool,&source,a,r2,r4,after));
    is_snapshot(&pool,r2,before);
    is_snapshot(&pool,source,after);
    uint16_t untouched=123;
    assert(ss_reserve(&pool,&untouched)<0 && untouched==123);
    make(before,77);
    assert(ss_edit(&pool,&second,c,r1,0,before)<0 && second==r1);
    is_snapshot(&pool,second,after);
    /* Pool exhaustion still permits edits of an exclusive Sound. */
    assert(!ss_edit(&pool,&source,a,r4,0,before));
    is_snapshot(&pool,source,before);
    is_snapshot(&pool,second,after);
    return 0;
}
