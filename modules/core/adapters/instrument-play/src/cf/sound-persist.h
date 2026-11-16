/* CANOPY Sound persistence on DT2 1.17: the stock-hook side (sound-persist.c)
 * and the instance side (control.c). See sound-assoc.h for the store. */
#ifndef DK_CANOPY_SOUND_PERSIST_H
#define DK_CANOPY_SOUND_PERSIST_H
#include <stdint.h>
#include "sound-assoc.h"
/* The store, or 0 before its memory exists. Mask interrupts around use. */
SaStore *ip_sa_store(void);
/* Allocate the store's memory if it does not exist yet. Task context with
 * interrupts enabled only; otherwise (and on allocation failure) it does
 * nothing. Returns the store or 0. */
SaStore *ip_sa_ensure(void);
/* Once per UI tick: ip_sa_ensure, then copy the counters for proofs. */
void ip_sa_tick(void);
/* control.c: publish the current state of every bound instance whose Sound
 * lies wholly in [lo, hi), before stock observes those Sounds. Any context;
 * masks interrupts itself. */
void instrument_sound_flush(uintptr_t lo,uintptr_t hi);
#endif
