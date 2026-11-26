/* The image part of core/relay-track: what the forwarders of seam.s jump to while services/relay.dks is not loaded (the slot
 * relay_vec is 0). RELAY's code is in the service file; the image keeps its DSP objects, its parameter entries, the seam and these
 * two block-interrupt paths, because core/dsp-link reserves RELAY's frame words whenever this adapter is in the image. */
#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
extern void pp_record_timing(u32 out[4]);

/* relay_write_frame without the service: the timing words and no command (files.c writes the same when nothing is pending), so
 * the DSP engine sees an idle tape transport and never a stale command. */
void relay_idle_frame(uintptr_t queue, u32 at)
{
    volatile u16 *dst = (volatile u16 *)(queue + 4 * at + 2);
    u32 timing[4];
    pp_record_timing(timing);
    for (unsigned i = 0; i < 4; ++i) {
        dst[0] = (u16)timing[i];
        dst[2] = (u16)(timing[i] >> 16);
        dst += 4;
    }
    dst[0] = dst[2] = 0;
}

/* rtm_gate_frame without the service: the stock frame unchanged. */
const u16 *rtm_gate_pass(const u16 *src)
{
    return src;
}
