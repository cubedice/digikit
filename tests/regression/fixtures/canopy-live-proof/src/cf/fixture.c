/* Disposable DTII emulator proof. Handles belong to this fixture, never a
 * stock Sound field. The separate sound-proof dependency supplies only its
 * existing UI tick trampoline and codec/pool symbols. This is not persistence. */
#include <stdint.h>
#include "../../../canopy-two-phase/credit-refill.h"
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/instances.h"
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/sound-state.h"
#include "../../../../../../modules/core/adapters/midi-events/src/cf/bridge.h"
extern int32_t dkrt_job_post(uintptr_t, uintptr_t, uint8_t *);
extern uint8_t *fw_alloc(uint32_t);
extern void fw_free(uint8_t *);
extern uint32_t tp_probe_sr(void);
extern uint32_t tp_probe_mask(void *);
extern void tp_probe_restore(void *, uint32_t);
extern int instrument_keyboard_events(uint32_t, uint32_t, uint32_t, const MiEvent *, uint32_t,
                                      MiEvent *);

/* 0 command (1 init, 2 refill, 3 mapped note pair, 4 disable), 1 MIDI track,
 * 2 result, 3 command completions, 4 registered, 5 job ID, 6 job completions,
 * 7 job result, 8 allocations, 9 allocator SR, 10 masked allocations,
 * 11 worker SR, 12 worker TCB, 13 UI TCB, 14 errors, 15 pool count.
 * 16..23 and 24..31 per track: prepares, attaches, refusals, current handle,
 * credit, current chord, current strum, current matches immutable snapshot.
 * 32 MIDI note-ons, 33 note-offs, 34 release token/pitch matches, 35 old
 * snapshots checked, 36 allocation fail injection, 37 active job.
 * 38 initialization refusal (1 repeated, 2 missing track, 3 snapshot, 4 mode,
 * 5 raw generation exhausted, 6 credit bind), 39 failing track.
 * 40..47 / 48..55 initialization observation per track: instance, Sound,
 * raw generation, mode, admission, active loads, type, credit generation. */
