#include "src-lock-packed.h"

/* Stable wire IDs and dense ranges. IDs 16..31 are the WT bank, 40..52 the
 * separate FM bank; view choices WAVE/CLOCK/BPM are 34..36. Do not reorder or
 * change existing ranges after establishing the on-card format. */
typedef struct { uint16_t first, max; uint8_t id; } IlpRange;
static const IlpRange ranges[]={
    {0,12,0},{13,3,1},{17,127,2},{145,127,3},{273,500,4},{774,127,5},
    {902,8,6},{911,4,7},{916,6,8},{923,100,9},{1024,75,10},{1100,2,11},
    {1103,0,12},{1104,0,13},{1105,127,14},{1233,127,15},{1361,4,16},
    {1366,700,17},{2067,100,18},{2168,1000,19},{3169,2,20},{3172,2,21},
    {3175,1,22},{3177,200,23},{3378,127,24},{3506,127,25},{3634,200,26},
    {3835,200,27},{4036,100,28},{4137,15,29},{4153,200,30},{4354,200,31},
    {4555,7,34},{4563,2,35},{4566,300,36},{4867,127,40},{4995,3,41},
    {4999,100,42},{5100,15,43},{5116,15,44},{5132,100,45},{5233,127,46},
    {5361,127,47},{5489,100,48},{5590,127,49},{5718,200,50},{5919,200,51},
    {6120,200,52}
};
#define RANGE_COUNT (sizeof(ranges)/sizeof(ranges[0]))
uint32_t ilp_id_for(uint32_t family,uint32_t control)
{
    if(family>3) return UINT32_MAX;
    if(control==34 || control==35 || control==36) return control;
    if(control<16) return family==3 && control==2 ? ILP_FM_DEPTH : control;
    if(family==2 && control<32) return control;
    if(family==3 && control>=16 && control<28) return ILP_FM_FIRST+control-16;
    return UINT32_MAX;
}

uint32_t ilp_control_for(uint32_t family,uint32_t id)
{
    if(family>3) return UINT32_MAX;
    if(id==34 || id==35 || id==36) return id;
    if(id<16) return family==3 && id==2 ? UINT32_MAX : id;
    if(family==2 && id>=16 && id<32) return id;
    if(family==3 && id==ILP_FM_DEPTH) return 2;
    if(family==3 && id>=ILP_FM_FIRST && id<=ILP_FM_LAST) return 16+id-ILP_FM_FIRST;
    return UINT32_MAX;
}

int ilp_pack(uint32_t id,uint32_t value,uint16_t *packed)
{
    if(!packed) return 0;
    for(uint32_t i=0;i<RANGE_COUNT;++i) if(ranges[i].id==id) {
        if(value>ranges[i].max) return 0;
        *packed=(uint16_t)(((ranges[i].first+value)<<2)|1u);
        return 1;
    }
    return 0;
}

int ilp_unpack(int32_t packed,uint32_t *id,uint32_t *value)
{
    if(!id || !value || packed<0 || packed>0x7fff || ((uint32_t)packed&3u)!=1u) return 0;
    uint32_t ordinal=(uint32_t)packed>>2;
    for(uint32_t i=0;i<RANGE_COUNT;++i) {
        if(ordinal>=ranges[i].first && ordinal-ranges[i].first<=ranges[i].max) {
            *id=ranges[i].id; *value=ordinal-ranges[i].first;
            return 1;
        }
    }
    return 0;
}

int ilp_edit(int32_t slots[ILP_LANES],uint32_t id,uint32_t value,int clear)
{
    uint16_t packed=0;
    if(!slots || !ilp_pack(id,clear ? 0 : value,&packed)) return ILP_INVALID;
    int found=-1,empty=-1;
    for(uint32_t i=0;i<ILP_LANES;++i) {
        if(slots[i]==ILP_EMPTY) { if(empty<0) empty=(int)i; continue; }
        uint32_t old_id,old_value;
        if(!ilp_unpack(slots[i],&old_id,&old_value)) return ILP_INVALID;
        if(old_id==id) {
            if(found>=0) return ILP_DUPLICATE;
            found=(int)i;
        }
    }
    if(clear) {
        if(found<0) return 0;
        slots[found]=ILP_EMPTY;
        return 1;
    }
    if(found<0) found=empty;
    if(found<0) return ILP_FULL;
    slots[found]=packed;
    return 1;
}

