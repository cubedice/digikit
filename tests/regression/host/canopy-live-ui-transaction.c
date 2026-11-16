/* Registered proof-only transactions through the actual CANOPY menu rows.
 * Sound identities, immutable storage and reservation refill are fixtures;
 * this does not establish native Sound lifetimes or device persistence. */
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

/* Link production keyboard.c for the real LEARN capture/save behavior. This
 * UI suite must not accidentally exercise the isolated ROOT-selector proof. */
int kb_selector_candidate(const KbState *s, const MiEvent *input, uint32_t count, uint32_t *type)
{
    (void)s;
    (void)input;
    (void)count;
    (void)type;
    assert(!"selector proof is outside the UI test");
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
    assert(!"selector proof is outside the UI test");
    return -1;
}

typedef struct {
    uint32_t sound, generation;
    uint16_t handle, reserved, expected, initial_handle;
    uint8_t initial[SC_BYTES], staged[SC_BYTES];
} UiOwner;
typedef struct {
    SpPool pool;
    SpEntry slab[SP_PER_SLAB];
    UiOwner owner[IP_INSTANCES];
    dk_ui_menu *active_menu;
    unsigned allocations, prepares, attaches, prepared_mask, attached_mask, expected_mask;
    int fail_owner;
} UiTransaction;

