/* Compose the existing upload hook with the optional instrument adapter.
 * No provider means exactly the original upload/stock dispatch. The adapter
 * only accepts normalized channel messages, never SysEx or clock. */
#include <stdint.h>
extern int mi_channel_message(const uint8_t *, uint32_t, uint32_t) __attribute__((weak));
extern int dkrt_midi_rx(const uint8_t *, uint32_t, uint32_t);

int dkrt_midi_dispatch(const uint8_t *msg, uint32_t len, uint32_t source)
{
    if (mi_channel_message && mi_channel_message(msg, len, source)) return 1;
    return dkrt_midi_rx(msg, len, source);
}
