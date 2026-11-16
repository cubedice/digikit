/* Host-only proof of the staged SHAPE SLOT menu edit. It uses the actual
 * changed() callback; fixture Sound identities do not model device lifetimes. */
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
#include <limits.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-share-pool.h"

/* Keep production keyboard.c linked; these unrelated selector-proof methods
 * must never be invoked by a SHAPE SLOT callback. */
int kb_selector_candidate(const KbState *s, const MiEvent *input, uint32_t count, uint32_t *type)
{
    (void)s;
    (void)input;
    (void)count;
    (void)type;
    assert(!"selector proof is outside the shape-slot test");
    return -1;
}
int kb_process_locked_selector(KbState *s, const MiEvent *input, uint32_t count, MiEvent output[8],
                               int allow)
{
    (void)s;
    (void)input;
    (void)count;
    (void)output;
    (void)allow;
    assert(!"selector proof is outside the shape-slot test");
    return -1;
}
typedef struct {
    SsPool pool;
    SsEntry entries[2];
    uint16_t handle, expected, reserved;
    uint8_t before[SC_BYTES], staged[SC_BYTES];
    int prepares, attaches, fail_after_prepare, probe_reentry, reentries;
} ShapeTransaction;
static void bytes_current(uint8_t out[SC_BYTES])
{
    CpPreset p;
    preset_snapshot(&inst[0], &p);
    assert(!sc_encode(&p, out));
}
static void attach_shape(void *context);
static int prepare_shape(void *context, const CpPreset *p)
{
    ShapeTransaction *tx = context;
    uint8_t actual[SC_BYTES];
    assert(masked && dk_ui_menu_is_open(shape_menu));
    assert(inst[0].slot == (int)inst[0].keyboard.slot + 1);
    bytes_current(actual);
    assert(!memcmp(actual, tx->before, sizeof(actual)));
    ++tx->prepares;
    if (tx->probe_reentry && !tx->reentries) {
        ++tx->reentries;
        assert(instrument_sound_proof_shape_slot(0, 0x123400, 7, 8, prepare_shape, attach_shape,
                                                 tx) < 0);
        assert(masked);
    }
    if (ss_can_edit(&tx->pool, &tx->handle, 0x100, tx->expected, tx->reserved))
        return -1;
    if (sc_encode(p, tx->staged))
        return -1;
    return tx->fail_after_prepare ? -1 : 0;
}
static void attach_shape(void *context)
{
    ShapeTransaction *tx = context;
    uint8_t actual[SC_BYTES];
    assert(masked && tx->prepares == 1 && dk_ui_menu_is_open(shape_menu));
    bytes_current(actual);
    assert(!memcmp(actual, tx->staged, sizeof(actual)));
    assert(!ss_edit(&tx->pool, &tx->handle, 0x100, tx->expected, tx->reserved, tx->staged));
    if (tx->handle != tx->expected)
        tx->reserved = 0;
    ++tx->attaches;
}
static void setup(ShapeTransaction *tx, uint32_t capacity)
{
    memset(tx, 0, sizeof(*tx));
    ip_init();
    keyboard_initialized = 1;
    cur = &inst[0];
    selected = focus = 0;
    mi_ledger.epoch = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        IpInst *in = &inst[i];
        in->track = in->bound = ip_tracks[i] = i;
        in->phase = 3;
        in->prepared = 1;
        in->mode = in->keyboard.mode = KB_ROOT;
        in->type = in->keyboard.type = 10;
        in->voicing = in->keyboard.voicing = 1;
        in->slot = 1;
        in->keyboard.slot = 0;
        for (uint32_t n = 0; n < 8; ++n)
            in->keyboard.shapes[n] = (KbShape){3, {0, 4, 7, 0}};
        in->keyboard.shapes[1] = (KbShape){3, {0, 3, 7, 0}};
        in->keyboard.shapes[7] = (KbShape){3, {0, 5, 9, 0}};
        mi_ledger.generations[i] = 11 + i;
        ip_admission[8 * i] = 1;
        ip_admission[8 * i + 1] = 0x123400 + i * 0x450;
        ip_admission[8 * i + 4] = 7 + i;
        ip_admission[8 * i + 5] = admitted;
    }
    cur->keyboard.preset_shape = (KbShape){3, {0, 7, 12, 0}};
    cur->keyboard.preset_shape_active = 1;
    selector_channel = 1;
    selector_octave = 2;
    route_restore = 1;
    uint8_t bytes[SC_BYTES];
    uint16_t reserved = 0;
    bytes_current(bytes);
    assert(!ss_init(&tx->pool, tx->entries, capacity) && !ss_reserve(&tx->pool, &reserved));
    assert(!ss_create(&tx->pool, reserved, 0x100, &tx->handle, bytes));
    shape_menu = dk_ui_menu_new(ui(), &shape_desc);
    assert(shape_menu && !dk_ui_menu_open(shape_menu));
}
static int select_slot(ShapeTransaction *tx, int slot)
{
    assert(!masked);
    tx->expected = tx->handle;
    tx->prepares = tx->attaches = 0;
    bytes_current(tx->before);
    inst[0].slot = slot; /* Real dk_ui_value writes its signed backing first. */
    int result =
        instrument_sound_proof_shape_slot(0, 0x123400, 7, slot, prepare_shape, attach_shape, tx);
    assert(!masked && dk_ui_menu_is_open(shape_menu));
    /* No refresh can repair this backing: shape_fill points at the same int.
     * The transaction itself must already have accepted or restored it. */
    assert(inst[0].slot == (int)inst[0].keyboard.slot + 1);
    return result;
}
static void matches(const ShapeTransaction *tx)
{
    uint8_t actual[SC_BYTES], stored[SC_BYTES];
    bytes_current(actual);
    assert(!ss_read(&tx->pool, tx->handle, stored) && !memcmp(actual, stored, sizeof(actual)));
    assert(!masked && !unsafe_io);
}
static uint16_t freeze(ShapeTransaction *tx, int reserve)
{
    uint32_t sr = dkrt_irq_mask();
    assert(!ss_share(&tx->pool, tx->handle, 0x100));
    uint16_t copy = tx->handle;
    dkrt_irq_restore(sr);
    if (reserve)
        assert(!ss_reserve(&tx->pool, &tx->reserved));
    return copy;
}
typedef struct {
    IpInst instances[IP_INSTANCES];
    MiLedger ledger;
    SsEntry entries[2];
    uint16_t handle, reserved;
    uint32_t count;
    int restore, selector;
} UnchangedShape;
static void remember(const ShapeTransaction *tx, UnchangedShape *before)
{
    memcpy(before->instances, inst, sizeof(inst));
    memcpy(&before->ledger, &mi_ledger, sizeof(mi_ledger));
    memcpy(before->entries, tx->entries, sizeof(tx->entries));
    before->handle = tx->handle;
    before->reserved = tx->reserved;
    before->count = tx->pool.count;
    before->restore = route_restore;
    before->selector = selector_channel;
}
static void unchanged(const ShapeTransaction *tx, const UnchangedShape *before)
{
    assert(!memcmp(before->instances, inst, sizeof(inst)));
    assert(!memcmp(&before->ledger, &mi_ledger, sizeof(mi_ledger)));
    assert(!memcmp(before->entries, tx->entries, sizeof(tx->entries)));
    assert(before->handle == tx->handle && before->reserved == tx->reserved &&
           before->count == tx->pool.count);
    assert(before->restore == route_restore && before->selector == selector_channel &&
           !tx->attaches);
    matches(tx);
}
static void callback_effects(void)
{
    assert(route_restore == 0 && selector_channel == 0);
    for (uint32_t i = 0; i < 2; ++i) {
        assert(mi_ledger.generations[i] == 12 + i);
        assert(inst[i].phase == 1 && !inst[i].prepared);
    }
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        assert(inst[i].keyboard.selector_base == 36 &&
               inst[i].keyboard.selector_channel == MI_NONE);
}
static void select_changed_slot(void)
{
    ShapeTransaction tx;
    setup(&tx, 2);
    uint8_t original[SC_BYTES], copied[SC_BYTES];
    bytes_current(original);
    KbShape library[8];
    memcpy(library, cur->keyboard.shapes, sizeof(library));
    uint16_t copy = freeze(&tx, 1), reservation = tx.reserved;
    assert(!select_slot(&tx, 2) && tx.prepares == 1 && tx.attaches == 1);
    assert(cur->slot == 2 && cur->keyboard.slot == 1 && !cur->keyboard.preset_shape_active);
    assert(!memcmp(library, cur->keyboard.shapes, sizeof(library)));
    assert(tx.handle == reservation && !tx.reserved && tx.handle != copy);
    assert(!ss_read(&tx.pool, copy, copied) && !memcmp(original, copied, sizeof(original)));
    callback_effects();
    matches(&tx);
}
static void select_same_slot(void)
{
    ShapeTransaction tx;
    setup(&tx, 1);
    uint8_t original[SC_BYTES], actual[SC_BYTES];
    bytes_current(original);
    assert(!select_slot(&tx, 1) && tx.prepares == 1 && tx.attaches == 1);
    bytes_current(actual);
    assert(cur->keyboard.preset_shape_active && !memcmp(original, actual, sizeof(original)));
    callback_effects();
    matches(&tx);
}
static void refused(int exhausted)
{
    ShapeTransaction tx;
    UnchangedShape before;
    setup(&tx, exhausted ? 1 : 2);
    freeze(&tx, !exhausted);
    tx.fail_after_prepare = !exhausted;
    remember(&tx, &before);
    assert(select_slot(&tx, 8) < 0 && tx.prepares == 1);
    unchanged(&tx, &before);
    if (!exhausted) {
        tx.fail_after_prepare = 0;
        assert(!select_slot(&tx, 8) && tx.attaches == 1);
        assert(cur->keyboard.slot == 7 && !cur->keyboard.preset_shape_active);
        callback_effects();
        matches(&tx);
    }
}
static void invalid_stale_and_newer(void)
{
    ShapeTransaction tx;
    UnchangedShape before;
    setup(&tx, 1);
    remember(&tx, &before);
    const int invalid[] = {-1, 0, 9, INT_MIN, INT_MAX};
    for (uint32_t n = 0; n < sizeof(invalid) / sizeof(invalid[0]); ++n) {
        assert(select_slot(&tx, invalid[n]) < 0 && !tx.prepares);
        unchanged(&tx, &before);
    }
    ++ip_admission[4];
    assert(select_slot(&tx, 2) < 0 && !tx.prepares);
    unchanged(&tx, &before);
    --ip_admission[4];
    cur = &inst[1];
    assert(select_slot(&tx, 2) < 0 && !tx.prepares);
    unchanged(&tx, &before);
    cur = &inst[0];
    /* A late callback for 2 cannot roll back newer UI staging for 3. */
    cur->slot = 3;
    assert(instrument_sound_proof_shape_slot(0, 0x123400, 7, 2, prepare_shape, attach_shape, &tx) <
           0);
    assert(cur->slot == 3 && cur->keyboard.slot == 0 && !tx.prepares);
    cur->slot = 1;
    unchanged(&tx, &before);
}
static void actual_backing_fields(void)
{
    ShapeTransaction tx;
    setup(&tx, 1);
    /* The real changed() reads these UI fields and copies the mapper chord
     * back into type, rather than overwriting it with a stale UI chord. */
    cur->mode = KB_PLAY;
    cur->voicing = 3;
    cur->type = 0;
    assert(!select_slot(&tx, 2));
    assert(cur->keyboard.mode == KB_PLAY && cur->keyboard.voicing == 3 &&
           cur->keyboard.type == 10 && cur->type == 10);
    callback_effects();
    matches(&tx);
}
static void reentry(void)
{
    ShapeTransaction tx;
    setup(&tx, 1);
    tx.probe_reentry = 1;
    assert(!select_slot(&tx, 2) && tx.reentries == 1 && tx.prepares == 1 && tx.attaches == 1);
    assert(cur->keyboard.slot == 1);
    callback_effects();
    matches(&tx);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "change"))
        select_changed_slot();
    else if (!strcmp(argv[1], "same"))
        select_same_slot();
    else if (!strcmp(argv[1], "capacity"))
        refused(1);
    else if (!strcmp(argv[1], "prepared"))
        refused(0);
    else if (!strcmp(argv[1], "stale"))
        invalid_stale_and_newer();
    else if (!strcmp(argv[1], "backing"))
        actual_backing_fields();
    else if (!strcmp(argv[1], "reentry"))
        reentry();
    else
        return 2;
    return 0;
}
