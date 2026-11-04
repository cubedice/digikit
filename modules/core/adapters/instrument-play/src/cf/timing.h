#ifndef INSTRUMENT_PLAY_TIMING_H
#define INSTRUMENT_PLAY_TIMING_H
#include <stdint.h>
typedef struct { uint32_t running, pending, block; } IpStartTiming;
/* Retain an already admitted, unchanged Sound during START lookahead only.
 * Never authorize a new instance or tolerate invalid timing mid-performance.
 * 64 audio blocks bounds this transition to 42.7 ms; expiry fails closed. */
static inline int ip_start_hold(IpStartTiming *s, uint32_t running,
    uint32_t valid, uint32_t block, uint32_t same)
{
    if (running && !s->running && same) { s->pending=1; s->block=block; }
    s->running=running;
    if (!running || valid || !same || block-s->block>64) s->pending=0;
    return s->pending != 0;
}
#endif
