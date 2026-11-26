#ifndef DK_INSTRUMENT_SRC_MOD_H
#define DK_INSTRUMENT_SRC_MOD_H
#include <stdint.h>
/* UI task publication. Native LFO destinations remain six stable Sound slots;
 * their synthesis meanings follow the requested CANOPY family. */
void instrument_mod_publish(uint32_t family);
/* Core/machines parameter-code callback. UINT32_MAX means stock fallback. */
uint32_t instrument_mod_param(uint32_t type, uint32_t mirror);
/* Only the admitted CANOPY Sound is passed here, before its stock refresh.
 * The six otherwise unused native SRC values are bipolar modulation carriers,
 * not the persistent CANOPY base values. */
void instrument_mod_defaults(uint32_t source);
/* Called by the two stock frame-conversion shims with the track they convert;
 * true only for a track whose admitted instance owns its live instrument. */
int instrument_mod_carrier(uint32_t track);
#endif
