/* Exercise the public encoder entry point with the registered, proof-only
 * immutable-snapshot transaction. Device ownership is a fixture. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#undef pad_family
#undef pad_color
#undef arp_bpm
#undef arp_mix
#undef arp_rate
#undef pad_dirty
#undef wt
#undef phase
#undef prepared
#undef src_state
#undef src_status
#include <assert.h>
#include "../../../runtime/platform/instrument-play/src/cf/sound-codec.h"
#include "../../../runtime/platform/instrument-play/src/cf/sound-pool.h"

typedef struct {
    uint32_t sound, generation;
    uint16_t handle, reserved, expected;
    uint8_t staged[SC_BYTES], initial[SC_BYTES];
    uint16_t initial_handle;
    unsigned prepares, attaches;
} LiveOwner;
typedef struct {
    SpPool pool;
    SpEntry slab[SP_PER_SLAB];
    LiveOwner owner[2];
    unsigned allocations, reentries;
    int probe_reentry, fail_after_prepare;
    uint32_t token;
} LiveTransaction;

static void *allocate(uint32_t size, void *context)
{
    LiveTransaction *tx = context;
    assert(!masked && size == sizeof(tx->slab));
    if (tx->allocations)
        return 0;
    ++tx->allocations;
    return tx->slab;
}
static void current_bytes(uint32_t i, uint8_t out[SC_BYTES])
{
    CpPreset p;
    preset_snapshot(&inst[i], &p);
    assert(!sc_encode(&p, out));
}
static int prepare_live(void *context, uint32_t i, uint32_t sound, uint32_t generation,
                        const CpPreset *p)
{
    LiveTransaction *tx = context;
    assert(masked && i < 2);
    LiveOwner *o = &tx->owner[i];
    ++o->prepares;
    if (tx->probe_reentry && !tx->reentries) {
        ++tx->reentries;
        assert(instrument_sound_proof_set_transactions(0, 0, 0) < 0);
        assert(instrument_src_encoder(tx->token, 7, 1, 101) == CS_STALE);
        assert(masked);
    }
    if (sound != o->sound || generation != o->generation || !o->reserved)
        return -1;
    assert(o->reserved <= tx->pool.count && !tx->slab[o->reserved - 1].ready);
    uint8_t live[SC_BYTES], stored[SC_BYTES];
    current_bytes(i, live);
    assert(!sp_read(&tx->pool, o->handle, stored) && !memcmp(live, stored, SC_BYTES));
    o->expected = o->handle;
    if (sc_encode(p, o->staged))
        return -1;
    return tx->fail_after_prepare ? -1 : 0;
}
static void attach_live(void *context, uint32_t i, uint32_t sound, uint32_t generation)
{
    LiveTransaction *tx = context;
    assert(masked && i < 2);
    LiveOwner *o = &tx->owner[i];
    assert(sound == o->sound && generation == o->generation && o->handle == o->expected &&
           o->reserved);
    uint8_t actual[SC_BYTES];
    current_bytes(i, actual);
    assert(!memcmp(actual, o->staged, SC_BYTES));
    assert(!sp_publish(&tx->pool, o->reserved, o->staged));
    o->handle = o->reserved;
    o->reserved = 0;
    ++o->attaches;
}
static void reserve_owner(LiveTransaction *tx, uint32_t i)
{
    assert(!masked && i < 2 && !tx->owner[i].reserved);
    assert(!sp_reserve_task(&tx->pool, &tx->owner[i].reserved));
}
static void setup(LiveTransaction *tx)
{
    memset(tx, 0, sizeof(*tx));
    ip_init();
    records_loaded = 1;
    sp_init(&tx->pool, allocate, tx);
    mi_ledger.epoch = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        IpInst *in = &inst[i];
        LiveOwner *o = &tx->owner[i];
        in->track = in->bound = ip_tracks[i] = i;
        in->phase = 3;
        in->prepared = 1;
        in->pad_color = 40 + (int)i;
        in->mode = KB_ROOT;
        in->type = 0;
        in->shape = (KbShape){3, {0, 4, 7, 0}};
        mi_ledger.generations[i] = 11 + i;
        o->sound = 0x123400 + i * 0x450;
        o->generation = 7 + i;
        ip_admission[8 * i] = 1;
        ip_admission[8 * i + 1] = o->sound;
        ip_admission[8 * i + 4] = o->generation;
        ip_admission[8 * i + 5] = admitted;
        current_bytes(i, o->initial);
        uint16_t reserved = 0;
        assert(!sp_reserve_task(&tx->pool, &reserved));
        uint32_t sr = dkrt_irq_mask();
        assert(!sp_publish(&tx->pool, reserved, o->initial));
        o->handle = o->initial_handle = reserved;
        dkrt_irq_restore(sr);
    }
    cur = &inst[0];
    selected = focus = 0;
    tx->token = instrument_src_context(0, 0, 1);
    assert(tx->token && !instrument_sound_proof_set_transactions(prepare_live, attach_live, tx));
}
/* An encoder of the VOICE page (0) or the DETAIL page (2). */
static int turn_on(LiveTransaction *tx, uint32_t page, uint32_t encoder, int32_t detents)
{
    tx->token = instrument_src_context(0, page, 1);
    return instrument_src_encoder(tx->token, encoder, detents, 100);
}
/* DRIFT, VOICE encoder 7 */
static int turn(LiveTransaction *tx, uint32_t encoder, int32_t detents)
{
    return turn_on(tx, 0, encoder, detents);
}
static void matches(const LiveTransaction *tx, uint32_t i)
{
    uint8_t current[SC_BYTES], stored[SC_BYTES];
    current_bytes(i, current);
    assert(!sp_read(&tx->pool, tx->owner[i].handle, stored) && !memcmp(current, stored, SC_BYTES));
    assert(!sp_read(&tx->pool, tx->owner[i].initial_handle, stored));
    assert(!memcmp(stored, tx->owner[i].initial, SC_BYTES));
    assert(tx->allocations == 1 && !masked && !unsafe_io);
}
static CpPreset read_preset(const LiveTransaction *tx, uint16_t handle)
{
    CpPreset p;
    uint8_t bytes[SC_BYTES];
    assert(!sp_read(&tx->pool, handle, bytes) && !sc_decode(&p, bytes));
    return p;
}
static void encoder_commit_and_refuse(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 7, 1) == CS_EDIT && inst[0].pad_drift == 1);
    uint16_t first = tx.owner[0].handle;
    assert(first != tx.owner[0].initial_handle && tx.owner[0].prepares == 1 &&
           tx.owner[0].attaches == 1);
    matches(&tx, 0);
    IpInst before;
    memcpy(&before, &inst[0], sizeof(before));
    uint32_t count = tx.pool.count;
    assert(turn(&tx, 7, 1) < 0 && !memcmp(&before, &inst[0], sizeof(before)));
    assert(tx.pool.count == count && tx.owner[0].handle == first && tx.owner[0].attaches == 1);
    matches(&tx, 0);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 7, 1) == CS_EDIT && inst[0].pad_drift == 2 && tx.owner[0].attaches == 2);
    assert(read_preset(&tx, first).control[CP_DRIFT] == 1);
    matches(&tx, 0);
}
static void native_and_list(void)
{
    LiveTransaction tx;
    setup(&tx);
    /* DETAIL: TUNE (A) and LEV (H) are the native cells. */
    assert(turn_on(&tx, 2, 0, 1) == CS_NATIVE && turn_on(&tx, 2, 7, 1) == CS_NATIVE);
    /* VOICE: WAVE (A) is a list of four detents per step. */
    for (uint32_t n = 0; n < 3; ++n)
        assert(turn(&tx, 0, 1) == CS_READONLY);
    assert(!tx.owner[0].prepares && src_state[0].list_delta[0] == 3);
    assert(turn(&tx, 0, 1) < 0 && tx.owner[0].prepares == 1 && !tx.owner[0].attaches);
    assert(inst[0].pad_family == 0 && src_state[0].list_delta[0] == 3);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 0, 1) == CS_EDIT && tx.owner[0].attaches == 1);
    assert(inst[0].pad_family == 1 && src_state[0].list_delta[0] == 0);
    matches(&tx, 0);
}
static void identity_and_prepared_refusal(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 0);
    uint16_t reserved = tx.owner[0].reserved, original = tx.owner[0].handle;
    ++tx.owner[0].generation;
    IpInst before;
    memcpy(&before, &inst[0], sizeof(before));
    assert(turn(&tx, 7, 1) < 0 && !memcmp(&before, &inst[0], sizeof(before)));
    assert(tx.owner[0].reserved == reserved && tx.owner[0].handle == original &&
           !tx.slab[reserved - 1].ready);
    --tx.owner[0].generation;
    tx.fail_after_prepare = 1;
    assert(turn(&tx, 7, 1) < 0 && !memcmp(&before, &inst[0], sizeof(before)));
    assert(tx.owner[0].reserved == reserved && !tx.slab[reserved - 1].ready &&
           !tx.owner[0].attaches);
    tx.fail_after_prepare = 0;
    assert(turn(&tx, 7, 1) == CS_EDIT && tx.owner[0].handle == reserved);
    matches(&tx, 0);
}
static void disabled_registration(void)
{
    LiveTransaction tx;
    setup(&tx);
    assert(instrument_sound_proof_set_transactions(prepare_live, 0, &tx) < 0);
    assert(turn(&tx, 7, 1) < 0 && tx.owner[0].prepares == 1);
    assert(!instrument_sound_proof_set_transactions(0, 0, 0));
    assert(turn(&tx, 7, 1) == CS_EDIT && inst[0].pad_drift == 1 && tx.owner[0].prepares == 1);
    assert(!tx.owner[0].attaches);
    CpPreset old = read_preset(&tx, tx.owner[0].initial_handle);
    assert(old.control[CP_DRIFT] == 0);
    assert(!masked && !unsafe_io);
}
static void reentry(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 0);
    tx.probe_reentry = 1;
    assert(turn(&tx, 7, 1) == CS_EDIT && tx.reentries == 1 && tx.owner[0].prepares == 1 &&
           tx.owner[0].attaches == 1);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 7, 1) == CS_EDIT && tx.owner[0].prepares == 2 && tx.owner[0].attaches == 2);
    matches(&tx, 0);
    matches(&tx, 1);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "encoder"))
        encoder_commit_and_refuse();
    else if (!strcmp(argv[1], "native"))
        native_and_list();
    else if (!strcmp(argv[1], "identity"))
        identity_and_prepared_refusal();
    else if (!strcmp(argv[1], "disabled"))
        disabled_registration();
    else if (!strcmp(argv[1], "reentry"))
        reentry();
    else
        return 2;
    return 0;
}
