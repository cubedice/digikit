/* Core-facing DT2 SRC capability. This does not allocate native parameter slots
 * or relax the V1 DK_PAGE restrictions. Register only after a complete painter
 * and input adapter are ready. All calls run on the UI task. */
#ifndef DK_MACHINE_SRC_PAGES_H
#define DK_MACHINE_SRC_PAGES_H
#include <stdint.h>
#define MF_SRC_ABI 1u
typedef struct {
    uint32_t version, bytes, pages, generation;
} MfSrcCapability;
/* type 7..31, pages 1..2, nonzero instance generation. Descriptor copied. */
int mf_src_register(uint32_t type,const MfSrcCapability *cap);
/* An old owner's teardown cannot remove a replacement generation. */
void mf_src_unregister(uint32_t type,uint32_t generation);
/* Called by the draw hook. Unregistered/invalid types retain the one-page rule. */
uint32_t mf_src_page_count(uint32_t type);
#endif
