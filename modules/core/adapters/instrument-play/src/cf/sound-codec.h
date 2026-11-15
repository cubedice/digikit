/* Compact Sound-persistence experiment; not wired to the firmware. */
#ifndef DK_CANOPY_SOUND_CODEC_H
#define DK_CANOPY_SOUND_CODEC_H
#include "preset.h"
enum { SC_BYTES=42 };
/* Only normalized current state is supported. Invalid input never changes
 * the destination; decoded snapshots likewise commit only after validation. */
int sc_encode(const CpPreset *p,uint8_t out[SC_BYTES]);
int sc_decode(CpPreset *out,const uint8_t in[SC_BYTES]);
/* DTII v3 serialized Sound only. These helpers are not firmware hooks. The
 * caller owns record identity and lifecycle; failure leaves output untouched. */
int sc_sound_record_write(uint8_t *record,uint32_t length,const CpPreset *p);
int sc_sound_record_read(CpPreset *out,const uint8_t *record,uint32_t length);
#endif
