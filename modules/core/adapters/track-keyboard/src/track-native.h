#ifndef DK_TRACK_NATIVE_H
#define DK_TRACK_NATIVE_H

#include "track-service.h"

enum TkNativeKind { TK_NATIVE_UNSUPPORTED = -1, TK_NATIVE_AUDIO, TK_NATIVE_MIDI };

/* The caller owns one UI task. Release and drain the old binding before changing
 * the track's type. Stock audio capacity is one until helper routing is qualified.
 */
int tk_native_kind(uint8_t track);
void tk_native_backend(bool midi, TkTrackBackend *out);
bool tk_native_event(void *context, uint8_t track, const TkNoteEvent *event);
void tk_native_drain(void);
bool tk_native_idle(uint8_t track);

/* DTII 1.17 firmware entry points, isolated for the host scheduler test. */
int tk_native_channel(uint8_t track);
bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity);

#endif
