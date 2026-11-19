/* The loop effect's controls and status, DTII first: the one home of the crossfader, ARM and DROP (core/loop-control).
 * No firmware addresses. The values live outside the kit: they are the same in every kit and do not change at a
 * pattern change. Any page of the image may show and set them (core/loop-display does; the phrase page may).
 *
 * Everything here runs on the UI task. The layout of the status words (LS_*) and the indexes of the raw values (LP_*)
 * are in dk_loop.inc, which the engine shares.
 */
#ifndef DK_LOOP_H
#define DK_LOOP_H
#include <stdint.h>
#include "../../dsp/contract/dk_loop.inc"

/* The native FX page code of the loop effect's page, or 0 while no loop effect is loaded. */
uint32_t dkrt_loop_page(void);
/* A raw value (LP_XFAD: the crossfader, 0 LIVE .. 127 LOOP; the others are the home's counters), 0 for no such index. */
uint32_t dkrt_loop_get(uint32_t index);
/* Set the crossfader, 0 .. 127 (a larger value is 127). */
void dkrt_loop_xfad(uint32_t value);
/* ARM: arm the engine for the next pattern change, or take the arm back when it is armed. */
void dkrt_loop_arm(void);
/* Drop the loop. */
void dkrt_loop_drop(void);
/* Copy the engine's status (LS_WORDS words) to `out`; returns 1 when it is fresh, 0 when the engine has not reported
 * for a while (no loop effect is loaded, or its DSP part does not run): the words are then the last ones. */
uint32_t dkrt_loop_status(uint32_t *out);
#endif
