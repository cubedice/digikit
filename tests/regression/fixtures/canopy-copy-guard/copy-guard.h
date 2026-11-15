/* Isolated experiment only: not linked into the hardware adapter. */
#ifndef CANOPY_COPY_GUARD_H
#define CANOPY_COPY_GUARD_H
#include <stdint.h>

enum { CG_SOUND=0x450, CG_KIT=0x5574, CG_KIT_FIRST=0x34, CG_TRACKS=16 };
typedef struct {
    uint32_t (*mask)(void *ctx);
    void (*restore)(void *ctx,uint32_t sr);
    /* Called under the mask for every Sound in the source. It must be bounded,
     * allocation-free, nonblocking and safe when an earlier freeze succeeded. */
    int (*freeze)(void *ctx,const uint8_t *sound);
    /* The stock forward memcpy; no allocation, waiting, or callbacks. */
    void (*copy)(void *ctx,void *dst,const void *src,uint32_t length);
    void *ctx;
} CopyGuard;
/* Reject invalid sizes/overlap before touching data. On freeze failure, no
 * bytes are copied; prior freezes remain conservatively shared. The caller
 * must ensure valid RAM, Sound identities, copy authorization, and no DMA or
 * non-maskable writer. Full copy occurs under mask; no latency claim. */
int cg_copy(const CopyGuard *guard,void *dst,const void *src,uint32_t length);
#endif
