/* The image part of core/midi-events that is not generated (docs/plans/fork-image-diet.md): the release step of the DSP
 * reset hook (reset.s) reads only core/dsp-link, so it stays in the image whether or not services/instrument.dks is loaded. */
#include <stdint.h>
extern int dl_reset_quiesce(void) __attribute__((weak));

int mi_dsp_reset_release(void)
{
    return !dl_reset_quiesce || dl_reset_quiesce() != 2;
}
