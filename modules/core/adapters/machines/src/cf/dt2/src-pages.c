#include "src-pages.h"
typedef struct { uint32_t pages,generation; } Entry;
static Entry entries[32];
int mf_src_register(uint32_t type,const MfSrcCapability *cap)
{
    if(type<7 || type>=32 || !cap || cap->version!=MF_SRC_ABI ||
        cap->bytes!=sizeof(*cap) || cap->pages<1 || cap->pages>2 || !cap->generation) return -1;
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
    return entries[type].pages==2 ? 2 : 1;
}
