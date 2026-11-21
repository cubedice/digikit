#ifndef DK_TRACK_MIDI_H
#define DK_TRACK_MIDI_H

#include <stdbool.h>
#include <stdint.h>

/* The MIDI parser invokes this before native note routing. For its first pass,
 * a matching owner can consume (1) or continue the original stock route (-1)
 * ahead of any new route. Zero leaves the message available to other owners.
 */
int tk_midi_channel_message(const uint8_t *bytes, uint32_t len, uint32_t source, int existing_only);
enum TkMidiNativeAction { TK_MIDI_NATIVE, TK_MIDI_SUPPRESS, TK_MIDI_RECORD_ONLY };
int tk_midi_native_action(uint32_t track, uint32_t channel, uint32_t note, uint32_t source,
                          uint32_t on);

/* UI task. Publish eligible tracks and dispatch events to the service. */
bool tk_midi_init(void);
void tk_midi_poll(void);
void tk_midi_reset(void);
void tk_midi_cancel_track(uint8_t track);

/* UI task editor callbacks. */
uint16_t tk_runtime_learn_mask(void);
bool tk_runtime_learn_note(uint8_t track, uint8_t pitch, uint8_t velocity);
void tk_runtime_midi_busy(void);

/* Stock MIDI routing facts. The mask includes bit 16 for the stock auxiliary path. */
uint8_t tk_midi_selected_track(void);
int tk_midi_notes_enabled(void);
uint8_t tk_midi_receive_ports(void);
uint32_t tk_midi_route(uint8_t channel, int auto_channel);

#endif
