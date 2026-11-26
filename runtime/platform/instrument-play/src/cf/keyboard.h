/* What a CANOPY Sound records of its performance: the keyboard mode (PLAY, or ROOT + TYPE) and a user chord shape by
 * value. The track keyboard plays them (instrument_track_keyboard_legacy); CANOPY has no note mapper of its own. */
#ifndef DK_INSTRUMENT_KEYBOARD_H
#define DK_INSTRUMENT_KEYBOARD_H
#include "../../../midi-events/src/cf/ownership.h"
enum { KB_PLAY, KB_ROOT };
typedef struct {
    uint8_t count, intervals[4];
} KbShape;
#endif
