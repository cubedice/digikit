#ifndef DK_TRACK_CANOPY_H
#define DK_TRACK_CANOPY_H
#include "track-service.h"

bool tk_canopy_is_owner(uint8_t track, int type);
void tk_canopy_backend(TkTrackBackend *out);
void tk_canopy_sync(uint8_t track, bool active, uint32_t pattern);

#endif
