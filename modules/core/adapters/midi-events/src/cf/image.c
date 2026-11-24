/* The image part of core/midi-events that is not generated (docs/plans/fork-image-diet.md): the release step of the DSP
 * reset hook (reset.s) reads only core/dsp-link, so it stays in the image whether or not services/instrument.dks is loaded. */
#include <stdint.h>
extern int dl_reset_quiesce(void) __attribute__((weak));

int mi_dsp_reset_release(void)
{
    return !dl_reset_quiesce || dl_reset_quiesce() != 2;
}

/* The channel router (core/platform's USB path and din.s call it): the track keyboard first, then CANOPY's channel ledger while
 * services/instrument.dks is loaded (the forwarder answers 0 otherwise), then the track keyboard's own routes. */
extern int tk_midi_channel_message(const uint8_t *msg, uint32_t len, uint32_t source, int first) __attribute__((weak));
extern int mi_legacy_channel_message(const uint8_t *msg, uint32_t len, uint32_t source);
int mi_channel_message(const uint8_t *msg, uint32_t len, uint32_t source)
{
    if (tk_midi_channel_message) {
        int owned = tk_midi_channel_message(msg, len, source, 1);
        if (owned)
            return owned > 0;
    }
    if (mi_legacy_channel_message(msg, len, source))
        return 1;
    return tk_midi_channel_message ? tk_midi_channel_message(msg, len, source, 0) : 0;
}
