#include "track-native.h"
#include "track-canopy.h"
#include "track-capture.h"
#include "track-memory.h"
#include "track-midi.h"
#include "track-pattern-fw.h"
#include "track-pool.h"
#include "track-sequence.h"

#include "dkrt.h"
#include "dk_fw_input.h"
#include "dk_ui_core.h"

extern uint32_t pp_current_pattern(void);
extern uint32_t pp_transport_running(void);
extern uint32_t pp_selected_tempo(void);
extern volatile uint32_t pp_position[16];
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern uintptr_t tk_fw_project(void);
extern uintptr_t tk_fw_active_kit(uintptr_t project);
extern char *tk_trig_decimal(char *out, unsigned value);
extern char *tk_trig_pitch_name(char *out, uint8_t pitch);
extern void tk_trig_notes_title(char *title, uint8_t track, int step, int pitch);
extern void tk_trig_refresh_keys(void);

static uint32_t pattern, editing_pattern, revision, block, boundary, block_fraction;
static uint32_t stopped_boundary;
static bool was_running;
static uint16_t registered;
static uint8_t kind[TK_TRACKS], panel_track[TK_NOTES];
static uint16_t panel_claims, canceled_panel;
static uint8_t retrig_track[TK_NOTES];
static uint16_t retrig_keys;
volatile uint32_t tk_panel_claim_hits;
static bool polling;
static TkNotesDraft draft;
static dk_ui_menu *notes_menu, *note_menu, *selector_menu, *pool_menu;
static bool notes_dirty, notes_choosing_step, notes_existing_step;
static uint8_t note_bank;
static uint16_t note_steps, pool_eligible;
static uint32_t notes_title_at, notes_title_key;
static void picker_enter(void);
static uint8_t pool_owner;
static int32_t note_index, note_offset, note_velocity;
static int32_t selector_octave, selector_channel;

static void notes_update_title(void)
{
    const int step = draft.step_scope && !notes_choosing_step ? draft.step : -1;
    uint32_t token;
    int pitch = notes_choosing_step ? -1 : tk_trig_arp_pitch(draft.track, &token);
    if (pitch >= 0 && kind[draft.track] != TK_NATIVE_CANOPY &&
        !tk_native_sounding(draft.track, token))
        pitch = -1;
    /* The three fields fit individually in a byte. The extra track count
     * keeps zero reserved for the initial title. */
    uint32_t key =
        ((uint32_t)(draft.track + 1) << 16) | ((uint32_t)(step + 1) << 8) | (uint32_t)(pitch + 1);
    if (key == notes_title_key)
        return;
    notes_title_key = key;
    char title[DK_UI_ROW];
    tk_trig_notes_title(title, draft.track, step, pitch);
    dk_ui_core_list_title(notes_menu, title);
}

static void close_notes(void)
{
    if (pool_menu && dk_ui_menu_is_open(pool_menu))
        dk_ui_menu_close(pool_menu);
    if (selector_menu && dk_ui_menu_is_open(selector_menu))
        dk_ui_menu_close(selector_menu);
    if (note_menu && dk_ui_menu_is_open(note_menu))
        dk_ui_menu_close(note_menu);
    if (notes_menu && dk_ui_menu_is_open(notes_menu))
        dk_ui_menu_close(notes_menu);
    draft.open = false;
}

uint16_t tk_runtime_learn_mask(void)
{
    return draft.open && draft.learn && draft.track < TK_TRACKS ? (uint16_t)(1u << draft.track) : 0;
}

void tk_runtime_midi_busy(void)
{
    static uint32_t last_notice;
    const uint32_t now = dkrt_blocks() | 1u;
    if (!last_notice || now - last_notice >= 1500u) {
        last_notice = now;
        dk_ui_toast(dk_ui_core_handle(), "MIDI BUSY");
    }
}

bool tk_runtime_learn_note(uint8_t track, uint8_t pitch, uint8_t velocity)
{
    if (!draft.open || !draft.learn || draft.track != track)
        return false;
    if (!tk_notes_append(&draft, pitch, velocity))
        dk_ui_toast(dk_ui_core_handle(), "NOTES FULL");
    return true;
}

static uint32_t pattern_snapshot(void)
{
    uint32_t flags = pp_position[5], audible = pp_position[6], committed = pp_position[3];
    /* A START can be native-running before the frame contains its position.
     * Until a new boundary arrives, follow the scheduled pattern armed at STOP. */
    if ((flags & 2u) && committed != stopped_boundary)
        return (flags & 1u) && audible ? audible : pattern;
    return pp_current_pattern();
}

void tk_panel_cancel(void)
{
    for (uint8_t key = 0; key < TK_NOTES; ++key)
        if (panel_claims & (1u << key))
            tk_service_release(panel_track[key], TK_PANEL, 0, key);
    canceled_panel |= panel_claims;
    panel_claims = 0;
}

static void clear_retrigs(void)
{
    for (uint8_t key = 0; key < TK_NOTES; ++key)
        if (retrig_keys & (1u << key))
            tk_service_manual_retrig(retrig_track[key], false);
    retrig_keys = 0;
}

void tk_panel_focus(uint32_t track)
{
    for (uint8_t key = 0; key < TK_NOTES; ++key)
        if ((panel_claims & (1u << key)) && panel_track[key] != track) {
            tk_panel_cancel();
            return;
        }
}