int ilp_store_edit(const IlpStorage *store,uint32_t step,uint32_t id,uint32_t value,int clear)
{
    static const uint8_t codes[ILP_LANES]={0xcb,0xcc,0xce,0xcf,0xd0};
    if(!store || !store->get || !store->set || !store->clear || step>=128) return ILP_INVALID;
    int32_t before[ILP_LANES],after[ILP_LANES];
    for(uint32_t i=0;i<ILP_LANES;++i) before[i]=after[i]=store->get(store->context,codes[i],step);
    int result=ilp_edit(after,id,value,clear);
    if(result<=0) return result;
    for(uint32_t i=0;i<ILP_LANES;++i) if(before[i]!=after[i]) {
        int ok=after[i]==ILP_EMPTY ? store->clear(store->context,codes[i],step) :
            store->set(store->context,codes[i],step,(uint32_t)after[i]);
        return ok ? 1 : ILP_STORAGE_FULL;
    }
    return 1; /* existing lock already had this value */
}

int ilp_store_read(const IlpStorage *store,uint32_t step,uint32_t id,uint32_t *value)
{
    static const uint8_t codes[ILP_LANES]={0xcb,0xcc,0xce,0xcf,0xd0};
    uint16_t probe;
    if(!store || !store->get || !value || step>=128 || !ilp_pack(id,0,&probe)) return ILP_INVALID;
    int found=0; uint32_t result=0;
    for(uint32_t i=0;i<ILP_LANES;++i) {
        int32_t raw=store->get(store->context,codes[i],step);
        if(raw==ILP_EMPTY) continue;
        uint32_t other,v;
        if(!ilp_unpack(raw,&other,&v)) return ILP_INVALID;
        if(other==id) { if(found) return ILP_DUPLICATE; found=1; result=v; }
    }
    if(found) *value=result;
    return found;
}

int ilp_store_edit_steps(const IlpStorage *store,const uint32_t steps[4],uint32_t id,uint32_t value,int clear)
{
    static const uint8_t codes[ILP_LANES]={0xcb,0xcc,0xce,0xcf,0xd0};
    /* One edit changes at most one lane per step. Save its exact location so
     * rollback also preserves the lane assignment of existing locks. */
    static struct { int32_t before,after; uint8_t step,lane; } journal[128];
    if(!store || !store->get || !store->set || !store->clear || !steps) return ILP_INVALID;
    uint32_t count=0;
    for(uint32_t step=0;step<128;++step) if((steps[step>>5]>>(step&31))&1u) {
        int32_t before[ILP_LANES],after[ILP_LANES];
        for(uint32_t i=0;i<ILP_LANES;++i) before[i]=after[i]=store->get(store->context,codes[i],step);
        int result=ilp_edit(after,id,value,clear);
        if(result<0) return result;
        for(uint32_t i=0;i<ILP_LANES;++i) if(before[i]!=after[i]) {
            journal[count].before=before[i]; journal[count].after=after[i];
            journal[count].step=(uint8_t)step; journal[count].lane=(uint8_t)i; ++count;
        }
    }
    for(uint32_t i=0;i<count;++i) {
        uint32_t code=codes[journal[i].lane],step=journal[i].step;
        int ok=journal[i].after==ILP_EMPTY ? store->clear(store->context,code,step) :
            store->set(store->context,code,step,(uint32_t)journal[i].after);
        if(!ok) {
            int restored=1;
            while(i) {
                --i; code=codes[journal[i].lane]; step=journal[i].step;
                ok=journal[i].before==ILP_EMPTY ? store->clear(store->context,code,step) :
                    store->set(store->context,code,step,(uint32_t)journal[i].before);
                if(!ok) restored=0;
            }
            return restored ? ILP_STORAGE_FULL : ILP_ROLLBACK_FAILED;
        }
    }
    return 1;
}
