#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compact-pool.h"

static int allocations_left=-1;
static unsigned allocated,freed;
static void *allocate(uint32_t bytes,void *unused)
{
    (void)unused;
    assert(bytes==4085);
    if(!allocations_left) return 0;
    if(allocations_left>0) --allocations_left;
    ++allocated; return malloc(bytes);
}
static void release(void *p,void *unused) { (void)unused; ++freed; free(p); }
static void make(uint8_t *bytes,uint32_t n)
{
    for(unsigned i=0;i<CRP_BYTES;++i) bytes[i]=(uint8_t)(n+i*7u);
    bytes[0]=(uint8_t)n; bytes[1]=(uint8_t)(n>>8);
}
static void free_pool(CrpPool *pool)
{ for(unsigned i=0;i<CRP_SLABS;++i) free(pool->slab[i]); }
int main(void)
{
    CrpPool pool; uint16_t first=0,old=0,intervening=0;
    uint8_t bytes[CRP_BYTES],out[CRP_BYTES],unchanged[CRP_BYTES];
    assert(sizeof(CrpEntry)==43 && CRP_SLABS==690);
    crp_init(&pool,allocate,release,0); make(bytes,0x1234);
    assert(!crp_reserve(&pool,1,&old) && old==1);
    assert(!crp_publish(&pool,old,bytes));
    allocations_left=1; freed=0; first=99;
    assert(crp_reserve(&pool,2192,&first));
    assert(first==99 && pool.count==1 && freed==1);
    assert(!crp_read(&pool,old,out) && !memcmp(out,bytes,CRP_BYTES));
    allocations_left=-1; unsigned start=allocated;
    assert(!crp_reserve(&pool,2192,&first) && first==2);
    assert(allocated-start==23); /* the first slab had 94 free entries */
    memset(out,0x5a,sizeof(out)); memcpy(unchanged,out,sizeof(out));
    assert(crp_read(&pool,first,out) && !memcmp(out,unchanged,sizeof(out)));
    allocations_left=0; start=allocated;
    for(uint32_t i=0;i<2192;++i) {
        make(bytes,i); assert(!crp_publish(&pool,(uint16_t)(first+i),bytes));
        assert(!crp_read(&pool,(uint16_t)(first+i),out) && !memcmp(bytes,out,CRP_BYTES));
    }
    assert(allocated==start);
    make(bytes,17); assert(crp_publish(&pool,first,bytes));
    make(bytes,0); assert(!crp_read(&pool,first,out) && !memcmp(bytes,out,CRP_BYTES));
    assert(crp_read(&pool,0,out) && crp_read(&pool,0xfffe,out) && crp_read(&pool,0xffff,out));
    allocations_left=-1;
    assert(!crp_reserve(&pool,4,&first));
    make(bytes,8); assert(!crp_publish(&pool,first,bytes));
    uint32_t count=pool.count;
    assert(crp_cancel_tail(&pool,first,4) && pool.count==count);
    assert(!crp_reserve(&pool,1,&intervening));
    assert(crp_cancel_tail(&pool,(uint16_t)(first+1),3));
    assert(!crp_cancel_tail(&pool,intervening,1));
    assert(!crp_cancel_tail(&pool,(uint16_t)(first+1),3));
    assert(pool.count==first && !crp_read(&pool,first,out) && !memcmp(bytes,out,CRP_BYTES));
    free_pool(&pool);
    CrpPool full; crp_init(&full,allocate,release,0);
    assert(!crp_reserve(&full,CRP_MAX_HANDLES,&first) && first==1);
    make(bytes,99); assert(!crp_publish(&full,1,bytes));
    first=77; assert(crp_reserve(&full,1,&first));
    assert(first==77 && full.count==CRP_MAX_HANDLES);
    assert(!crp_read(&full,1,out) && !memcmp(bytes,out,CRP_BYTES));
    free_pool(&full);

    /* Without a deallocator, partial slabs remain available for retry but
     * the failed reservation still consumes no handles. */
    CrpPool retry; crp_init(&retry,allocate,0,0);
    allocations_left=1; first=77;
    assert(crp_reserve(&retry,2192,&first));
    assert(first==77 && !retry.count && retry.slab[0]);
    CrpEntry *retained=retry.slab[0];
    allocations_left=0; start=allocated;
    assert(!crp_reserve(&retry,1,&first) && first==1);
    assert(allocated==start && retry.slab[0]==retained);
    make(bytes,3); assert(!crp_publish(&retry,first,bytes));
    assert(!crp_read(&retry,first,out) && !memcmp(bytes,out,CRP_BYTES));
    free_pool(&retry);
    return 0;
}