static bool refresh_pattern(void)
{
    uint32_t sr = dkrt_irq_mask();
    bool running = pp_transport_running() != 0;
    if (!running)
        stopped_boundary = pp_position[3];
    uint32_t next = running ? pattern_snapshot() : 0;
    dkrt_irq_restore(sr);
    uint32_t edit = tk_pattern_fw_selected();
    if (!edit)
        edit = pp_current_pattern();
    uint32_t stamp = tk_pattern_fw_revision();
    bool project_data_changed = stamp != revision;
    if (next != pattern || edit != editing_pattern || project_data_changed) {
        close_notes();
        if (project_data_changed) {
            clear_retrigs();
            tk_panel_cancel();
            tk_midi_reset();
            tk_sequence_reset();
            tk_capture_poll(0, 0);
            tk_service_stop();
        }
        tk_service_edit_pattern(tk_pattern_fw_get(edit));
        tk_service_play_pattern(tk_pattern_fw_get(next));
        pattern = next;
        editing_pattern = edit;
        revision = stamp;
    }
    if (was_running && !running)
        tk_service_stop();
    was_running = running;
    return edit != 0;
}

static bool can_prepare_track(uint8_t track, const TkPattern *destination,
                              TkSequenceNativeTarget target, uintptr_t active_kit)
{
    const TkTrackConfig *active = tk_service_active_config(track);
    const TkTrackConfig *next = &destination->tracks[track];
    if (!active)
        return false;
    if (kind[track] == TK_NATIVE_CANOPY) {
        /* An early seed can use the active CANOPY Sound only when both
         * patterns share that Sound and have adopted the shared keyboard. */
        return active_kit && target.kit == active_kit && active->override && next->override &&
               tk_canopy_routed(track) && !tk_sequence_canopy_sound_fallback(target.address, track);
    }
    if (kind[track] == TK_NATIVE_RELAY)
        return active_kit && target.kit == active_kit && !active->helpers && !next->helpers;
    if (tk_sequence_target_kind(target.kit, track) != (int)kind[track])
        return false;
    /* MIDI sends through the active kit's channel. Audio has a detached
     * Sound snapshot, but pooled audio depends on the current voice group. */
    if (kind[track] == TK_NATIVE_MIDI)
        return target.kit == active_kit;
    return !active->helpers && !next->helpers;
}

static void prepare_sequence_targets(void)
{
    if (!was_running || !pattern)
        return;
    TkSequenceNativeTarget native[TK_SEQUENCE_TARGETS];
    TkSequenceTarget targets[TK_SEQUENCE_TARGETS];
    unsigned count = 0;
    tk_sequence_native_targets(native);
    uintptr_t project = tk_fw_project();
    uintptr_t active_kit = project ? tk_fw_active_kit(project) : 0;
    for (unsigned i = 0; i < TK_SEQUENCE_TARGETS; ++i) {
        uint32_t address = native[i].address;
        if (!address || address == pattern)
            continue;
        bool already_prepared = false;
        for (unsigned j = 0; j < count; ++j)
            already_prepared |= targets[j].address == address;
        if (already_prepared)
            continue;
        const TkPattern *destination = tk_pattern_fw_existing(address);
        if (!destination)
            continue;
        uint16_t blocked = (uint16_t)~registered;
        for (uint8_t track = 0; track < TK_TRACKS; ++track)
            if ((registered & (UINT32_C(1) << track)) &&
                !can_prepare_track(track, destination, native[i], active_kit))
                blocked |= (uint16_t)(UINT32_C(1) << track);
        targets[count++] = (TkSequenceTarget){address, destination, blocked};
    }
    tk_sequence_prepare(targets, count);
}

static void update_pool(void)
{
    tk_pool_fw_update(pattern ? pattern : editing_pattern, editing_pattern,
                      tk_service_active_config(0), !was_running);
}

