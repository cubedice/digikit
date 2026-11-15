/* Packed CANOPY parameter locks stored in five native pattern-lock lanes. */
#ifndef DK_INSTRUMENT_SRC_LOCK_PACKED_H
#define DK_INSTRUMENT_SRC_LOCK_PACKED_H
#include <stdint.h>

/* The low two bits are 01; stock UI lock values have low eight bits zero.
 * The upper 13 bits densely index the valid control/value combinations.
 * This distinguishes stock UI locks, but cannot prove ownership of arbitrary
 * locks written by other software. A negative stock value means no lock. */
enum { ILP_ID_LIMIT=62, ILP_POSITION=17, ILP_RATE=19,
       /* IDs 40..52 distinguish the remembered FM bank from WT/legacy
        * controls that reuse the same CP_* numbers. */
       ILP_FM_DEPTH=40, ILP_FM_FIRST=41, ILP_FM_LAST=52,
       /* IDs 53..55 are the second to fourth notes of a recorded chord,
        * each an interval of -48..+48 semitones from the trig's note (value
        * minus ILP_NOTE_CENTER). The CF mapper consumes them; they never
        * reach the DSP bank. */
       ILP_NOTE_FIRST=53, ILP_NOTE_LAST=55, ILP_NOTE_CENTER=48, ILP_NOTE_MAX=96 };

/* Returns 1 on success. On failure output arguments remain untouched. The
 * Caller additionally validates the parameter against its active sound family
 * and any mode-specific minimum or maximum (e.g. synced scan rate). */
int ilp_pack(uint32_t id,uint32_t value,uint16_t *packed);
int ilp_unpack(int32_t packed,uint32_t *id,uint32_t *value);
/* Translate between a source-family control number and its stable wire ID.
 * Returns UINT32_MAX when the control does not belong to that family. */
uint32_t ilp_id_for(uint32_t family,uint32_t control);
uint32_t ilp_control_for(uint32_t family,uint32_t id);
/* Edit one step's five lanes after the caller has established ownership.
 * No lock is represented by -1. Existing parameters keep their lane;
 * inserting takes the first empty one. A failed edit never changes the array.
 * 1 changed, 0 absent on clear, -1 invalid data/input, -2 all lanes in use,
 * -3 duplicate parameter identity. No firmware storage is accessed here. */
enum { ILP_LANES=5, ILP_EMPTY=-1, ILP_INVALID=-1, ILP_FULL=-2, ILP_DUPLICATE=-3 };
int ilp_edit(int32_t slots[ILP_LANES],uint32_t id,uint32_t value,int clear);
/* One-step storage transaction. The callbacks address stock parameter codes,
 * not lane numbers. Reads and validation complete before the only write. */
typedef struct {
    void *context;
    int32_t (*get)(void *,uint32_t code,uint32_t step);
    int (*set)(void *,uint32_t code,uint32_t step,uint32_t value);
    int (*clear)(void *,uint32_t code,uint32_t step);
} IlpStorage;
enum { ILP_STORAGE_FULL=-4, ILP_ROLLBACK_FAILED=-5 };
int ilp_store_edit(const IlpStorage *store,uint32_t step,uint32_t id,uint32_t value,int clear);
/* Edit a selection of steps as one transaction. Call from the UI task only;
 * the bounded journal is shared and no concurrent edits may interleave. */
int ilp_store_edit_steps(const IlpStorage *store,const uint32_t steps[4],uint32_t id,uint32_t value,int clear);
/* Read one ID without changing output on failure or absence. */
int ilp_store_read(const IlpStorage *store,uint32_t step,uint32_t id,uint32_t *value);
#endif
