#ifndef DK_TRACK_CANOPY_H
#define DK_TRACK_CANOPY_H
#include "track-service.h"

bool tk_canopy_is_owner(uint8_t track, int type);
bool tk_canopy_routed(uint8_t track);
void tk_canopy_backend(TkTrackBackend *out);
void tk_canopy_sync(uint8_t track, bool active);
/* The mod wheel (CC1), Reset All Controllers (CC121) or pitch bend for `tracks` (`type` 0xb0 or 0xe0, the data bytes): given to
 * each that CANOPY plays through the track keyboard. The tracks that took it. */
uint16_t tk_canopy_controller(uint16_t tracks, uint8_t type, uint8_t data1, uint8_t data2);

#endif