static void poll(void)
{
    if (!refresh_pattern())
        return;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        uint16_t mask = (uint16_t)(1u << track);
        int type = tk_pool_fw_reserved(track) ? TK_NATIVE_UNSUPPORTED : tk_native_kind(track);
        if ((registered & mask) && type != kind[track]) {
            tk_midi_cancel_track(track);
            tk_service_set_backend(track, NULL);
            if (kind[track] == TK_NATIVE_CANOPY)
                tk_canopy_sync(track, false, 0);
            registered &= (uint16_t)~mask;
        }
        if (!(registered & mask) && type != TK_NATIVE_UNSUPPORTED && tk_native_idle(track)) {
            TkTrackBackend backend;
            if (type == TK_NATIVE_CANOPY)
                tk_canopy_backend(&backend);
            else if (type == TK_NATIVE_RELAY)
                tk_native_relay_backend(&backend);
            else
                tk_native_backend(type == TK_NATIVE_MIDI, &backend);
            if (tk_service_set_backend(track, &backend)) {
                kind[track] = (uint8_t)type;
                registered |= mask;
            }
        }
        if (type == TK_NATIVE_CANOPY)
            tk_canopy_sync(track, true, pattern ? pattern : editing_pattern);
        else if ((type == TK_NATIVE_AUDIO || type == TK_NATIVE_RELAY) && (registered & mask))
            tk_service_set_voice_capacity(track, tk_pool_fw_limit(track));
        tk_sequence_set_midi(track, type == TK_NATIVE_MIDI);
        tk_sequence_set_relay(track, (registered & mask) && type == TK_NATIVE_RELAY);
    }
    uint32_t observed = pp_position[3];
    if (observed != boundary && (pp_position[5] & 1u) && pp_position[7] == 0) {
        tk_service_boundary();
        boundary = observed;
    }
    uint32_t now = dkrt_blocks();
    uint32_t elapsed = now - block;
    block = now;
    if (elapsed > 1500u) {
        clear_retrigs();
        tk_panel_cancel();
        tk_midi_reset();
        elapsed = 1500u;
    }
    uint32_t time = elapsed * 2000u + block_fraction;
    block_fraction = time % 3u;
    tk_service_tick(time / 3u, (uint16_t)pp_selected_tempo());
    tk_midi_poll();
    tk_sequence_poll(pattern ? pattern : editing_pattern, was_running);
    prepare_sequence_targets();
    tk_capture_poll(editing_pattern, (uint16_t)pp_selected_tempo());
    update_pool();
    static uint32_t capture_drops, capture_notice;
    if (capture_drops != tk_capture_dropped) {
        capture_drops = tk_capture_dropped;
        if (!capture_notice || now - capture_notice >= 1500u) {
            capture_notice = now | 1u;
            dk_ui_toast(dk_ui_core_handle(), "NOTES FULL");
        }
    }
    tk_native_drain();
    if (draft.open && now - notes_title_at >= 120u && dk_ui_core_menu_front(notes_menu)) {
        notes_title_at = now;
        notes_update_title();
    }
}

static void ui_poll(void)
{
    static uint32_t keys_block;
    poll();
    /* The painter also synchronizes musical state through poll(). Only
     * the registered UI pass can request another keyboard frame. */
    if (block != keys_block) {
        keys_block = block;
        tk_trig_refresh_keys();
    }
}

void tk_runtime_init(void)
{
    if (polling || !tk_service_init() || !tk_sequence_init() || !tk_sequence_fw_init() ||
        !tk_midi_init() || !tk_native_init() || !tk_pool_link_init())
        return;
    block = dkrt_blocks();
    if (dkrt_poll_add((uintptr_t)ui_poll) == 0)
        polling = true;
}

void tk_runtime_touch(uint8_t track)
{
    if (track >= TK_TRACKS)
        return;
    tk_runtime_init();
    if (draft.open && draft.track != track)
        close_notes();
    if (polling)
        poll();
}

int tk_panel_on(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity)
{
    if (key >= TK_NOTES || track >= TK_TRACKS || pitch > 127 || !velocity || velocity > 127)
        return 0;
    uint16_t mask = (uint16_t)(1u << key);
    if ((panel_claims | canceled_panel) & mask)
        return 1;
    if (!polling || !refresh_pattern() || !(registered & (1u << track)) ||
        tk_native_kind((uint8_t)track) != kind[track])
        return 0;
    bool learned = tk_runtime_learn_note((uint8_t)track, (uint8_t)pitch, (uint8_t)velocity);
    poll();
    int8_t octave, channel;
    bool selector = !learned && tk_service_enabled((uint8_t)track) &&
                    tk_service_selector((uint8_t)track, true, &octave, &channel) && channel < 0 &&
                    octave == (int8_t)(pitch / 12u);
    if (selector) {
        tk_service_select_chord((uint8_t)track, (enum TkChord)(TK_CHORD_MAJ + pitch % 12u));
        tk_capture_shape_changed((uint8_t)track);
    } else if (!tk_service_press((uint8_t)track, TK_PANEL, 0, key, (uint8_t)pitch,
                                 (uint8_t)velocity) &&
               !learned) {
        return 0;
    }
    panel_track[key] = (uint8_t)track;
    panel_claims |= mask;
    ++tk_panel_claim_hits;
    tk_service_tick(0, (uint16_t)pp_selected_tempo());
    if (tk_native_audio_waiting() & (1u << track))
        update_pool();
    tk_native_drain();
    return 1;
}

static void notes_apply(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (notes_existing_step && !tk_pattern_fw_note_step(editing_pattern, draft.track, draft.step)) {
        dk_ui_toast(dk_ui_core_handle(), "STEP UNAVAILABLE");
        return;
    }
    if (tk_notes_commit(&draft))
        dk_ui_menu_close(notes_menu);
    else
        dk_ui_toast(dk_ui_core_handle(), "PLAY NOTES FIRST");
}

static void notes_learn(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    notes_dirty = true;
    tk_notes_learn(&draft);
    tk_midi_poll();
    dk_ui_menu_close(notes_menu);
    dk_ui_toast(dk_ui_core_handle(), "PLAY KEYS. CLICK CHRD TO SAVE");
}

static int32_t notes_back(void *unused)
{
    (void)unused;
    if (notes_choosing_step) {
        notes_choosing_step = false;
        notes_update_title();
        dk_ui_menu_select(notes_menu, 0);
        return 1;
    }
    draft.open = false;
    return 0;
}

