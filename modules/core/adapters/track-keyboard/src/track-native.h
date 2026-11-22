#ifndef DK_TRACK_NATIVE_H
#define DK_TRACK_NATIVE_H

#include "track-sequence.h"

enum TkNativeKind {
    TK_NATIVE_UNSUPPORTED = -1,
    TK_NATIVE_AUDIO,
    TK_NATIVE_MIDI,
    TK_NATIVE_CANOPY,
    TK_NATIVE_RELAY
};
bool tk_native_init(void);

/* The caller owns one UI task. Release and drain the old binding before changing
 * the track's type. Only admitted physical destinations increase stock capacity. */
int tk_native_kind(uint8_t track);
void tk_native_backend(bool midi, TkTrackBackend *out);
void tk_native_relay_backend(TkTrackBackend *out);
bool tk_native_relay_sound(uintptr_t sound);
bool tk_native_event(void *context, uint8_t track, const TkNoteEvent *event);
void tk_native_drain(void);
bool tk_native_idle(uint8_t track);

/* DTII 1.17 firmware entry points, isolated for the host scheduler test. */
int tk_native_channel(uint8_t track);
uint8_t tk_native_midi_ports(void);
bool tk_native_try_midi_send(uint8_t track, uint8_t pitch, uint8_t velocity, uint8_t ports);
bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity);
bool tk_native_try_seed_send(uint8_t track, uint8_t pitch, uint8_t velocity,
                             const TkSequenceAudio *audio);
bool tk_native_try_helper_send(uint8_t owner, uint8_t track, uint8_t pitch, uint8_t velocity,
                               const TkSequenceAudio *audio);

#endif
