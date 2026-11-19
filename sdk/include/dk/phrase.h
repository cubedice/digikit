/* Generic single-owner phrase FX capability, DTII first. No firmware addresses. */
#ifndef DK_PHRASE_H
#define DK_PHRASE_H
#include <stdint.h>
#include "../../dsp/contract/dk_phrase.inc"

/* The provider (core/phrase-control) holds the owner's raw values PH_P_* and sends them to the engine as the control
 * words PH_C_*; the layout and the ranges are in dk_phrase.inc. GET and SET execute only on the UI task. SET clamps
 * to the range and returns 0; -1 without an owner or for an index that does not exist; -2 for PH_P_XFAD, which is the
 * loop effect's crossfader (<dk/loop.h>). PH_P_INPT is not saved: after a restart it is 0 (LIVE). PH_P_TRKS, the tracks the
 * effect hears, is saved with the other values; the provider gives the fx service their send amounts. Snapshot is a bounded copy of the engine's status
 * words PH_S_* and returns whether the status is fresh. active returns the native global FX page code of the owner's
 * page, or 0 when no owner is loaded.
 */
uint32_t dkrt_phrase_get(uint32_t index);
int32_t dkrt_phrase_set(uint32_t index, uint32_t value);
uint32_t dkrt_phrase_active(void);
const char *dkrt_phrase_title(void);
uint32_t dkrt_phrase_snapshot(uint32_t *out, uint32_t words);
#endif