static void *allocate_ui(uint32_t size, void *context)
{
    UiTransaction *tx = context;
    assert(!masked && size == sizeof(tx->slab));
    if (tx->allocations)
        return 0;
    ++tx->allocations;
    return tx->slab;
}
static void accepted_bytes(const UiTransaction *tx, uint32_t i, uint8_t out[SC_BYTES])
{
    CpPreset p;
    assert(!instrument_sound_proof_snapshot(i, tx->owner[i].sound, tx->owner[i].generation, &p));
    assert(!sc_encode(&p, out));
}
static int prepare_ui(void *context, uint32_t i, uint32_t sound, uint32_t generation,
                      const CpPreset *p)
{
    UiTransaction *tx = context;
    assert(masked && i < IP_INSTANCES && tx->active_menu && dk_ui_menu_is_open(tx->active_menu));
    assert(!tx->attaches && !(tx->prepared_mask & (1u << i)));
    ++tx->prepares;
    UiOwner *o = &tx->owner[i];
    assert(sound == o->sound && generation == o->generation);
    uint8_t actual[SC_BYTES], stored[SC_BYTES];
    accepted_bytes(tx, i, actual);
    assert(!sp_read(&tx->pool, o->handle, stored) && !memcmp(actual, stored, SC_BYTES));
    if (tx->active_menu == shape_menu)
        assert(inst[i].slot == (int)inst[i].keyboard.slot + 1);
    if (!o->reserved)
        return -1;
    assert(o->reserved <= tx->pool.count && !tx->slab[o->reserved - 1].ready);
    for (uint32_t j = 0; j < IP_INSTANCES; ++j)
        if (tx->prepared_mask & (1u << j))
            assert(tx->owner[j].reserved != o->reserved);
    o->expected = o->handle;
    assert(!sc_encode(p, o->staged));
    if (tx->fail_owner == (int)i)
        return -1;
    tx->prepared_mask |= 1u << i;
    return 0;
}
static void attach_ui(void *context, uint32_t i, uint32_t sound, uint32_t generation)
{
    UiTransaction *tx = context;
    assert(masked && i < IP_INSTANCES && tx->active_menu && dk_ui_menu_is_open(tx->active_menu));
    assert(tx->prepared_mask == tx->expected_mask && (tx->prepared_mask & (1u << i)));
    assert(!(tx->attached_mask & (1u << i)));
    /* All real changes, including shared learned shapes, precede the first
     * immutable publication; all affected owners were prepared first. */
    for (uint32_t j = 0; j < IP_INSTANCES; ++j)
        if (tx->prepared_mask & (1u << j)) {
            uint8_t actual[SC_BYTES];
            accepted_bytes(tx, j, actual);
            assert(!memcmp(actual, tx->owner[j].staged, SC_BYTES));
        }
    UiOwner *o = &tx->owner[i];
    assert(sound == o->sound && generation == o->generation && o->handle == o->expected &&
           o->reserved);
    assert(!sp_publish(&tx->pool, o->reserved, o->staged));
    o->handle = o->reserved;
    o->reserved = 0;
    tx->attached_mask |= 1u << i;
    ++tx->attaches;
}
static void reserve_ui(UiTransaction *tx, uint32_t i)
{
    assert(!masked && i < IP_INSTANCES && !tx->owner[i].reserved);
    assert(!sp_reserve_task(&tx->pool, &tx->owner[i].reserved));
}
static void begin_action(UiTransaction *tx, dk_ui_menu *active, unsigned expected_mask)
{
    assert(!masked && active && dk_ui_menu_is_open(active));
    tx->active_menu = active;
    tx->expected_mask = expected_mask;
    tx->prepares = tx->attaches = tx->prepared_mask = tx->attached_mask = 0;
}
static int row(dk_ui_menu *m, const char *label)
{
    for (int i = 0; i < m->count; ++i)
        if (!strcmp(m->rows[i].label, label))
            return i;
    assert(!"missing actual menu row");
    return -1;
}
static void press(dk_ui_menu *m, const char *label)
{
    assert(!masked && m && dk_ui_menu_is_open(m));
    int i = row(m, label);
    assert(m->rows[i].fn && !m->rows[i].value);
    m->rows[i].fn(m->rows[i].ctx, m->rows[i].arg);
    assert(!masked && !unsafe_io);
}
static void set_value(dk_ui_menu *m, const char *label, int value)
{
    assert(!masked && m && dk_ui_menu_is_open(m));
    int i = row(m, label);
    assert(m->rows[i].fn && m->rows[i].value);
    assert(value >= m->rows[i].low && value <= m->rows[i].high);
    /* The native menu writes this signed backing before the callback. */
    *m->rows[i].value = value;
    m->rows[i].fn(m->rows[i].ctx, value);
    assert(!masked && !unsafe_io);
}
static void setup(UiTransaction *tx, int family, int divergent)
{
    memset(tx, 0, sizeof(*tx));
    tx->fail_owner = -1;
    ip_init();
    keyboard_initialized = 1;
    mi_ledger.epoch = 1;
    sp_init(&tx->pool, allocate_ui, tx);
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        IpInst *in = &inst[i];
        UiOwner *o = &tx->owner[i];
        in->track = in->bound = ip_tracks[i] = i;
        in->phase = 3;
        in->prepared = 1;
        in->pad_dirty = 0;
        in->pad_family = family;
        in->pad_color = 40 + (int)i;
        in->mode = in->keyboard.mode = i == 1 ? KB_PLAY : KB_ROOT;
        in->type = in->keyboard.type = i == 0 || i == 2 ? 10 : 0;
        in->slot = 1;
        in->keyboard.slot = 0;
        for (uint32_t s = 0; s < 8; ++s)
            in->keyboard.shapes[s] = (KbShape){3, {0, 4, 7, 0}};
        in->keyboard.shapes[1] = (KbShape){3, {0, 3, 7, 0}};
        in->wt[CP_WT_RATE - 16] = 27;
        in->wt[CP_WT_WHEEL - 16] = 81;
        in->fm[CP_FM_WHEEL - 15] = 122;
        in->wtfm[CP_WTFM_WHEEL - 28] = 143;
        mi_ledger.generations[i] = 11 + i;
        kb_reset(&in->keyboard, mi_ledger.epoch, mi_ledger.generations[i]);
        o->sound = 0x123400 + i * 0x450;
        o->generation = 7 + i;
        ip_admission[8 * i] = 1;
        ip_admission[8 * i + 1] = o->sound;
        ip_admission[8 * i + 4] = o->generation;
        ip_admission[8 * i + 5] = admitted;
    }
    inst[0].keyboard.preset_shape = (KbShape){3, {0, 7, 12, 0}};
    inst[0].keyboard.preset_shape_active = 1;
    inst[2].keyboard.preset_shape = (KbShape){3, {0, 3, 8, 0}};
    inst[2].keyboard.preset_shape_active = 1;
    inst[3].slot = 2;
    inst[3].keyboard.slot = 1;
    if (divergent)
        inst[3].keyboard.shapes[1] = (KbShape){3, {0, 2, 7, 0}};
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        UiOwner *o = &tx->owner[i];
        accepted_bytes(tx, i, o->initial);
        uint16_t reserved = 0;
        assert(!sp_reserve_task(&tx->pool, &reserved));
        uint32_t sr = dkrt_irq_mask();
        assert(!sp_publish(&tx->pool, reserved, o->initial));
        o->handle = o->initial_handle = reserved;
        dkrt_irq_restore(sr);
    }
    cur = &inst[0];
    selected = focus = 0;
    assert(!instrument_sound_proof_set_transactions(prepare_ui, attach_ui, tx));
}
static void originals_unchanged(const UiTransaction *tx)
{
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        uint8_t stored[SC_BYTES];
        assert(!sp_read(&tx->pool, tx->owner[i].initial_handle, stored));
        assert(!memcmp(stored, tx->owner[i].initial, SC_BYTES));
    }
    assert(tx->allocations == 1 && !masked && !unsafe_io);
}
static void matches(const UiTransaction *tx)
{
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        uint8_t actual[SC_BYTES], stored[SC_BYTES];
        accepted_bytes(tx, i, actual);
        assert(!sp_read(&tx->pool, tx->owner[i].handle, stored) &&
               !memcmp(actual, stored, SC_BYTES));
    }
    originals_unchanged(tx);
}
typedef struct {
    IpInst instances[IP_INSTANCES];
    CsState sources[IP_INSTANCES];
    IsStatus statuses[IP_INSTANCES];
    MiLedger ledger;
    SpEntry slab[SP_PER_SLAB];
    uint16_t handles[IP_INSTANCES], reservations[IP_INSTANCES];
    uint32_t count;
    int pending, preset_pending_save, restore, selector, wheel, fm_wheel, wtfm_wheel;
    uint8_t pending_preset[CP_RECORD_BYTES];
} Before;
static void remember(const UiTransaction *tx, Before *before)
{
    memcpy(before->instances, inst, sizeof(inst));
    memcpy(before->sources, src_state, sizeof(src_state));
    memcpy(before->statuses, src_status, sizeof(src_status));
    before->ledger = mi_ledger;
    memcpy(before->slab, tx->slab, sizeof(tx->slab));
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        before->handles[i] = tx->owner[i].handle;
        before->reservations[i] = tx->owner[i].reserved;
    }
    before->count = tx->pool.count;
    before->pending = save_pending;
    before->preset_pending_save = preset_pending;
    before->restore = route_restore;
    before->selector = selector_channel;
    before->wheel = wheel_amount;
    before->fm_wheel = fm_wheel_amount;
    before->wtfm_wheel = wtfm_wheel_amount;
    memcpy(before->pending_preset, preset_write, sizeof(preset_write));
}
static void unchanged(const UiTransaction *tx, const Before *before)
{
    assert(!memcmp(before->instances, inst, sizeof(inst)));
    assert(!memcmp(before->sources, src_state, sizeof(src_state)));
    assert(!memcmp(before->statuses, src_status, sizeof(src_status)));
    assert(!memcmp(&before->ledger, &mi_ledger, sizeof(mi_ledger)));
    assert(!memcmp(before->slab, tx->slab, sizeof(tx->slab)) && before->count == tx->pool.count);
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        assert(before->handles[i] == tx->owner[i].handle &&
               before->reservations[i] == tx->owner[i].reserved);
    assert(before->pending == save_pending && before->preset_pending_save == preset_pending);
    assert(before->restore == route_restore && before->selector == selector_channel);
    assert(before->wheel == wheel_amount && before->fm_wheel == fm_wheel_amount &&
           before->wtfm_wheel == wtfm_wheel_amount);
    assert(!memcmp(before->pending_preset, preset_write, sizeof(preset_write)));
    assert(!tx->attaches && dk_ui_menu_is_open(tx->active_menu));
    matches(tx);
}
static void open_preset(void)
{
    preset_menu = dk_ui_menu_new(ui(), &preset_desc);
    assert(preset_menu && !dk_ui_menu_open(preset_menu));
}
static void install_preset(void)
{
    CpPreset p;
    preset_snapshot(cur, &p);
    p.mode = KB_PLAY;
    p.family = 3;
    p.control[CP_COLOR] = 93;
    p.control[CP_STRUM] = 19;
    p.fm[CP_FM_WHEEL - 15] = 160;
    p.shape = (KbShape){3, {0, 5, 9, 0}};
    assert(!cp_encode(&p, preset_records[0]));
    preset_present = 1;
}
static void preset_load_transaction(void)
{
    UiTransaction tx;
    Before before;
    setup(&tx, 0, 0);
    install_preset();
    open_preset();
    press(preset_menu, "SAVE CURRENT SOUND");
    assert(preset_pending == 1);
    begin_action(&tx, preset_menu, 1);
    remember(&tx, &before);
    press(preset_menu, "LOAD SAVED SOUND");
    assert(tx.prepares == 1);
    unchanged(&tx, &before);
    reserve_ui(&tx, 0);
    uint16_t next = tx.owner[0].reserved;
    begin_action(&tx, preset_menu, 1);
    press(preset_menu, "LOAD SAVED SOUND");
    assert(tx.prepares == 1 && tx.attaches == 1 && tx.owner[0].handle == next && !preset_pending);
    assert(cur->keyboard.mode == KB_PLAY && cur->pad_family == 3 && cur->pad_color == 93 &&
           cur->pad_strum == 19);
    assert(cur->keyboard.preset_shape_active && cur->keyboard.preset_shape.intervals[1] == 5);
    assert(cur->phase == 1 && !cur->prepared && dk_ui_menu_is_open(preset_menu));
    matches(&tx);
}
static void open_tone(void)
{
    tone_menu = dk_ui_menu_new(ui(), &tone_desc);
    assert(tone_menu && !dk_ui_menu_open(tone_menu));
}
static void tone_edit(UiTransaction *tx, unsigned setting, int value)
{
    begin_action(tx, tone_menu, 1);
    if (setting == IP_PROOF_SCAN_CLOCK || setting == IP_PROOF_SCAN_RESET) {
        /* The shared host's native chooser stub does not retain its callback.
         * Invoke the real row, then complete that chooser with its real UI
         * completion entry point rather than calling a transaction helper. */
        int i = setting == IP_PROOF_SCAN_CLOCK ? 0 : 1;
        assert(tone_menu->rows[i].fn && !tone_menu->rows[i].value);
        tone_menu->rows[i].fn(tone_menu->rows[i].ctx, tone_menu->rows[i].arg);
        if (setting == IP_PROOF_SCAN_CLOCK)
            scan_clock_done(0, value);
        else
            scan_restart_done(0, value);
    } else {
        static const char *const labels[] = {"WHEEL TO POS", "WHEEL TO FM", "WHEEL TO WT FM"};
        set_value(tone_menu, labels[setting - IP_PROOF_WT_WHEEL], value);
    }
    assert(!masked && !unsafe_io && dk_ui_menu_is_open(tone_menu));
    /* No test-side refresh repairs rejected signed staging. */
    assert(wheel_amount == cur->wt[CP_WT_WHEEL - 16] - 100);
    assert(fm_wheel_amount == cur->fm[CP_FM_WHEEL - 15] - 100);
    assert(wtfm_wheel_amount == cur->wtfm[CP_WTFM_WHEEL - 28] - 100);
}
static void modulation_refuse_and_retry(int family, int fail_after_prepare)
{
    UiTransaction tx;
    Before before;
    setup(&tx, family, 0);
    open_tone();
    if (fail_after_prepare) {
        reserve_ui(&tx, 0);
        tx.fail_owner = 0;
    }
    const int values[] = {1, 2, -100, 100, -37};
    for (unsigned setting = 0; setting <= IP_PROOF_WTFM_WHEEL; ++setting) {
        remember(&tx, &before);
        tone_edit(&tx, setting, values[setting]);
        assert(tx.prepares == 1);
        unchanged(&tx, &before);
    }
    tx.fail_owner = -1;
    for (unsigned setting = 0; setting <= IP_PROOF_WTFM_WHEEL; ++setting) {
        if (!tx.owner[0].reserved)
            reserve_ui(&tx, 0);
        tone_edit(&tx, setting, values[setting]);
        assert(tx.prepares == 1 && tx.attaches == 1 && !tx.owner[0].reserved);
        matches(&tx);
    }
    assert(cur->pad_family == family && cur->wt[CP_WT_CLOCK - 16] == 1 &&
           cur->wt[CP_WT_RATE - 16] == 3);
    assert(cur->wt[CP_WT_RESET - 16] == 2 && cur->wt[CP_WT_WHEEL - 16] == 0);
    assert(cur->fm[CP_FM_WHEEL - 15] == 200 && cur->wtfm[CP_WTFM_WHEEL - 28] == 63);
    reserve_ui(&tx, 0);
    tone_edit(&tx, IP_PROOF_SCAN_CLOCK, 0);
    assert(tx.attaches == 1 && cur->wt[CP_WT_CLOCK - 16] == 0 && cur->wt[CP_WT_RATE - 16] == 10);
    matches(&tx);
}
static void open_shape(void)
{
    shape_menu = dk_ui_menu_new(ui(), &shape_desc);
    assert(shape_menu && !dk_ui_menu_open(shape_menu));
}
static void shape_transaction(int same_slot)
{
    UiTransaction tx;
    Before before;
    setup(&tx, 0, 0);
    open_shape();
    route_restore = 1;
    selector_channel = 1;
    selector_octave = 2;
    begin_action(&tx, shape_menu, 1);
    remember(&tx, &before);
    set_value(shape_menu, "SHAPE SLOT", 2);
    assert(tx.prepares == 1 && cur->slot == 1 && cur->keyboard.preset_shape_active);
    unchanged(&tx, &before);
    reserve_ui(&tx, 0);
    tx.fail_owner = 0;
    begin_action(&tx, shape_menu, 1);
    remember(&tx, &before);
    set_value(shape_menu, "SHAPE SLOT", 8);
    assert(tx.prepares == 1);
    unchanged(&tx, &before);
    tx.fail_owner = -1;
    begin_action(&tx, shape_menu, 1);
    set_value(shape_menu, "SHAPE SLOT", same_slot ? 1 : 2);
    assert(tx.prepares == 1 && tx.attaches == 1 && cur->slot == (int)cur->keyboard.slot + 1);
    assert(cur->keyboard.preset_shape_active == same_slot &&
           cur->keyboard.slot == (same_slot ? 0 : 1));
    assert(!route_restore && !selector_channel && dk_ui_menu_is_open(shape_menu));
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        assert(inst[i].phase == 1 && !inst[i].prepared && mi_ledger.generations[i] == 12 + i);
        assert(inst[i].keyboard.selector_base == 36 &&
               inst[i].keyboard.selector_channel == MI_NONE);
    }
    matches(&tx);
}
static void input_note(uint8_t note)
{
    MiEvent input = {0}, output[8];
    input.kind = MI_NOTE_ON;
    input.note = note;
    input.value = 90;
    input.token = token++;
    input.epoch = mi_ledger.epoch;
    input.generation = mi_ledger.generations[0];
    input.track = 0;
    assert(instrument_keyboard_events(0, input.epoch, input.generation, &input, 1, output) >= 0);
    assert(!masked && !unsafe_io);
}
static void capture_chord(UiTransaction *tx)
{
    open_shape();
    begin_action(tx, shape_menu, 0);
    press(shape_menu, "LEARN TO ROOT");
    assert(learn_menu && dk_ui_menu_is_open(learn_menu) && cur->keyboard.mode == KB_LEARN);
    assert(!tx->prepares && !tx->attaches);
    input_note(69);
    input_note(60);
    input_note(65);
    dk_ui_menu_refresh(learn_menu);
    assert(cur->keyboard.capture_count == 3 && cur->keyboard.capture[0] == 60 &&
           cur->keyboard.capture[1] == 65 && cur->keyboard.capture[2] == 69);
    assert(shown_session == cur->keyboard.session && shown_revision == cur->keyboard.revision);
    /* The accepted snapshot reads the pre-LEARN mode, not the temporary UI
     * capture state, so persistence can compare it to the existing handle. */
    matches(tx);
}
static void check_learned(const UiTransaction *tx, unsigned affected)
{
    assert(tx->prepared_mask == affected && tx->attached_mask == affected);
    assert(save_pending && !dk_ui_menu_is_open(learn_menu) && cur->keyboard.mode == KB_ROOT);
    assert(!cur->keyboard.preset_shape_active && inst[2].keyboard.preset_shape_active);
    assert(inst[1].keyboard.mode == KB_PLAY && inst[1].keyboard.type == 0);
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        assert(inst[i].keyboard.shapes[0].count == 3 &&
               inst[i].keyboard.shapes[0].intervals[1] == 5 &&
               inst[i].keyboard.shapes[0].intervals[2] == 9);
        if (affected & (1u << i))
            assert(tx->owner[i].handle != tx->owner[i].initial_handle && !tx->owner[i].reserved);
        else
            assert(tx->owner[i].handle == tx->owner[i].initial_handle);
    }
    matches(tx);
}
static void learn_transaction(int divergent, int refuse_later)
{
    UiTransaction tx;
    Before before;
    setup(&tx, 0, divergent);
    capture_chord(&tx);
    unsigned affected = divergent ? 11u : 3u;
    reserve_ui(&tx, 0);
    if (divergent || !refuse_later)
        reserve_ui(&tx, 1);
    if (divergent && !refuse_later)
        reserve_ui(&tx, 3);
    if (refuse_later) {
        begin_action(&tx, learn_menu, affected);
        remember(&tx, &before);
        press(learn_menu, "YES SAVE / NO CANCEL");
        assert(tx.prepares == (divergent ? 3u : 2u));
        unchanged(&tx, &before);
        /* Failed prepare of the later owner leaves all earlier reservations,
         * capture state, old snapshots and the real SAVE menu available. */
        begin_action(&tx, learn_menu, affected);
        press(learn_menu, "YES SAVE / NO CANCEL");
        unchanged(&tx, &before);
        reserve_ui(&tx, divergent ? 3 : 1);
    }
    begin_action(&tx, learn_menu, affected);
    press(learn_menu, "YES SAVE / NO CANCEL");
    assert(tx.prepares == (divergent ? 3u : 2u) && tx.attaches == tx.prepares);
    check_learned(&tx, affected);
}
static void disabled_callbacks(void)
{
    UiTransaction tx;
    setup(&tx, 0, 0);
    assert(!instrument_sound_proof_set_transactions(0, 0, 0));
    install_preset();
    open_preset();
    begin_action(&tx, preset_menu, 0);
    press(preset_menu, "SAVE CURRENT SOUND");
    press(preset_menu, "LOAD SAVED SOUND");
    assert(!preset_pending && cur->pad_color == 93 && cur->keyboard.mode == KB_PLAY);
    open_tone();
    tone_edit(&tx, IP_PROOF_SCAN_CLOCK, 1);
    tone_edit(&tx, IP_PROOF_SCAN_RESET, 2);
    tone_edit(&tx, IP_PROOF_WT_WHEEL, -50);
    tone_edit(&tx, IP_PROOF_FM_WHEEL, 25);
    tone_edit(&tx, IP_PROOF_WTFM_WHEEL, -75);
    assert(cur->wt[CP_WT_RATE - 16] == 3 && cur->wt[CP_WT_RESET - 16] == 2);
    assert(cur->wt[CP_WT_WHEEL - 16] == 50 && cur->fm[CP_FM_WHEEL - 15] == 125 &&
           cur->wtfm[CP_WTFM_WHEEL - 28] == 25);
    open_shape();
    set_value(shape_menu, "SHAPE SLOT", 2);
    assert(cur->keyboard.slot == 1 && !cur->keyboard.preset_shape_active);
    press(shape_menu, "LEARN TO ROOT");
    input_note(60);
    input_note(67);
    dk_ui_menu_refresh(learn_menu);
    begin_action(&tx, learn_menu, 0);
    press(learn_menu, "YES SAVE / NO CANCEL");
    assert(!dk_ui_menu_is_open(learn_menu) && save_pending && cur->keyboard.mode == KB_PLAY);
    assert(cur->keyboard.shapes[1].count == 2 && cur->keyboard.shapes[1].intervals[1] == 7);
    assert(!tx.prepares && !tx.attaches);
    originals_unchanged(&tx);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "preset"))
        preset_load_transaction();
    else if (!strcmp(argv[1], "modulation"))
        modulation_refuse_and_retry(3, 0);
    else if (!strcmp(argv[1], "inactive"))
        modulation_refuse_and_retry(0, 1);
    else if (!strcmp(argv[1], "shape"))
        shape_transaction(0);
    else if (!strcmp(argv[1], "shape-same"))
        shape_transaction(1);
    else if (!strcmp(argv[1], "learn-two"))
        learn_transaction(0, 0);
    else if (!strcmp(argv[1], "learn-three"))
        learn_transaction(1, 0);
    else if (!strcmp(argv[1], "learn-refuse-two"))
        learn_transaction(0, 1);
    else if (!strcmp(argv[1], "learn-refuse-three"))
        learn_transaction(1, 1);
    else if (!strcmp(argv[1], "disabled"))
        disabled_callbacks();
    else
        return 2;
    return 0;
}