static void note_apply(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (!draft.open || note_index < 0 || note_index >= draft.recipe.count)
        return;
    notes_dirty = true;
    draft.recipe.notes[note_index] = (TkRecipeNote){(int8_t)note_offset, (uint8_t)note_velocity};
    dk_ui_menu_close(note_menu);
    dk_ui_menu_refresh(notes_menu);
}

static void note_fill(dk_ui_menu *m, void *unused)
{
    (void)unused;
    dk_ui_menu_add_value(m, "OFFSET", &note_offset, -127, 127, NULL, NULL);
    dk_ui_menu_add_value(m, "VELOCITY", &note_velocity, 1, 127, NULL, NULL);
    dk_ui_menu_add_action(m, "APPLY NOTE", note_apply, NULL, 0);
}

static void note_open(void *unused, int32_t index)
{
    (void)unused;
    static const dk_ui_menu_desc desc = {
        .name = "track-keyboard/note", .title = "NOTE", .fill = note_fill};
    if (!draft.open || index < 0 || index >= draft.recipe.count)
        return;
    if (!note_menu)
        note_menu = dk_ui_menu_new(dk_ui_core_handle(), &desc);
    if (note_menu) {
        note_index = index;
        note_offset = draft.recipe.notes[index].interval;
        note_velocity = draft.recipe.notes[index].velocity;
        if (!note_velocity)
            note_velocity = 100;
        dk_ui_menu_open(note_menu);
    }
}

static void note_remove(void *unused, int32_t index)
{
    (void)unused;
    if (!draft.open || index < 0 || index >= draft.recipe.count)
        return;
    notes_dirty = true;
    --draft.recipe.count;
    draft.clear = !draft.recipe.count;
    for (; index < draft.recipe.count; ++index)
        draft.recipe.notes[index] = draft.recipe.notes[index + 1];
    dk_ui_menu_refresh(notes_menu);
}

static void note_move(void *unused, int32_t encoded)
{
    (void)unused;
    int32_t index = encoded / 2;
    int32_t to = index + (encoded & 1 ? 1 : -1);
    if (!draft.open || index < 0 || to < 0 || index >= draft.recipe.count ||
        to >= draft.recipe.count)
        return;
    notes_dirty = true;
    TkRecipeNote note = draft.recipe.notes[index];
    draft.recipe.notes[index] = draft.recipe.notes[to];
    draft.recipe.notes[to] = note;
    dk_ui_menu_refresh(notes_menu);
    dk_ui_menu_select(notes_menu, to + 2);
}

static void note_insert(void *unused, int32_t index)
{
    (void)unused;
    if (!draft.open || index < 0 || index > draft.recipe.count)
        return;
    if (draft.recipe.count == TK_NOTES) {
        dk_ui_toast(dk_ui_core_handle(), "NOTES FULL");
        return;
    }
    notes_dirty = true;
    for (int32_t i = draft.recipe.count; i > index; --i)
        draft.recipe.notes[i] = draft.recipe.notes[i - 1];
    draft.recipe.notes[index] = (TkRecipeNote){.velocity = 100};
    ++draft.recipe.count;
    draft.clear = false;
    dk_ui_menu_refresh(notes_menu);
    dk_ui_menu_select(notes_menu, index + 2);
    note_open(NULL, index);
}

static void note_reference(void *unused, int32_t index)
{
    (void)unused;
    if (!draft.open || index < 0 || index >= draft.recipe.count)
        return;
    int reference = draft.recipe.notes[index].interval;
    for (uint8_t i = 0; i < draft.recipe.count; ++i) {
        int interval = draft.recipe.notes[i].interval - reference;
        if (interval < -127 || interval > 127) {
            dk_ui_toast(dk_ui_core_handle(), "REFERENCE OUT OF RANGE");
            return;
        }
    }
    notes_dirty = true;
    for (uint8_t i = 0; i < draft.recipe.count; ++i)
        draft.recipe.notes[i].interval = (int8_t)(draft.recipe.notes[i].interval - reference);
    dk_ui_menu_refresh(notes_menu);
}

static void notes_clear(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (!draft.open)
        return;
    notes_dirty = true;
    draft.recipe.count = 0;
    draft.clear = true;
    dk_ui_menu_refresh(notes_menu);
    dk_ui_menu_select(notes_menu, 0);
}

static void selector_save(void *unused, int32_t mode)
{
    (void)unused;
    if (!draft.open || draft.step_scope)
        return;
    int8_t octave = mode == 1 ? (int8_t)(selector_octave + 1) : -1;
    int8_t channel = mode == 2 ? (int8_t)(selector_channel - 1) : -1;
    if (tk_service_write_selector(draft.track, octave, channel)) {
        tk_midi_poll();
        dk_ui_menu_close(selector_menu);
    }
}

static void selector_fill(dk_ui_menu *m, void *unused)
{
    (void)unused;
    dk_ui_menu_add_action(m, "OFF", selector_save, NULL, 0);
    dk_ui_menu_add_value(m, "OCTAVE", &selector_octave, -1, 9, NULL, NULL);
    dk_ui_menu_add_action(m, "USE OCTAVE", selector_save, NULL, 1);
    dk_ui_menu_add_value(m, "MIDI CH", &selector_channel, 1, 16, NULL, NULL);
    dk_ui_menu_add_action(m, "USE MIDI CH", selector_save, NULL, 2);
}

