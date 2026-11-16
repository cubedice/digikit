/* Exercise the public encoder and MIDI entry points with the registered,
 * proof-only immutable-snapshot transaction. Device ownership is a fixture. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#undef keyboard
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
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"

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
static MiEvent key(uint32_t track, uint32_t key_token, uint8_t note, uint8_t kind)
{
    MiEvent e = {0};
    e.epoch = mi_ledger.epoch;
    e.generation = mi_ledger.generations[track];
    e.token = key_token;
    e.kind = kind;
    e.note = note;
    e.value = 90;
    e.port = 0;
    e.channel = 0;
    e.track = (uint8_t)track;
    return e;
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
        assert(instrument_src_encoder(tx->token, 3, 1, 101) == CS_STALE);
        MiEvent input = key(1, 999, 60, MI_NOTE_ON), output[8];
        assert(instrument_keyboard_events(1, input.epoch, input.generation, &input, 1, output) < 0);
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
    keyboard_initialized = 1;
    sp_init(&tx->pool, allocate, tx);
    mi_ledger.epoch = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        IpInst *in = &inst[i];
        LiveOwner *o = &tx->owner[i];
        in->track = in->bound = ip_tracks[i] = i;
        in->phase = 3;
        in->prepared = 1;
        in->pad_color = 40 + (int)i;
        in->mode = in->keyboard.mode = KB_ROOT;
        in->type = in->keyboard.type = 0;
        in->keyboard.shapes[0] = (KbShape){3, {0, 4, 7, 0}};
        in->keyboard.selector_base = 24;
        in->keyboard.selector_channel = MI_NONE;
        mi_ledger.generations[i] = 11 + i;
        kb_reset(&in->keyboard, mi_ledger.epoch, mi_ledger.generations[i]);
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
static int turn(LiveTransaction *tx, uint32_t encoder, int32_t detents)
{
    tx->token = instrument_src_context(0, 0, 1);
    return instrument_src_encoder(tx->token, encoder, detents, 100);
}
static int midi(uint32_t track, const MiEvent *input, uint32_t count, MiEvent output[8])
{
    int result = instrument_keyboard_events(track, mi_ledger.epoch, mi_ledger.generations[track],
                                            input, count, output);
    assert(!masked && !unsafe_io);
    for (int n = 0; n < result; ++n)
        assert(output[n].track == track);
    return result;
}
static void release(uint32_t track, uint32_t key_token, uint8_t note)
{
    MiEvent input = key(track, key_token, note, MI_NOTE_OFF), output[8];
    assert(midi(track, &input, 1, output) >= 0);
    for (uint32_t n = 0; n < MI_KEYS; ++n)
        assert(inst[track].keyboard.keys[n].token != key_token ||
               !inst[track].keyboard.keys[n].held);
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
    assert(turn(&tx, 3, 1) == CS_EDIT && inst[0].pad_strum == 1);
    uint16_t first = tx.owner[0].handle;
    assert(first != tx.owner[0].initial_handle && tx.owner[0].prepares == 1 &&
           tx.owner[0].attaches == 1);
    matches(&tx, 0);
    IpInst before;
    memcpy(&before, &inst[0], sizeof(before));
    uint32_t count = tx.pool.count;
    assert(turn(&tx, 3, 1) < 0 && !memcmp(&before, &inst[0], sizeof(before)));
    assert(tx.pool.count == count && tx.owner[0].handle == first && tx.owner[0].attaches == 1);
    matches(&tx, 0);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 3, 1) == CS_EDIT && inst[0].pad_strum == 2 && tx.owner[0].attaches == 2);
    assert(read_preset(&tx, first).control[CP_STRUM] == 1);
    matches(&tx, 0);
}
static void native_and_list(void)
{
    LiveTransaction tx;
    setup(&tx);
    assert(turn(&tx, 0, 1) == CS_NATIVE && turn(&tx, 7, 1) == CS_NATIVE);
    for (uint32_t n = 0; n < 3; ++n)
        assert(turn(&tx, 1, 1) == CS_READONLY);
    assert(!tx.owner[0].prepares && src_state[0].list_delta[1] == 3);
    assert(turn(&tx, 1, 1) < 0 && tx.owner[0].prepares == 1 && !tx.owner[0].attaches);
    assert(inst[0].keyboard.type == 0 && src_state[0].list_delta[1] == 3);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 1, 1) == CS_EDIT && tx.owner[0].attaches == 1);
    assert(inst[0].keyboard.type == 1 && src_state[0].list_delta[1] == 0);
    matches(&tx, 0);
}
static void midi_refusal_and_retry(void)
{
    LiveTransaction tx;
    setup(&tx);
    MiEvent events[2] = {key(0, 100, 26, MI_NOTE_ON), key(0, 200, 60, MI_NOTE_ON)}, output[8];
    assert(midi(0, events, 2, output) > 0 && inst[0].keyboard.type == 0);
    assert(tx.owner[0].prepares == 1 && !tx.owner[0].attaches);
    release(0, 200, 60);
    release(0, 100, 26);
    matches(&tx, 0);
    reserve_owner(&tx, 0);
    events[0] = key(0, 300, 26, MI_NOTE_ON);
    events[1] = key(0, 400, 62, MI_NOTE_ON);
    assert(midi(0, events, 2, output) > 0 && inst[0].keyboard.type == 2);
    assert(tx.owner[0].attaches == 1 && !tx.owner[0].reserved);
    release(0, 400, 62);
    release(0, 300, 26);
    assert(tx.owner[0].prepares == 2);
    matches(&tx, 0);
}
static void different_track(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 1);
    MiEvent events[2] = {key(1, 100, 25, MI_NOTE_ON), key(1, 200, 60, MI_NOTE_ON)}, output[8];
    assert(midi(1, events, 2, output) > 0);
    assert(cur == &inst[0] && inst[0].keyboard.type == 0 && inst[1].keyboard.type == 1);
    assert(!tx.owner[0].prepares && !tx.owner[0].attaches && tx.owner[1].attaches == 1);
    release(1, 200, 60);
    release(1, 100, 25);
    matches(&tx, 0);
    matches(&tx, 1);
}
static void identity_and_prepared_refusal(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 0);
    uint16_t reserved = tx.owner[0].reserved, original = tx.owner[0].handle;
    ++tx.owner[0].generation;
    MiEvent events[2] = {key(0, 100, 25, MI_NOTE_ON), key(0, 200, 60, MI_NOTE_ON)}, output[8];
    assert(midi(0, events, 2, output) > 0 && inst[0].keyboard.type == 0);
    release(0, 200, 60);
    release(0, 100, 25);
    assert(tx.owner[0].reserved == reserved && tx.owner[0].handle == original &&
           !tx.slab[reserved - 1].ready);
    --tx.owner[0].generation;
    tx.fail_after_prepare = 1;
    IpInst before;
    memcpy(&before, &inst[0], sizeof(before));
    assert(turn(&tx, 3, 1) < 0 && !memcmp(&before, &inst[0], sizeof(before)));
    assert(tx.owner[0].reserved == reserved && !tx.slab[reserved - 1].ready &&
           !tx.owner[0].attaches);
    tx.fail_after_prepare = 0;
    assert(turn(&tx, 3, 1) == CS_EDIT && tx.owner[0].handle == reserved);
    matches(&tx, 0);
}
static void unavailable_admission(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 0);
    ip_admission[0] = 0;
    MiEvent events[2] = {key(0, 100, 25, MI_NOTE_ON), key(0, 200, 60, MI_NOTE_ON)}, output[8];
    assert(midi(0, events, 2, output) > 0 && inst[0].keyboard.type == 0);
    release(0, 200, 60);
    release(0, 100, 25);
    assert(turn(&tx, 3, 1) < 0 && !tx.owner[0].prepares && !tx.owner[0].attaches);
    ip_admission[0] = 1;
    inst[0].bound = 1;
    events[0] = key(0, 300, 25, MI_NOTE_ON);
    events[1] = key(0, 400, 62, MI_NOTE_ON);
    assert(midi(0, events, 2, output) > 0 && inst[0].keyboard.type == 0);
    release(0, 400, 62);
    release(0, 300, 25);
    assert(!tx.owner[0].prepares && !tx.owner[0].attaches);
    inst[0].bound = 0;
    matches(&tx, 0);
}
static void learn_and_unmapped(void)
{
    LiveTransaction tx;
    setup(&tx);
    MiEvent input = key(7, 100, 60, MI_NOTE_ON), output[8];
    assert(midi(7, &input, 1, output) == 1 && output[0].token == input.token &&
           output[0].note == 60);
    learn_begin(0, 0);
    input = key(0, 200, 72, MI_NOTE_ON);
    assert(midi(0, &input, 1, output) >= 0);
    assert(inst[0].keyboard.mode == KB_LEARN && inst[0].keyboard.capture_count == 1 &&
           inst[0].keyboard.capture[0] == 72 && !tx.owner[0].prepares);
    learn_close(0, 0);
    matches(&tx, 0);
}
static void disabled_registration(void)
{
    LiveTransaction tx;
    setup(&tx);
    assert(instrument_sound_proof_set_transactions(prepare_live, 0, &tx) < 0);
    assert(turn(&tx, 3, 1) < 0 && tx.owner[0].prepares == 1);
    assert(!instrument_sound_proof_set_transactions(0, 0, 0));
    assert(turn(&tx, 3, 1) == CS_EDIT && inst[0].pad_strum == 1 && tx.owner[0].prepares == 1);
    MiEvent events[2] = {key(0, 100, 27, MI_NOTE_ON), key(0, 200, 60, MI_NOTE_ON)}, output[8];
    assert(midi(0, events, 2, output) > 0 && inst[0].keyboard.type == 3);
    release(0, 200, 60);
    release(0, 100, 27);
    assert(tx.owner[0].prepares == 1 && !tx.owner[0].attaches);
    CpPreset old = read_preset(&tx, tx.owner[0].initial_handle);
    assert(old.control[CP_STRUM] == 0 && old.control[CP_CHORD] == 0);
    assert(!masked && !unsafe_io);
}
static void reentry(void)
{
    LiveTransaction tx;
    setup(&tx);
    reserve_owner(&tx, 0);
    tx.probe_reentry = 1;
    assert(turn(&tx, 3, 1) == CS_EDIT && tx.reentries == 1 && tx.owner[0].prepares == 1 &&
           tx.owner[0].attaches == 1);
    for (uint32_t n = 0; n < MI_KEYS; ++n)
        assert(inst[1].keyboard.keys[n].token != 999);
    reserve_owner(&tx, 0);
    assert(turn(&tx, 3, 1) == CS_EDIT && tx.owner[0].prepares == 2 && tx.owner[0].attaches == 2);
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
    else if (!strcmp(argv[1], "midi"))
        midi_refusal_and_retry();
    else if (!strcmp(argv[1], "track"))
        different_track();
    else if (!strcmp(argv[1], "identity"))
        identity_and_prepared_refusal();
    else if (!strcmp(argv[1], "admission"))
        unavailable_admission();
    else if (!strcmp(argv[1], "learn"))
        learn_and_unmapped();
    else if (!strcmp(argv[1], "disabled"))
        disabled_registration();
    else if (!strcmp(argv[1], "reentry"))
        reentry();
    else
        return 2;
    return 0;
}
