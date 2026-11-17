/* CANOPY Sound record extension, format 1 (DTII 1.17 v3 Sound records).
 *
 * Forty bytes occupy twenty record halfwords that the stock writer zero-fills
 * and the stock reader never leaves in a live parameter: positions 4..28 in
 * steps of 4 (loaded into dummy parameter 0, then overwritten by position 32,
 * which is never used here and stays zero) and positions 77..89 (outside the
 * 77-entry load loop). Position p is at record offset 0x1c + 2p. Stock 1.17
 * therefore loads a record carrying the extension exactly as it loads the
 * same record without it, and saving on stock firmware drops the extension.
 *
 * Byte 0 is 0xc0 | format. Bytes 1..37 hold the bit-packed preset and bytes
 * 38..39 a CRC-16/CCITT over bytes 0..37. */
#ifndef DK_CANOPY_SOUND_RECORD_H
#define DK_CANOPY_SOUND_RECORD_H
#include <stdint.h>
#include "preset.h"
enum { SR_BYTES = 40, SR_SLOTS = 20, SR_FORMAT = 0xc1 };
/* 0 on success. Only normalized current state (cp_valid, current clock). */
int sr_encode(const CpPreset *p, uint8_t out[SR_BYTES]);
/* 0 on success; anything invalid leaves *out untouched. */
int sr_decode(CpPreset *out, const uint8_t in[SR_BYTES]);
/* Nonzero for an extension of any CANOPY format, valid or not. Such bytes
 * are carried with their Sound unchanged; only format 1 decodes. */
int sr_present(const uint8_t in[SR_BYTES]);
/* Record offset of extension halfword i (0..SR_SLOTS-1). */
uint32_t sr_offset(uint32_t i);
void sr_gather(const uint8_t *record, uint8_t out[SR_BYTES]);
void sr_scatter(uint8_t *record, const uint8_t in[SR_BYTES]);
#endif
