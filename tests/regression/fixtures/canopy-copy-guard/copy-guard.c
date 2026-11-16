#include "copy-guard.h"
#include <stdint.h>

int cg_copy(const CopyGuard *g, void *dst, const void *src, uint32_t length)
{
    if (!g || !g->mask || !g->restore || !g->freeze || !g->copy || !dst || !src ||
        (length != CG_SOUND && length != CG_KIT))
        return -1;
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;
    if (d > UINTPTR_MAX - length || s > UINTPTR_MAX - length || (d < s + length && s < d + length))
        return -1;
    uint32_t sr = g->mask(g->ctx);
    const uint8_t *first = (const uint8_t *)src + (length == CG_KIT ? CG_KIT_FIRST : 0);
    uint32_t count = length == CG_KIT ? CG_TRACKS : 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (g->freeze(g->ctx, first + i * CG_SOUND)) {
            g->restore(g->ctx, sr);
            return -1;
        }
    }
    g->copy(g->ctx, dst, src, length);
    g->restore(g->ctx, sr);
    return 0;
}