static void selector_open(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    static const dk_ui_menu_desc desc = {
        .name = "track-keyboard/selector", .title = "SELECTOR", .fill = selector_fill};
    int8_t octave, channel;
    if (!draft.open || draft.step_scope ||
        !tk_service_selector(draft.track, false, &octave, &channel))
        return;
    if (!selector_menu)
        selector_menu = dk_ui_menu_new(dk_ui_core_handle(), &desc);
    if (!selector_menu)
        return;
    selector_octave = octave >= 0 ? octave - 1 : 2;
    selector_channel = channel >= 0 ? channel + 1 : 16;
    dk_ui_menu_select(selector_menu, octave >= 0 ? 2 : (channel >= 0 ? 4 : 0));
    dk_ui_menu_open(selector_menu);
}

static void notes_pick_step(void *unused, int32_t step)
{
    (void)unused;
    if (tk_pattern_fw_selected() != editing_pattern ||
        !tk_pattern_fw_note_step(editing_pattern, draft.track, (uint8_t)step) ||
        !tk_notes_open(draft.track, (uint8_t)step, true, &draft)) {
        dk_ui_toast(dk_ui_core_handle(), "STEP UNAVAILABLE");
        return;
    }
    notes_existing_step = true;
    notes_choosing_step = false;
    notes_update_title();
    dk_ui_menu_select(notes_menu, 0);
}

static void notes_edit_step(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (notes_dirty) {
        dk_ui_toast(dk_ui_core_handle(), "APPLY OR CANCEL FIRST");
        return;
    }
    notes_choosing_step = true;
    note_bank = draft.step_scope ? draft.step / 16u : 0;
    picker_enter();
    notes_update_title();
    dk_ui_menu_select(notes_menu, 0);
}

static void notes_panel(dk_ui_menu *m, int32_t row, void *unused)
{
    (void)unused;
    if (notes_choosing_step)
        return;
    int32_t n = row - 2;
    if (n < 0 || n >= draft.recipe.count) {
        dk_ui_menu_add_action(m, "INSERT", note_insert, NULL, draft.recipe.count);
        if (draft.recipe.count)
            dk_ui_menu_add_action(m, "CLEAR NOTES", notes_clear, NULL, 0);
        if (!draft.step_scope)
            dk_ui_menu_add_action(m, "SELECTOR", selector_open, NULL, 0);
        dk_ui_menu_add_action(m, "EDIT STEP", notes_edit_step, NULL, 0);
        return;
    }
    if (n > 0)
        dk_ui_menu_add_action(m, "MOVE UP", note_move, NULL, 2 * n);
    if (n + 1 < draft.recipe.count)
        dk_ui_menu_add_action(m, "MOVE DOWN", note_move, NULL, 2 * n + 1);
    dk_ui_menu_add_action(m, "INSERT", note_insert, NULL, n);
    dk_ui_menu_add_action(m, "SET REFERENCE", note_reference, NULL, n);
    dk_ui_menu_add_action(m, "REMOVE", note_remove, NULL, n);
    if (!draft.step_scope)
        dk_ui_menu_add_action(m, "SELECTOR", selector_open, NULL, 0);
    dk_ui_menu_add_action(m, "EDIT STEP", notes_edit_step, NULL, 0);
}

static void notes_play_pitches(dk_ui_menu *m)
{
    TkParams params;
    uint16_t locks;
    bool found = draft.step_scope ? tk_trig_lock_display(draft.track, draft.step, &params, &locks)
                                  : tk_trig_display(draft.track, &params);
    if (!found || params.chord != TK_CHORD_PLAY)
        return;
    uint8_t pitches[TK_NOTES], count;
    const char *heading;
    const TkSeed *seed =
        draft.step_scope ? tk_pattern_seed(draft.pattern, draft.track, draft.step) : NULL;
    if (seed) {
        count = seed->count;
        for (uint8_t i = 0; i < count; ++i)
            pitches[i] = seed->notes[i].pitch;
        heading = "RECORDED PLAY";
    } else if (draft.step_scope) {
        count = tk_sequence_native_pitches(editing_pattern, draft.track, draft.step,
                                           kind[draft.track] == TK_NATIVE_MIDI, pitches);
        heading = "NATIVE PLAY";
    } else {
        count = tk_trig_held_pitches(draft.track, pitches);
        heading = "HELD PLAY";
    }
    if (!count)
        return;
    dk_ui_menu_add_text(m, heading);
    for (uint8_t i = 0; i < count; ++i) {
        char row[6];
        *tk_trig_pitch_name(row, pitches[i]) = 0;
        dk_ui_menu_add_text(m, row);
    }
}

