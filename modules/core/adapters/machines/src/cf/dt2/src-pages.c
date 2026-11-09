#include "src-pages.h"
typedef struct { uint32_t pages,generation; } Entry;
static Entry entries[32];
int mf_src_register(uint32_t type,const MfSrcCapability *cap)
{
    if(type<7 || type>=32 || !cap || cap->version!=MF_SRC_ABI ||
        cap->bytes!=sizeof(*cap) || cap->pages<1 || cap->pages>4 || !cap->generation) return -1;
    Entry *e=&entries[type];
    /* Registration is idempotent, not a way for an older owner to reclaim a
     * live view. Replacement requires matching-generation revocation first. */
    if(e->generation && e->generation!=cap->generation) return -1;
    e->generation=cap->generation; e->pages=cap->pages;
    return 0;
}
void mf_src_unregister(uint32_t type,uint32_t generation)
{
    if(type<7 || type>=32 || !generation || entries[type].generation!=generation) return;
    entries[type].pages=0; entries[type].generation=0;
}
extern uint32_t mf_native_page_count(uint32_t type, uint32_t ordinary);
uint32_t mf_src_page_count(uint32_t type)
{
    if(type<7 || type>=32 || !entries[type].generation) return mf_native_page_count(type, 1);
    return entries[type].pages;
}

extern uint8_t *mf_src_alloc(uint32_t bytes);
extern void mf_src_free(uint8_t *ptr);
uint32_t mf_src_apply_pages(uint8_t *view,uint32_t pages)
{
    uint32_t *v=(uint32_t *)(view+0x7c),begin=v[0],end=v[1],capacity=v[2];
    uint32_t *index=(uint32_t *)(view+0x90);
    if(!begin || (begin&3) || end<begin || capacity<end ||
        ((end-begin)&3) || ((capacity-begin)&3) || capacity-begin<4) {
        *index=0; return 0;
    }
    if(pages<1 || pages>4) pages=1;
    if(pages>2 && pages>(capacity-begin)/4 && capacity-begin>=8) {
        /* Preserve existing IDs, including a currently hidden sample page.
         * Added pages use the knob page ID. Publish before freeing old
         * storage; no module-owned pointer survives on the native object. */
        uint32_t *grown=(uint32_t *)mf_src_alloc(pages*4);
        if(!grown) {
            uint32_t current=(end-begin)/4;
            if(*index>=current) *index=0;
            return current;
        }
        const uint32_t *old=(const uint32_t *)(uintptr_t)begin;
        uint32_t old_count=(capacity-begin)/4;
        for(uint32_t i=0;i<pages;++i) grown[i]=i<old_count ? old[i] : old[0];
        begin=(uint32_t)(uintptr_t)grown;
        v[0]=begin; v[1]=begin+pages*4; v[2]=begin+pages*4;
        mf_src_free((uint8_t *)old);
        capacity=begin+pages*4;
    }
    if(pages>(capacity-begin)/4) pages=1;
    v[1]=begin+pages*4;
    if(*index>=pages) *index=0;
    return pages;
}