volatile uint32_t canopy_live_test[56];
static SpPool pool;
static BcCredits credits;
static CreditRefill refill;
static volatile uint32_t task_guard;
static uint8_t initialized, seeding, job_active;
typedef struct {
    uint32_t instance, sound, generation, credit_generation;
    uint16_t handle, expected, credit;
    uint8_t staged[SC_BYTES];
} LiveOwner;
static LiveOwner owner[2];
static uint16_t archive_handle[16];
static uint8_t archive_bytes[16][SC_BYTES];
static uint32_t archive_count, next_token = 0x4000;
static MiEvent input[2], output[8], started[4];
static int equal(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static void copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        ((volatile uint8_t *)d)[i] = s[i];
}
static int task_ready(void *x)
{
    (void)x;
    return !(tp_probe_sr() & 0x700u);
}
static int enter(void *x)
{
    (void)x;
    uint32_t sr = tp_probe_mask(0), busy = task_guard;
    if (!busy)
        task_guard = 1;
    tp_probe_restore(0, sr);
    return busy ? -1 : 0;
}
static void leave(void *x)
{
    (void)x;
    uint32_t sr = tp_probe_mask(0);
    task_guard = 0;
    tp_probe_restore(0, sr);
}
static TpSync sync = {task_ready, enter, leave, tp_probe_mask, tp_probe_restore, 0};
static void *allocate(uint32_t size, void *x)
{
    (void)x;
    uint32_t sr = tp_probe_sr();
    canopy_live_test[9] = sr;
    ++canopy_live_test[8];
    if (sr & 0x700u) {
        ++canopy_live_test[10];
        return 0;
    }
    if (canopy_live_test[36])
        return 0;
    return fw_alloc(size);
}
static void release(void *p, void *x)
{
    (void)x;
    if (tp_probe_sr() & 0x700u) {
        ++canopy_live_test[10];
        return;
    }
    fw_free((uint8_t *)p);
}
static int32_t step(uint8_t *x)
{
    (void)x;
    canopy_live_test[11] = tp_probe_sr();
    /* Same measured DTII 1.17 current-TCB word as worker-refill. */
    canopy_live_test[12] = *(volatile const uint32_t *)(uintptr_t)0x47db3f64u;
    return cr_replenish(&refill);
}
static void done(uint8_t *x, int32_t status)
{
    (void)x;
    uint32_t sr = tp_probe_mask(0);
    canopy_live_test[7] = (uint32_t)status;
    ++canopy_live_test[6];
    job_active = 0;
    canopy_live_test[37] = 0;
    tp_probe_restore(0, sr);
}
static int post(void)
{
    uint32_t sr = tp_probe_mask(0);
    if (job_active) {
        tp_probe_restore(0, sr);
        return -1;
    }
    job_active = 1;
    canopy_live_test[37] = 1;
    tp_probe_restore(0, sr);
    int32_t id = dkrt_job_post((uintptr_t)step, (uintptr_t)done, 0);
    canopy_live_test[5] = (uint32_t)id;
    if (id < 0) {
        sr = tp_probe_mask(0);
        job_active = 0;
        canopy_live_test[37] = 0;
        tp_probe_restore(0, sr);
    }
    return id < 0 ? -1 : 0;
}
static int slot_for(uint32_t instance)
{
    for (uint32_t t = 0; t < 2; ++t)
        if (owner[t].instance == instance)
            return (int)t;
    return -1;
}
static int snapshot(uint32_t track, uint8_t bytes[SC_BYTES], CpPreset *p)
{
    LiveOwner *o = &owner[track];
    uint32_t generation;
    /* The same instance index can be rebound. Verify the original track in
     * addition to the real Sound pointer and raw admission generation. */
    return o->instance >= IP_INSTANCES || ip_tracks[o->instance] != track ||
           instrument_sound_snapshot(o->instance, o->sound, p, &generation) ||
           generation != o->generation || sc_encode(p, bytes);
}
static void archive(uint16_t handle, const uint8_t *bytes)
{
    if (archive_count >= 16) {
        ++canopy_live_test[14];
        return;
    }
    archive_handle[archive_count] = handle;
    copy(archive_bytes[archive_count], bytes, SC_BYTES);
    ++archive_count;
}
static int prepare(void *x, uint32_t i, uint32_t sound, uint32_t generation, const CpPreset *p)
{
    (void)x;
    int t = slot_for(i);
    if (t < 0)
        return -1;
    uint32_t base = 16u + 8u * (uint32_t)t;
    ++canopy_live_test[base];
    LiveOwner *o = &owner[t];
    uint8_t bytes[SC_BYTES], stored[SC_BYTES];
    CpPreset current;
    if (!(tp_probe_sr() & 0x700u)) {
        ++canopy_live_test[14];
        return -1;
    }
    if (sound != o->sound || generation != o->generation || archive_count >= 16 ||
        bc_can_edit(&credits, (uint32_t)t, sound, o->credit_generation) ||
        snapshot((uint32_t)t, bytes, &current) || sp_read(&pool, o->handle, stored) ||
        !equal(bytes, stored, SC_BYTES) || sc_encode(p, o->staged)) {
        ++canopy_live_test[base + 2];
        return -1;
    }
    o->expected = o->handle;
    o->credit = credits.slot[t].credit;
    return 0;
}
static void attach(void *x, uint32_t i, uint32_t sound, uint32_t generation)
{
    (void)x;
    int t = slot_for(i);
    if (t < 0) {
        ++canopy_live_test[14];
        return;
    }
    LiveOwner *o = &owner[t];
    CpPreset current;
    uint8_t bytes[SC_BYTES];
    if (!(tp_probe_sr() & 0x700u) || sound != o->sound || generation != o->generation ||
        o->handle != o->expected || o->credit != credits.slot[t].credit ||
        snapshot((uint32_t)t, bytes, &current) || !equal(bytes, o->staged, SC_BYTES) ||
        bc_edit_committed(&credits, (uint32_t)t, sound, o->credit_generation) ||
        bc_capture(&credits, (uint32_t)t, sound, o->credit_generation, o->staged, &o->handle)) {
        ++canopy_live_test[14];
        return;
    }
    archive(o->handle, o->staged);
    ++canopy_live_test[17u + 8u * (uint32_t)t];
}
static int initialize(void)
{
    canopy_live_test[38] = 0;
    canopy_live_test[39] = 0xffffffffu;
    if (initialized) {
        canopy_live_test[38] = 1;
        return -1;
    }
    for (uint32_t t = 0; t < 2; ++t) {
        canopy_live_test[39] = t;
        uint32_t base = 40 + 8 * t;
        uint32_t i = 0;
        while (i < IP_INSTANCES && ip_tracks[i] != t)
            ++i;
        canopy_live_test[base] = i;
        if (i == IP_INSTANCES) {
            canopy_live_test[38] = 2;
            return -1;
        }
        owner[t].instance = i;
        owner[t].sound = ip_admission[8 * i + 1];
        owner[t].generation = ip_admission[8 * i + 4];
        canopy_live_test[base + 1] = owner[t].sound;
        canopy_live_test[base + 2] = owner[t].generation;
        canopy_live_test[base + 3] = 0xffffffffu;
        canopy_live_test[base + 4] = ip_admission[8 * i];
        canopy_live_test[base + 5] = ip_admission[8 * i + 6];
        canopy_live_test[base + 6] = ip_admission[8 * i + 5];
        /* Admission counts invalidations and valid first admission is zero.
         * BcCredits reserves zero for unbound: map only its fixture-local
         * generation, retaining the raw value for every ownership check. */
        if (owner[t].generation == UINT32_MAX) {
            canopy_live_test[38] = 5;
            return -1;
        }
        owner[t].credit_generation = owner[t].generation + 1;
        canopy_live_test[base + 7] = owner[t].credit_generation;
        CpPreset p;
        uint8_t bytes[SC_BYTES];
        if (snapshot(t, bytes, &p)) {
            canopy_live_test[38] = 3;
            return -1;
        }
        canopy_live_test[base + 3] = p.mode;
        if (p.mode != KB_ROOT) {
            canopy_live_test[38] = 4;
            return -1;
        }
    }
    sp_init(&pool, allocate, 0);
    sp_set_deallocator(&pool, release);
    bc_init(&credits, &pool);
    cr_init(&refill, &credits, &sync);
    for (uint32_t t = 0; t < 2; ++t)
        if (bc_bind(&credits, t, owner[t].sound, owner[t].credit_generation)) {
            canopy_live_test[38] = 6;
            canopy_live_test[39] = t;
            return -1;
        }
    canopy_live_test[39] = 0xffffffffu;
    initialized = 1;
    seeding = 1;
    return 0;
}
static int seed(void)
{
    /* The worker reserved two entries. Stage both current Sounds before
     * publishing either; source addresses and generations must still match. */
    for (uint32_t t = 0; t < 2; ++t) {
        CpPreset p;
        if (bc_can_edit(&credits, t, owner[t].sound, owner[t].credit_generation) ||
            snapshot(t, owner[t].staged, &p))
            return -1;
    }
    for (uint32_t t = 0; t < 2; ++t) {
        LiveOwner *o = &owner[t];
        if (bc_edit_committed(&credits, t, o->sound, o->credit_generation) ||
            bc_capture(&credits, t, o->sound, o->credit_generation, o->staged, &o->handle))
            return -1;
        archive(o->handle, o->staged);
    }
    if (instrument_sound_proof_set_transactions(prepare, attach, 0))
        return -1;
    canopy_live_test[4] = 1;
    seeding = 0;
    return 0;
}
static void event(MiEvent *e, uint32_t track, uint32_t token, uint32_t note, uint32_t kind)
{
    for (uint32_t i = 0; i < sizeof(*e); ++i)
        ((volatile uint8_t *)e)[i] = 0;
    e->epoch = mi_ledger.epoch;
    e->generation = mi_ledger.generations[track];
    e->token = token;
    e->kind = (uint8_t)kind;
    e->note = (uint8_t)note;
    e->value = 90;
    e->track = (uint8_t)track;
}
static int mapped_notes(uint32_t track)
{
    if (track > 1 || !canopy_live_test[4])
        return -1;
    CpPreset p;
    uint8_t bytes[SC_BYTES];
    if (snapshot(track, bytes, &p))
        return -1;
    uint32_t selector = 24u + (p.control[CP_CHORD] + 1u) % 8u, token = next_token;
    next_token += 2;
    event(&input[0], track, token, selector, MI_NOTE_ON);
    event(&input[1], track, token + 1, 60u + track, MI_NOTE_ON);
    int n = instrument_keyboard_events(track, mi_ledger.epoch, mi_ledger.generations[track], input,
                                       2, output);
    uint32_t on = 0, off = 0, matches = 0;
    if (n < 0)
        return -1;
    for (int i = 0; i < n; ++i)
        if (output[i].kind == MI_NOTE_ON) {
            if (on >= 4 || output[i].track != track)
                return -1;
            mi_event_copy(&started[on++], &output[i]);
        }
    event(&input[0], track, token + 1, 60u + track, MI_NOTE_OFF);
    event(&input[1], track, token, selector, MI_NOTE_OFF);
    n = instrument_keyboard_events(track, mi_ledger.epoch, mi_ledger.generations[track], input, 2,
                                   output);
    if (n < 0)
        return -1;
    uint32_t seen = 0;
    for (int i = 0; i < n; ++i)
        if (output[i].kind == MI_NOTE_OFF) {
            ++off;
            for (uint32_t j = 0; j < on; ++j)
                if (!(seen & (1u << j)) && output[i].token == started[j].token &&
                    output[i].note == started[j].note && output[i].track == track) {
                    ++matches;
                    seen |= 1u << j;
                }
        }
    canopy_live_test[32] = on;
    canopy_live_test[33] = off;
    canopy_live_test[34] = matches;
    return on && on == off && on == matches ? 0 : -1;
}
static void inspect(void)
{
    canopy_live_test[15] = pool.count;
    for (uint32_t t = 0; t < 2; ++t) {
        uint32_t base = 16 + 8 * t;
        CpPreset p;
        uint8_t bytes[SC_BYTES], stored[SC_BYTES];
        canopy_live_test[base + 3] = owner[t].handle;
        canopy_live_test[base + 4] = credits.slot[t].credit;
        if (snapshot(t, bytes, &p)) {
            ++canopy_live_test[14];
            continue;
        }
        canopy_live_test[base + 5] = p.control[CP_CHORD];
        canopy_live_test[base + 6] = p.control[CP_STRUM];
        canopy_live_test[base + 7] =
            !sp_read(&pool, owner[t].handle, stored) && equal(bytes, stored, SC_BYTES);
    }
    uint32_t good = 0;
    uint8_t stored[SC_BYTES];
    for (uint32_t n = 0; n < archive_count; ++n)
        if (!sp_read(&pool, archive_handle[n], stored) && equal(stored, archive_bytes[n], SC_BYTES))
            ++good;
    canopy_live_test[35] = good;
    if (good != archive_count)
        ++canopy_live_test[14];
}
void canopy_live_tick(void)
{
    canopy_live_test[13] = *(volatile const uint32_t *)(uintptr_t)0x47db3f64u;
    uint32_t cmd = canopy_live_test[0];
    canopy_live_test[0] = 0;
    if (cmd) {
        int result = -1;
        uint32_t sr = tp_probe_mask(0);
        if (cmd == 1)
            result = initialize();
        else if (cmd == 2 && initialized)
            result = 0;
        else if (cmd == 3)
            result = mapped_notes(canopy_live_test[1]);
        else if (cmd == 4) {
            result = instrument_sound_proof_set_transactions(0, 0, 0);
            if (!result)
                canopy_live_test[4] = 0;
        }
        tp_probe_restore(0, sr);
        if (!result && (cmd == 1 || cmd == 2))
            result = post();
        canopy_live_test[2] = (uint32_t)result;
        ++canopy_live_test[3];
    }
    if (initialized) {
        uint32_t sr = tp_probe_mask(0);
        if (seeding && !job_active && canopy_live_test[6] && canopy_live_test[7] == 0 && seed())
            ++canopy_live_test[14];
        if (!seeding)
            inspect();
        tp_probe_restore(0, sr);
    }
}