static void notes_fill(dk_ui_menu *m, void *unused)
{
    (void)unused;
    if (notes_choosing_step) {
        char title[DK_UI_ROW] = "STEPS ";
        unsigned first = 16u * note_bank;
        char *end = tk_trig_decimal(title + 6, first + 1);
        *end++ = '-';
        *tk_trig_decimal(end, first + 16) = 0;
        dk_ui_menu_add_text(m, title);
        note_steps = 0;
        for (unsigned key = 0; key < 16; ++key) {
            unsigned step = first + key;
            if (!tk_pattern_fw_note_step(editing_pattern, draft.track, (uint8_t)step))
                continue;
            note_steps |= (uint16_t)(1u << key);
            char label[DK_UI_ROW] = "STEP ";
            *tk_trig_decimal(label + 5, step + 1u) = 0;
            dk_ui_menu_add_action(m, label, notes_pick_step, NULL, (int32_t)step);
        }
        if (!note_steps)
            dk_ui_menu_add_text(m, "NO NOTE TRIGS");
        return;
    }
    const char *apply = draft.clear ? "APPLY PLAY" : "APPLY USER";
    char scope[DK_UI_ROW];
    if (draft.step_scope) {
        memcpy(scope, apply, 10);
        memcpy(scope + 10, " T", 2);
        char *end = tk_trig_decimal(scope + 12, (unsigned)draft.track + 1u);
        memcpy(end, " S", 2);
        *tk_trig_decimal(end + 2, (unsigned)draft.step + 1u) = 0;
        apply = scope;
    }
    dk_ui_menu_add_action(m, apply, notes_apply, NULL, 0);
    dk_ui_menu_add_action(m, "LEARN", notes_learn, NULL, 0);
    for (uint8_t i = 0; i < draft.recipe.count; ++i) {
        char text[DK_UI_ROW];
        char *w = tk_trig_decimal(text, i + 1u);
        *w++ = ' ';
        int n = draft.recipe.notes[i].interval;
        *w++ = n < 0 ? '-' : '+';
        w = tk_trig_decimal(w, (unsigned)(n < 0 ? -n : n));
        *w++ = ' ';
        *tk_trig_decimal(w, draft.recipe.notes[i].velocity) = 0;
        dk_ui_menu_add_action(m, text, note_open, NULL, i);
    }
    if (!notes_dirty)
        notes_play_pitches(m);
}

bool tk_trig_open_notes(uint8_t track, int step)
{
    static const dk_ui_menu_desc desc = {.name = "track-keyboard/notes",
                                         .title = "NOTES",
                                         .flags = DK_UI_LIST,
                                         .fill = notes_fill,
                                         .panel = notes_panel,
                                         .back = notes_back};
    if (!notes_menu)
        notes_menu = dk_ui_menu_new(dk_ui_core_handle(), &desc);
    if (!notes_menu)
        return false;
    if (!draft.open || draft.track != track ||
        (step >= 0 && (!draft.step_scope || draft.step != step))) {
        if (draft.open && notes_dirty) {
            dk_ui_toast(dk_ui_core_handle(), "APPLY OR CANCEL FIRST");
            return false;
        }
        if (!tk_notes_open(track, step < 0 ? 0 : (uint8_t)step, step >= 0, &draft))
            return false;
        notes_dirty = notes_existing_step = false;
    }
    notes_choosing_step = draft.learn = false;
    notes_update_title();
    dk_ui_menu_select(notes_menu, 0);
    if (dk_ui_menu_open(notes_menu) < 0) {
        draft.open = false;
        return false;
    }
    return true;
}

static uint16_t pool_original, pool_request;
static uint32_t pool_pattern, pool_revision;

uint16_t tk_trig_voice_group(uint8_t track)
{
    if (track >= TK_TRACKS || (kind[track] != TK_NATIVE_AUDIO && kind[track] != TK_NATIVE_RELAY))
        return 0;
    return (uint16_t)((1u << track) | tk_pool_fw_helpers(track));
}

static bool pool_has_helpers(void)
{
    return kind[pool_owner] == TK_NATIVE_AUDIO ||
           (kind[pool_owner] == TK_NATIVE_RELAY && tk_pool_fw_relay_ready());
}

static bool pool_editable(void)
{
    if (pp_transport_running()) {
        dk_ui_toast(dk_ui_core_handle(), "STOP TO EDIT VOICE GROUP");
        return false;
    }
    if (editing_pattern == pool_pattern && tk_pattern_fw_revision() == pool_revision &&
        tk_trig_ready(pool_owner) && pool_has_helpers() &&
        tk_service_helpers(pool_owner) == pool_original)
        return true;
    dk_ui_menu_close(pool_menu);
    dk_ui_toast(dk_ui_core_handle(), "VOICE GROUP CHANGED");
    return false;
}

static void pool_toggle(void *unused, int32_t track)
{
    (void)unused;
    if (track < 0 || track >= TK_TRACKS || track == pool_owner || !pool_editable())
        return;
    uint16_t bit = (uint16_t)(1u << track);
    uint16_t request = pool_request ^ bit;
    uint16_t selectable = tk_pool_fw_eligible(pool_owner) | pool_original;
    /* An unavailable draft member can always be removed. Apply checks the
     * complete set against current eligibility before it changes the pattern. */
    if ((request & bit) && !(selectable & bit)) {
        dk_ui_toast(dk_ui_core_handle(), "TRACK UNAVAILABLE");
        return;
    }
    if ((request & bit) && kind[pool_owner] == TK_NATIVE_RELAY && (request & (request - 1u))) {
        dk_ui_toast(dk_ui_core_handle(), "RELAY: ONE HELPER");
        return;
    }
    TkTrackConfig preview;
    if (!tk_assign_helpers(&preview, pool_owner, request, UINT16_MAX, UINT16_MAX, 0)) {
        dk_ui_toast(dk_ui_core_handle(), "UP TO 3 HELPERS");
        return;
    }
    pool_request = preview.helpers;
    dk_ui_menu_refresh(pool_menu);
}

