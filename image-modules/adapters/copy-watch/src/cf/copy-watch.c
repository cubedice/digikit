/* core/copy-watch: who hears of a long copy or fill (copy-watch.s). The track keyboard follows pattern and track copies and pattern
 * clears; CANOPY's Sound persistence (core/instrument-play) follows every copy or fill of at least one Sound once its store exists.
 * Either may be missing from the image. */
#include <stdint.h>

/* Set by a client that wants every long copy and fill (sound-persist.c once its association store exists). */
volatile uint32_t cw_long_watch;

extern void ip_sa_copy_before(uint32_t destination, uint32_t source, uint32_t length)
    __attribute__((weak));
extern void ip_sa_copy_after(uint32_t destination, uint32_t source, uint32_t length)
    __attribute__((weak));
extern void ip_sa_cleared(uint32_t destination, uint32_t length) __attribute__((weak));
extern void tk_pattern_copied(uint32_t destination, uint32_t source, uint32_t length)
    __attribute__((weak));
extern void tk_pattern_cleared(uint32_t destination, uint32_t length) __attribute__((weak));

void cw_copy_before(uint32_t destination, uint32_t source, uint32_t length)
{
    if (ip_sa_copy_before)
        ip_sa_copy_before(destination, source, length);
}

void cw_copy_after(uint32_t destination, uint32_t source, uint32_t length)
{
    if (tk_pattern_copied && (length == 0x1db8cu || length == 0x6a5u))
        tk_pattern_copied(destination, source, length);
    if (ip_sa_copy_after)
        ip_sa_copy_after(destination, source, length);
}

void cw_cleared(uint32_t destination, uint32_t length)
{
    if (tk_pattern_cleared && length == 0x1db8cu)
        tk_pattern_cleared(destination, length);
    if (ip_sa_cleared)
        ip_sa_cleared(destination, length);
}
