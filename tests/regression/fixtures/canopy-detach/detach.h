/* Test-only in-place Sound overwrite model. No firmware hook or recovery ABI. */
#ifndef CANOPY_DETACH_H
#define CANOPY_DETACH_H
#include <stdint.h>
#include "../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"
enum {
    DT_SOUND_SIZE = 0x450,
    DT_HANDLE = 0x13a,
    DT_EMPTY = 0,
    DT_PENDING = 1,
    DT_DONE = 2,
    DT_QUARANTINED = 3
};
typedef struct {
    uint32_t (*mask)(void *);
    void (*restore)(void *, uint32_t);
    int (*owned)(void *, uint32_t, uint32_t, int);
    int (*snapshot)(void *, uint32_t, uint32_t, CpPreset *);
    /* Infallible after owned() preflight, and executed under the same lock. */
    void (*suspend_begin)(void *, uint32_t, uint32_t);
    void (*end)(void *);
    /* Must admit and apply atomically under the lock; on failure it leaves
     * the instance silent/unadmitted. Stock firmware lacks this hook today. */
    int (*admit_apply)(void *, uint32_t, uint32_t, const CpPreset *);
    void *context;
} DtOps;
typedef struct {
    SpPool *pool;
    const DtOps *ops;
    uint8_t *sound, *undo;
    uint32_t sound_id, old_generation, new_generation, phase;
    uint16_t old_handle, new_handle;
    CpPreset old_preset, new_preset;
} Detach;
/* A complete incoming snapshot and a pre-reserved old-snapshot credit are
 * mandatory. Under one lock: capture old settings, publish the old snapshot,
 * copy a full undo Sound and suspend/invalidate the matching instance. */
int dt_begin(Detach *t, SpPool *pool, const DtOps *ops, uint8_t *sound, uint8_t *undo,
             uint32_t sound_id, uint32_t generation, uint16_t old_credit, uint16_t new_handle);
/* Caller performs a full stock overwrite while admission is held closed.
 * complete=0 restores the complete undo Sound. Generation mismatch never
 * writes to the potentially reused destination and stays quarantined. */
int dt_finish(Detach *t, int complete);
#endif