static void pool_apply(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (!pool_editable())
        return;
    if ((kind[pool_owner] == TK_NATIVE_RELAY && (pool_request & (pool_request - 1u))) ||
        !tk_service_assign_helpers(pool_owner, pool_request, tk_pool_fw_eligible(pool_owner))) {
        dk_ui_toast(dk_ui_core_handle(), "TRACK UNAVAILABLE");
        return;
    }
    dk_ui_menu_close(pool_menu);
    poll();
}

static void pool_midi_rate(dk_ui_menu *m, const char *port, unsigned pairs_per_second)
{
    char row[DK_UI_ROW];
    memcpy(row, port, 3);
    row[3] = ' ';
    char *end = tk_trig_decimal(row + 4, pairs_per_second);
    memcpy(end, " PAIRS/S", 9);
    dk_ui_menu_add_text(m, row);
}

static void pool_midi_fill(dk_ui_menu *m)
{
    int channel = tk_native_channel(pool_owner);
    char row[DK_UI_ROW] = "CHAN OFF";
    if (channel >= 0 && channel < 16)
        *tk_trig_decimal(row + 5, (unsigned)channel + 1) = 0;
    dk_ui_menu_add_text(m, row);

    static const char *const ports[] = {"OUT OFF", "OUT DIN", "OUT USB", "OUT MIDI+USB"};
    uint8_t mask = tk_native_midi_ports() & (TK_MIDI_OUT_DIN | TK_MIDI_OUT_USB);
    dk_ui_menu_add_text(m, ports[mask]);

    TkParams params;
    if (tk_trig_display(pool_owner, &params)) {
        unsigned capacity = tk_trig_voice_capacity(pool_owner);
        memcpy(row, "VOICES ", 7);
        *tk_trig_decimal(row + 7, params.voices < capacity ? params.voices : capacity) = 0;
        dk_ui_menu_add_text(m, row);
    }
    if (mask & TK_MIDI_OUT_DIN)
        pool_midi_rate(m, "DIN", TK_MIDI_DIN_BYTES_PER_S / TK_MIDI_DIN_PAIR_BYTES);
    if (mask & TK_MIDI_OUT_USB)
        pool_midi_rate(m, "USB", TK_MIDI_USB_BYTES_PER_S / TK_MIDI_USB_PAIR_BYTES);
}

static void pool_fill(dk_ui_menu *m, void *unused)
{
    (void)unused;
    if (kind[pool_owner] == TK_NATIVE_MIDI) {
        pool_midi_fill(m);
        return;
    }
    if (kind[pool_owner] == TK_NATIVE_CANOPY) {
        dk_ui_menu_add_text(m, "CANOPY");
        dk_ui_menu_add_text(m, "4 PAD VOICES");
        dk_ui_menu_add_text(m, "1 ARP VOICE");
        dk_ui_menu_add_text(m, "2 RELEASE TAILS");
        return;
    }
    char summary[DK_UI_ROW];
    if (pool_original) {
        char *end = tk_trig_decimal(summary, tk_pool_fw_voices(pool_owner));
        memcpy(end, " VOICES", 8);
    } else {
        memcpy(summary, "AUTO ", 5);
        char *end = tk_trig_decimal(summary + 5, tk_pool_fw_voices(pool_owner));
        *end++ = '/';
        *tk_trig_decimal(end, tk_pool_fw_limit(pool_owner)) = 0;
    }
    dk_ui_menu_add_text(m, summary);
    dk_ui_menu_add_action(m, "APPLY GROUP", pool_apply, NULL, 0);
    pool_eligible = tk_pool_fw_eligible(pool_owner);
    int32_t row = 2;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        if (track == pool_owner)
            continue;
        char label[DK_UI_ROW] = {'T'};
        char *w = tk_trig_decimal(label + 1, track + 1);
        *w++ = ' ';
        uint16_t bit = (uint16_t)(1u << track);
        int owner = tk_pool_fw_owner(track);
        const char *state = pool_request & bit    ? owner == pool_owner ? "RESERVED" : "SELECTED"
                            : pool_original & bit ? "REMOVE"
                            : owner == pool_owner ? "BORROWED"
                            : pool_eligible & bit ? "AVAILABLE"
                                                  : "IN USE";
        while (*state)
            *w++ = *state++;
        *w = 0;
        dk_ui_menu_add_action(m, label, pool_toggle, NULL, track);
        dk_ui_menu_check(m, row++, (pool_request & bit) != 0);
    }
}

