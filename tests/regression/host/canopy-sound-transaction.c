/* Exercise the real STRUM encoder path with a pre-reserved Sound entry.
 * This host-only experiment does not prove interrupt timing or concurrency. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"

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
static int midi(Transaction *t, const MiEvent *input, uint32_t count, MiEvent *output, int *refused)
{
    uint32_t sr = dkrt_irq_mask();
    int n = instrument_sound_proof_midi(0, 0x123400, 7, mi_ledger.epoch, mi_ledger.generations[0],
                                        input, count, output, prepare, attach, t, refused);
    dkrt_irq_restore(sr);
    return n;
}
static MiEvent key(uint32_t token, uint8_t note, uint8_t kind)
{
    MiEvent e = {0};
    e.epoch = mi_ledger.epoch;
    e.generation = mi_ledger.generations[0];
    e.token = token;
    e.kind = kind;
    e.note = note;
    e.value = 90;
    e.port = 0;
    e.channel = 0;
    return e;
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
    cur->mode = keyboard.mode = KB_ROOT;
    instrument_select_track(0);
    /* A proof Sound handoff must reject stale ownership and publish a valid
     * snapshot atomically without leaving interrupts masked. */
    CpPreset handoff, unchanged;
    uint32_t sound_generation = 0;
    memset(&handoff, 0x5a, sizeof(handoff));
    memcpy(&unchanged, &handoff, sizeof(handoff));
    cur->voicing = keyboard.voicing = 2;
    assert(instrument_sound_snapshot(0, 0x999900, &handoff, &sound_generation) < 0);
    assert(!memcmp(&handoff, &unchanged, sizeof(handoff)) && !sound_generation);
    assert(!instrument_sound_snapshot(0, 0x123400, &handoff, &sound_generation) &&
           sound_generation == 7);
    cur->voicing = keyboard.voicing = 0;
    assert(instrument_sound_restore(0, 0x123400, sound_generation + 1, &handoff) < 0);
    assert(instrument_sound_restore(0, 0x999900, sound_generation, &handoff) < 0);
    ip_admission[6] = 1;
    assert(instrument_sound_restore(0, 0x123400, sound_generation, &handoff) < 0);
    ip_admission[6] = 0;
    assert(!instrument_sound_restore(0, 0x123400, sound_generation, &handoff) &&
           keyboard.voicing == 2 && !masked);
    cur->voicing = keyboard.voicing = 0;
    uint32_t view = instrument_src_context(0, 0, 1);
    assert(view && cur->pad_strum == 0);
    SpPool pool;
    sp_init(&pool, allocate, 0);
    Transaction t = {.pool = &pool};
    /* No reservation: neither the real control nor the attached handle changes. */
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 3, 16, 100, prepare, attach, &t) < 0);
    assert(cur->pad_strum == 0 && !t.attached && !t.attaches && !masked);
    assert(!sp_reserve_task(&pool, &t.reserved));
    int attempts = t.prepares;
    assert(instrument_sound_proof_edit(0, 0x999900, 7, view, 3, 16, 100, prepare, attach, &t) < 0);
    assert(instrument_sound_proof_edit(0, 0x123400, 8, view, 3, 16, 100, prepare, attach, &t) < 0);
    assert(t.prepares == attempts && cur->pad_strum == 0 && !masked);
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 3, 16, 100, prepare, attach, &t) ==
           CS_EDIT);
    assert(cur->pad_strum == 16 && t.attached == t.reserved && t.attaches == 1 && !masked);
    uint8_t bytes[SC_BYTES];
    CpPreset recorded;
    assert(!sp_read(&pool, t.attached, bytes) && !sc_decode(&recorded, bytes));
    assert(recorded.control[CP_STRUM] == 16);
    /* Discrete controls accumulate small turns without spending a reservation. */
    view = instrument_src_context(0, 0, 1);
    assert(!sp_reserve_task(&pool, &t.reserved));
    unsigned prior_attaches = (unsigned)t.attaches;
    for (unsigned turn = 0; turn < 3; ++turn)
        assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 2, 1, 100, prepare, attach, &t) ==
               CS_READONLY);
    assert((unsigned)t.attaches == prior_attaches && sp_read(&pool, t.reserved, bytes) < 0);
    assert(instrument_sound_proof_edit(0, 0x123400, 7, view, 2, 1, 100, prepare, attach, &t) ==
           CS_EDIT);
    assert(t.attached == t.reserved && !sp_read(&pool, t.attached, bytes));
    edit(&t, 0, 1, 4);    /* chord */
    edit(&t, 0, 2, 4);    /* voicing */
    edit(&t, 1, 0, 4);    /* arp divider */
    edit(&t, 1, 3, 7);    /* arp gate */
    edit(&t, 2, 0, 8);    /* switch to wavetable */
    edit(&t, 2, 1, 5);    /* wavetable position */
    edit(&t, 2, 6, 4);    /* WT clock also changes its rate */
    edit(&t, 2, 0, 20);   /* switch to FM */
    edit(&t, 2, 2, 4);    /* FM algorithm */
    edit(&t, 3, 0, 6);    /* FM attack */
    edit(&t, 0, 1, -100); /* return to PLAY */
    edit(&t, 0, 1, 4);    /* return to ROOT */
    /* A missing reservation refuses a chord selector while retaining the
     * simultaneous root note and all subsequent note-offs. */
    MiEvent in[2] = {key(100, 25, MI_NOTE_ON), key(200, 60, MI_NOTE_ON)}, out[8];
    uint8_t old_type = keyboard.type;
    uint16_t old_handle = t.attached;
    int refused = 0;
    t.reserved = 0;
    assert(midi(&t, in, 2, out, &refused) > 0 && refused && keyboard.type == old_type &&
           t.attached == old_handle);
    MiEvent release = key(200, 60, MI_NOTE_OFF);
    assert(midi(&t, &release, 1, out, &refused) > 0 && !refused);
    for (uint32_t n = 0; n < MI_KEYS; ++n)
        assert(keyboard.keys[n].token != 200 || !keyboard.keys[n].held);
    release = key(100, 25, MI_NOTE_OFF);
    assert(midi(&t, &release, 1, out, &refused) == 0 && !refused);
    /* With a reservation, the same selector updates the chord and the exact
     * selected chord in the attached snapshot while retaining note ownership. */
    assert(!sp_reserve_task(&pool, &t.reserved));
    in[0] = key(300, 25, MI_NOTE_ON);
    in[1] = key(400, 60, MI_NOTE_ON);
    assert(midi(&t, in, 2, out, &refused) > 0 && !refused && keyboard.type == 1 &&
           t.attached == t.reserved);
    assert(!sp_read(&pool, t.attached, bytes) && !sc_decode(&recorded, bytes));
    assert(recorded.control[CP_CHORD] == 1);
    release = key(400, 60, MI_NOTE_OFF);
    assert(midi(&t, &release, 1, out, &refused) > 0 && !refused);
    for (uint32_t n = 0; n < MI_KEYS; ++n)
        assert(keyboard.keys[n].token != 400 || !keyboard.keys[n].held);
    release = key(300, 25, MI_NOTE_OFF);
    assert(midi(&t, &release, 1, out, &refused) == 0 && !refused);
    /* A temporary step chord lock must not request a persistent snapshot. */
    MiEvent locked = key(500, 60, MI_NOTE_ON);
    locked.port = MI_PANEL;
    locked.lock_valid = 1;
    locked.lock_count = 1;
    locked.locks[0] = 3; /* chord lock ID 0, value 3 */
    int before_prepares = t.prepares;
    uint8_t before_type = keyboard.type;
    assert(midi(&t, &locked, 1, out, &refused) > 0 && !refused && t.prepares == before_prepares &&
           keyboard.type == before_type);
    locked = key(500, 60, MI_NOTE_OFF);
    locked.port = MI_PANEL;
    assert(midi(&t, &locked, 1, out, &refused) > 0 && !refused);
    /* A zero-interval note lock is discarded by the mapper and therefore
     * does not make a real selector change temporary. */
    assert(!sp_reserve_task(&pool, &t.reserved));
    locked = key(550, 26, MI_NOTE_ON);
    locked.port = MI_PANEL;
    locked.lock_valid = 1;
    locked.lock_count = 1;
    locked.locks[0] = (KB_LOCK_NOTE_FIRST << 10) | KB_LOCK_NOTE_CENTER;
    assert(midi(&t, &locked, 1, out, &refused) == 0 && !refused && keyboard.type == 2);
    assert(t.attached == t.reserved && !sp_read(&pool, t.attached, bytes) &&
           !sc_decode(&recorded, bytes));
    assert(recorded.control[CP_CHORD] == 2);
    locked = key(550, 26, MI_NOTE_OFF);
    locked.port = MI_PANEL;
    assert(midi(&t, &locked, 1, out, &refused) == 0 && !refused);
    /* A full batch must not require a second stack-sized copy, even when the
     * pool is exhausted. Every selector is silent, every root is accepted,
     * and a subsequent batch of note-offs still releases those roots. */
    MiEvent full[8];
    t.reserved = 0;
    before_type = keyboard.type;
    old_handle = t.attached;
    for (uint32_t n = 0; n < 4; ++n) {
        full[n] = key(600 + n, (uint8_t)(24 + n), MI_NOTE_ON);
        full[4 + n] = key(700 + n, (uint8_t)(61 + n), MI_NOTE_ON);
    }
    assert(midi(&t, full, 8, out, &refused) > 0 && refused);
    assert(keyboard.type == before_type && t.attached == old_handle);
    for (uint32_t n = 0; n < 4; ++n)
        full[n] = key(700 + n, (uint8_t)(61 + n), MI_NOTE_OFF);
    assert(midi(&t, full, 4, out, &refused) > 0 && !refused);
    for (uint32_t n = 0; n < MI_KEYS; ++n)
        assert(keyboard.keys[n].token < 700 || keyboard.keys[n].token >= 704 ||
               !keyboard.keys[n].held);
    /* A fresh zero-handle Sound gets the canonical SRC defaults, but cannot
     * reset a stale or loading admission. The shared USER library survives. */
    keyboard.shapes[0].count = 3;
    keyboard.shapes[0].intervals[0] = 0;
    keyboard.shapes[0].intervals[1] = 4;
    keyboard.shapes[0].intervals[2] = 7;
    keyboard.shapes[0].intervals[3] = 0;
    keyboard.preset_shape_active = 1;
    uint16_t prior_strum = (uint16_t)cur->pad_strum, marker = SP_PENDING_DEFAULT;
    assert(prior_strum &&
           instrument_sound_proof_defaults(0, 0x999900, 7, claim_default, &marker) < 0);
    assert(instrument_sound_proof_defaults(0, 0x123400, 8, claim_default, &marker) < 0 &&
           cur->pad_strum == prior_strum);
    ip_admission[6] = 1;
    assert(instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) < 0 &&
           cur->pad_strum == prior_strum);
    ip_admission[6] = 0;
    assert(marker == SP_PENDING_DEFAULT);
    assert(!instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) && !masked &&
           !marker);
    cur->pad_strum = 9;
    assert(instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) < 0 &&
           cur->pad_strum == 9);
    marker = SP_LOAD_ERROR;
    assert(instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) < 0 &&
           cur->pad_strum == 9);
    marker = SP_PENDING_DEFAULT;
    assert(!instrument_sound_proof_defaults(0, 0x123400, 7, claim_default, &marker) && !marker);
    assert(keyboard.mode == KB_PLAY && keyboard.slot == 0 && !keyboard.preset_shape_active);
    CpPreset reset;
    preset_snapshot(cur, &reset);
    CsState defaults;
    cs_init(&defaults);
    for (uint32_t i = 0; i < CP_CONTROLS; ++i)
        assert(reset.control[i] == defaults.value[i]);
    for (uint32_t i = 0; i < 13; ++i)
        assert(reset.fm[i] == defaults.fm[i]);
    assert(reset.shape.count == 3 && reset.shape.intervals[1] == 4 &&
           reset.shape.intervals[2] == 7);
    for (uint32_t i = 0; i < SP_SLABS; ++i)
        free(pool.slab[i]);
    return 0;
}
