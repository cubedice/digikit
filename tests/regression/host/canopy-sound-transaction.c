/* Exercise the real COLOR encoder path with a pre-reserved Sound entry.
 * This host-only experiment does not prove interrupt timing or concurrency. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include "../../../runtime/platform/instrument-play/src/cf/sound-codec.h"
#include "../../../runtime/platform/instrument-play/src/cf/sound-pool.h"

typedef struct {
    SpPool *pool;
    uint16_t reserved, attached;
    int prepares, attaches;
} Transaction;
static void *allocate(uint32_t size, void *context)
{
    (void)context;
    assert(!masked);
    return malloc(size);
}
static int prepare(void *context, const CpPreset *preset)
{
    Transaction *t = context;
    uint8_t bytes[SC_BYTES];
    assert(masked);
    ++t->prepares;
    return sc_encode(preset, bytes) || sp_publish(t->pool, t->reserved, bytes);
}
static void attach(void *context)
{
    Transaction *t = context;
    assert(masked);
    ++t->attaches;
    t->attached = t->reserved;
}
static int claim_default(void *context)
{
    uint16_t *marker = context;
    assert(masked);
    if (*marker != SP_PENDING_DEFAULT)
        return -1;
    *marker = 0;
    return 0;
}
static void edit(Transaction *t, uint32_t page, uint32_t encoder, int32_t movement)
{
    uint32_t view = instrument_src_context(0, page, 1);
    assert(view && !sp_reserve_task(t->pool, &t->reserved));
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, encoder, movement, 100, prepare,
                                       attach, t) == CS_EDIT);
    CpPreset actual;
    uint8_t encoded[SC_BYTES], saved[SC_BYTES];
    preset_snapshot(cur, &actual);
    assert(!sc_encode(&actual, encoded) && !sp_read(t->pool, t->attached, saved));
    assert(!memcmp(encoded, saved, sizeof(encoded)) && !masked);
}
int main(void)
{
    mi_ledger.epoch = 1;
    mi_ledger.generations[0] = 1;
    instrument_play_tick();
    ip_admission[0] = 1;
    ip_admission[1] = 0x123400;
    ip_admission[4] = 7;
    ip_admission[5] = admitted;
    cur->mode = KB_ROOT;
    instrument_select_track(0);
    /* A proof Sound handoff must reject stale ownership and publish a valid
     * snapshot atomically without leaving interrupts masked. */
    CpPreset handoff, unchanged;
    uint32_t sound_generation = 0;
    memset(&handoff, 0x5a, sizeof(handoff));
    memcpy(&unchanged, &handoff, sizeof(handoff));
    cur->voicing = 2;
    assert(instrument_sound_snapshot(0, 0x999900, &handoff, &sound_generation) < 0);
    assert(!memcmp(&handoff, &unchanged, sizeof(handoff)) && !sound_generation);
    assert(!instrument_sound_snapshot(0, 0x123400, &handoff, &sound_generation) &&
           sound_generation == 7);
    cur->voicing = 0;
    assert(instrument_sound_restore(0, 0x123400, sound_generation + 1, &handoff) < 0);
    assert(instrument_sound_restore(0, 0x999900, sound_generation, &handoff) < 0);
    ip_admission[6] = 1;
    assert(instrument_sound_restore(0, 0x123400, sound_generation, &handoff) < 0);
    ip_admission[6] = 0;
    assert(!instrument_sound_restore(0, 0x123400, sound_generation, &handoff) &&
           cur->voicing == 2 && !masked);
    cur->voicing = 0;
    /* VOICE page: COLOR is encoder 1 of a Velvet Sound. */
    uint32_t view = instrument_src_context(0, 0, 1);
    assert(view && pad_color == 64);
    SpPool pool;
    sp_init(&pool, allocate, 0);
    Transaction t = {.pool = &pool};
    /* No reservation: neither the real control nor the attached handle changes. */
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 1, 16, 100, prepare, attach, &t) < 0);
    assert(pad_color == 64 && !t.attached && !t.attaches && !masked);
    assert(!sp_reserve_task(&pool, &t.reserved));
    int attempts = t.prepares;
    assert(instrument_sound_proof_edit(0, 0x999900, 7, view, 1, 16, 100, prepare, attach, &t) < 0);
    assert(instrument_sound_proof_edit(0, 0x123400, 8, view, 1, 16, 100, prepare, attach, &t) < 0);
    assert(t.prepares == attempts && pad_color == 64 && !masked);
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 1, 16, 100, prepare, attach, &t) ==
           CS_EDIT);
    assert(pad_color == 80 && t.attached == t.reserved && t.attaches == 1 && !masked);
    uint8_t bytes[SC_BYTES];
    CpPreset recorded;
    assert(!sp_read(&pool, t.attached, bytes) && !sc_decode(&recorded, bytes));
    assert(recorded.control[CP_COLOR] == 80);
    /* Discrete controls (WAVE, encoder 0) accumulate small turns without spending a reservation. */
    view = instrument_src_context(0, 0, 1);
    assert(!sp_reserve_task(&pool, &t.reserved));
    unsigned prior_attaches = (unsigned)t.attaches;
    for (unsigned turn = 0; turn < 3; ++turn)
        assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 0, 1, 100, prepare, attach, &t) ==
               CS_READONLY);
    assert((unsigned)t.attaches == prior_attaches && sp_read(&pool, t.reserved, bytes) < 0);
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 0, 1, 100, prepare, attach, &t) ==
           CS_EDIT);
    assert(t.attached == t.reserved && !sp_read(&pool, t.attached, bytes) && pad_family == 1);
    edit(&t, 0, 7, 4);  /* drift */
    edit(&t, 2, 6, 7);  /* MIX */
    edit(&t, 0, 0, 4);  /* switch to wavetable */
    edit(&t, 0, 1, 5);  /* wavetable position */
    edit(&t, 0, 5, 4);  /* WT clock also changes its rate */
    edit(&t, 1, 0, 6);  /* wavetable attack */
    edit(&t, 0, 0, 20); /* switch to FM */
    edit(&t, 0, 1, 4);  /* FM algorithm */
    edit(&t, 1, 0, 6);  /* FM attack */
    edit(&t, 1, 4, 4);  /* FM COLOR */
    /* A fresh zero-handle Sound gets the canonical SRC defaults, but cannot
     * reset a stale or loading admission. A default Sound has no user shape. */
    cur->shape.count = 3;
    cur->shape.intervals[0] = 0;
    cur->shape.intervals[1] = 4;
    cur->shape.intervals[2] = 7;
    cur->shape.intervals[3] = 0;
    uint16_t prior_drift = (uint16_t)cur->pad_drift, marker = SP_PENDING_DEFAULT;
    assert(prior_drift &&
           instrument_sound_proof_defaults(0, 0x999900, 7, claim_default, &marker) < 0);
    assert(instrument_sound_proof_defaults(0, 0x123400, 8, claim_default, &marker) < 0 &&
           cur->pad_drift == prior_drift);
    ip_admission[6] = 1;
    assert(instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) < 0 &&
           cur->pad_drift == prior_drift);
    ip_admission[6] = 0;
    assert(marker == SP_PENDING_DEFAULT);
    assert(!instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) && !masked &&
           !marker);
    cur->pad_drift = 9;
    assert(instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) < 0 &&
           cur->pad_drift == 9);
    marker = SP_LOAD_ERROR;
    assert(instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) < 0 &&
           cur->pad_drift == 9);
    marker = SP_PENDING_DEFAULT;
    assert(!instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) && !marker);
    assert(cur->mode == KB_PLAY && !cur->shape.count);
    CpPreset reset;
    preset_snapshot(cur, &reset);
    CsState defaults;
    cs_init(&defaults);
    for (uint32_t i = 0; i < CP_CONTROLS; ++i)
        assert(reset.control[i] == defaults.value[i]);
    for (uint32_t i = 0; i < 13; ++i)
        assert(reset.fm[i] == defaults.fm[i]);
    assert(!reset.shape.count);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
    return 0;
}