bool tk_trig_open_pool(uint8_t track)
{
    static const dk_ui_menu_desc desc = {.name = "track-keyboard/pool",
                                         .title = "VOICE GROUP",
                                         .flags = DK_UI_LIST,
                                         .fill = pool_fill};
    if (!tk_trig_ready(track))
        return false;
    bool editable = kind[track] != TK_NATIVE_MIDI && kind[track] != TK_NATIVE_CANOPY;
    if (kind[track] == TK_NATIVE_RELAY && !tk_pool_fw_relay_ready())
        return dk_ui_toast(dk_ui_core_handle(), "RELAY: ONE TAPE VOICE") == 0;
    if (editable && tk_service_helpers(track) && pp_transport_running())
        return dk_ui_toast(dk_ui_core_handle(), "STOP TO EDIT VOICE GROUP") == 0;
    if (!pool_menu)
        pool_menu = dk_ui_menu_new(dk_ui_core_handle(), &desc);
    if (!pool_menu)
        return false;
    pool_owner = track;
    pool_original = pool_request = tk_service_helpers(track);
    pool_pattern = editing_pattern;
    pool_revision = tk_pattern_fw_revision();
    if (editable)
        picker_enter();
    dk_ui_menu_select(pool_menu, editable ? 2 : 0);
    return dk_ui_menu_open(pool_menu) >= 0;
}

enum {
    PICKER_OFF,
    PICKER_POOL,
    PICKER_STEP,
    KEY_PAGE = 22,
    KEY_TRIG = 25,
    LED_TRIG = 26,
    LED_PAGE = 42,
};
static uint32_t picker_held, picker_foreign;

static void picker_enter(void)
{
    picker_foreign = dk_fw_input_key_down(KEY_PAGE) ? 1u << 16 : 0;
    for (unsigned i = 0; i < 16; ++i)
        if (dk_fw_input_key_down(KEY_TRIG + i))
            picker_foreign |= 1u << i;
    picker_foreign &= ~picker_held;
}

static unsigned picker_front(void)
{
    if (notes_choosing_step && draft.open && dk_ui_core_menu_front(notes_menu))
        return PICKER_STEP;
    if (dk_ui_core_menu_front(pool_menu) && pool_has_helpers())
        return PICKER_POOL;
    return PICKER_OFF;
}

int tk_picker_key(uint32_t key, uint32_t flags)
{
    unsigned index = key == KEY_PAGE ? 16u : key - KEY_TRIG;
    if (index > 16)
        return 0;
    uint32_t bit = 1u << index;
    if ((flags & 16u) || !(flags & 11u)) {
        bool owned = (picker_held & bit) != 0;
        picker_held &= ~bit;
        picker_foreign &= ~bit;
        return owned;
    }
    if (picker_held & bit)
        return 1;
    if (!(flags & 1u) || (flags & 8u) || (picker_foreign & bit))
        return 0;
    unsigned mode = picker_front();
    if (!mode || (index == 16 && mode != PICKER_STEP))
        return 0;
    picker_held |= bit;
    if (mode == PICKER_POOL) {
        pool_toggle(NULL, (int32_t)index);
        if (dk_ui_core_menu_front(pool_menu))
            dk_ui_menu_select(pool_menu, 1); /* YES applies the whole draft. */
    } else if (index == 16) {
        note_bank = (uint8_t)((note_bank + 1u) % (TK_STEPS / 16u));
        dk_ui_menu_select(notes_menu, 0);
    } else {
        notes_pick_step(NULL, 16 * note_bank + (int32_t)index);
    }
    return 1;
}

int tk_picker_led(uint32_t logical)
{
    unsigned mode = picker_front();
    if (mode == PICKER_STEP && logical >= LED_PAGE && logical < LED_PAGE + TK_STEPS / 16)
        return logical - LED_PAGE == note_bank ? 0x0a : 0x02;
    unsigned track = logical - LED_TRIG;
    if (!mode || track >= 16)
        return -1;
    uint16_t bit = (uint16_t)(1u << track);
    if (mode == PICKER_STEP)
        return (note_steps & bit) ? 0x71 : 0x01;
    if (track == pool_owner)
        return 0x41; /* Owner: blue. */
    if (pool_request & bit)
        return 0x71; /* Selected: white. */
    return (pool_eligible & bit) ? 0x02 : 0x01;
}

int tk_panel_off(uint32_t key)
{
    if (key >= TK_NOTES)
        return 0;
    uint16_t mask = (uint16_t)(1u << key);
    if (canceled_panel & mask) {
        canceled_panel &= (uint16_t)~mask;
        return 1;
    }
    if (!(panel_claims & mask))
        return 0;
    panel_claims &= (uint16_t)~mask;
    tk_service_release(panel_track[key], TK_PANEL, 0, key);
    tk_native_drain();
    return 1;
}

int tk_panel_retrig(uint32_t key, uint32_t track)
{
    if (key >= TK_NOTES || !polling)
        return 0;
    uint16_t mask = (uint16_t)(1u << key);
    bool held = (retrig_keys & mask) != 0;
    if (held) {
        uint8_t owner = retrig_track[key];
        retrig_keys &= (uint16_t)~mask;
        bool owner_held = false;
        for (uint8_t other = 0; other < TK_NOTES; ++other)
            if ((retrig_keys & (1u << other)) && retrig_track[other] == owner)
                owner_held = true;
        if (!owner_held)
            tk_service_manual_retrig(owner, false);
    }
    if (track < TK_TRACKS && (registered & (1u << track)) && kind[track] != TK_NATIVE_MIDI) {
        tk_panel_off(key);
        retrig_track[key] = (uint8_t)track;
        retrig_keys |= mask;
        tk_service_manual_retrig((uint8_t)track, true);
        tk_native_drain();
    }
    return held;
}
