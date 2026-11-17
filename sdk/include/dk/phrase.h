/* Generic single-owner phrase FX capability, DTII first. No firmware addresses. */
#ifndef DK_PHRASE_H
#define DK_PHRASE_H
#include <stdint.h>
#include "../../dsp/contract/dk_phrase.inc"

/* Raw ranges: REC/FROM0..1, TAKE/CUE0..128 (0=PAT/NOW), STRT0..126,
 * LEN1..127, RATE0..12 (OFF,1/1,1/2,1/4,1/8,1/16,1/32,1/64,
 * 1/4T,1/8T,1/16T,1/32T,1/64T), TUNE0..96 (-24..24 in .5st),
 * SHAP0..3, SCAN0..128 (64=still), MASK18bits; others0..127.
 * Gains16and17 use the single linked EXT_GAIN. Zero gain removes membership.
 * GET/SET/actions execute only on the UI task. Snapshot is a bounded copy.
 * active returns the native global FX page code, or0 when no owner is loaded.
 */
uint32_t dkrt_phrase_get(uint32_t index);
int32_t dkrt_phrase_set(uint32_t index, uint32_t value);
int32_t dkrt_phrase_action(uint32_t action);
uint32_t dkrt_phrase_active(void);
const char *dkrt_phrase_title(void);
uint32_t dkrt_phrase_snapshot(uint32_t *out, uint32_t words);
#endif
