#include "track-native.h"

#include <dk/platform/ui.h>
#include "dkrt.h"

/* Experimental panel audition. This adapter keeps one scratch pattern and
 * releases ownership on pattern changes. No stock pattern bytes are edited.
 */
extern uint32_t pp_current_pattern(void);
extern uint32_t pp_selected_tempo(void);
extern volatile uint32_t pp_position[16];
extern dk_ui *dk_ui_core_handle(void);

static TkPattern scratch;
static uint32_t pattern, block, boundary, block_fraction;
static uint16_t registered;
static uint8_t kind[TK_TRACKS], panel_track[TK_NOTES];
static uint16_t panel_claims, canceled_panel;
volatile uint32_t tk_panel_claim_hits;
static bool polling;
static TkNotesDraft draft;
static dk_ui_menu *notes_menu, *note_menu;
static int32_t note_index, note_offset, note_velocity;

static void close_notes(void)
{
    if (note_menu && dk_ui_menu_is_open(note_menu))
        dk_ui_menu_close(note_menu);
    if (notes_menu && dk_ui_menu_is_open(notes_menu))
        dk_ui_menu_close(notes_menu);
    draft.open = false;
}

static uint32_t pattern_snapshot(void)
{
    uint32_t before = pp_position[2];
    uint32_t flags = pp_position[5], audible = pp_position[6];
    uint32_t after = pp_position[2];
    if (before != after)
        return pattern;
    if (flags & 2u)
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
    uint32_t next = pattern_snapshot();
    if (next == pattern)
        return next != 0;
    tk_panel_cancel();
    close_notes();
    tk_service_stop();
    tk_pattern_init(&scratch);
    tk_service_edit_pattern(next ? &scratch : NULL);
    pattern = next;
    return next != 0;
}

static void poll(void)
{
    refresh_pattern();
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        uint16_t mask = (uint16_t)(1u << track);
        if ((registered & mask) && tk_native_kind(track) != kind[track]) {
            tk_service_set_backend(track, NULL);
            registered &= (uint16_t)~mask;
        }
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
        tk_panel_cancel();
        elapsed = 1500u;
    }
    uint32_t time = elapsed * 2000u + block_fraction;
    block_fraction = time % 3u;
    tk_service_tick(time / 3u, (uint16_t)pp_selected_tempo());
    tk_native_drain();
}

void tk_runtime_touch(uint8_t track)
{
    if (track >= TK_TRACKS || !refresh_pattern())
        return;
    if (!polling) {
        block = dkrt_blocks();
        if (dkrt_poll_add((uintptr_t)poll) != 0)
            return;
        polling = true;
    }
    if (draft.open && draft.track != track)
        close_notes();
    uint16_t mask = (uint16_t)(1u << track);
    if ((registered & mask) || !tk_native_idle(track))
        return;
    int type = tk_native_kind(track);
    TkTrackBackend backend;
    if (type == TK_NATIVE_UNSUPPORTED)
        return;
    tk_native_backend(type == TK_NATIVE_MIDI, &backend);
    if (!tk_service_set_backend(track, &backend))
        return;
    kind[track] = (uint8_t)type;
    registered |= mask;
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
    if (draft.open && draft.learn && draft.track == track &&
        !tk_notes_append(&draft, (uint8_t)pitch, (uint8_t)velocity))
        dk_ui_toast(dk_ui_core_handle(), "NOTES FULL");
    poll();
    if (!tk_service_press((uint8_t)track, TK_PANEL, 0, key, (uint8_t)pitch, (uint8_t)velocity))
        return 0;
    panel_track[key] = (uint8_t)track;
    panel_claims |= mask;
    ++tk_panel_claim_hits;
    tk_service_tick(0, (uint16_t)pp_selected_tempo());
    tk_native_drain();
    return 1;
}

static void notes_apply(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (tk_notes_commit(&draft))
        dk_ui_menu_close(notes_menu);
    else
        dk_ui_toast(dk_ui_core_handle(), "PLAY NOTES FIRST");
}

static void notes_learn(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    tk_notes_learn(&draft);
    dk_ui_menu_close(notes_menu);
    dk_ui_toast(dk_ui_core_handle(), "PLAY KEYS. CLICK CHRD TO SAVE");
}

static int32_t notes_back(void *unused)
{
    (void)unused;
    draft.open = false;
    return 0;
}

static char *decimal(char *out, unsigned value)
{
    if (value >= 100)
        *out++ = (char)('0' + value / 100);
    if (value >= 10)
        *out++ = (char)('0' + value / 10 % 10);
    *out++ = (char)('0' + value % 10);
    return out;
}

static void note_apply(void *unused, int32_t value)
{
    (void)unused;
    (void)value;
    if (!draft.open || note_index < 0 || note_index >= draft.recipe.count)
        return;
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
    --draft.recipe.count;
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
    TkRecipeNote note = draft.recipe.notes[index];
    draft.recipe.notes[index] = draft.recipe.notes[to];
    draft.recipe.notes[to] = note;
    dk_ui_menu_refresh(notes_menu);
    dk_ui_menu_select(notes_menu, to + 2);
}

static void notes_panel(dk_ui_menu *m, int32_t row, void *unused)
{
    (void)unused;
    int32_t n = row - 2;
    if (n < 0 || n >= draft.recipe.count)
        return;
    if (n > 0)
        dk_ui_menu_add_action(m, "MOVE UP", note_move, NULL, 2 * n);
    if (n + 1 < draft.recipe.count)
        dk_ui_menu_add_action(m, "MOVE DOWN", note_move, NULL, 2 * n + 1);
    dk_ui_menu_add_action(m, "REMOVE", note_remove, NULL, n);
}

static void notes_fill(dk_ui_menu *m, void *unused)
{
    (void)unused;
    dk_ui_menu_add_action(m, "APPLY USER", notes_apply, NULL, 0);
    dk_ui_menu_add_action(m, "LEARN", notes_learn, NULL, 0);
    for (uint8_t i = 0; i < draft.recipe.count; ++i) {
        char text[DK_UI_ROW];
        char *w = decimal(text, i + 1u);
        *w++ = ' ';
        int n = draft.recipe.notes[i].interval;
        *w++ = n < 0 ? '-' : '+';
        w = decimal(w, (unsigned)(n < 0 ? -n : n));
        *w++ = ' ';
        *decimal(w, draft.recipe.notes[i].velocity) = 0;
        dk_ui_menu_add_action(m, text, note_open, NULL, i);
    }
}

bool tk_trig_open_notes(uint8_t track)
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
    if (!draft.open && !tk_notes_open(track, 0, false, &draft))
        return false;
    draft.learn = false;
    dk_ui_menu_select(notes_menu, 0);
    if (dk_ui_menu_open(notes_menu) < 0) {
        draft.open = false;
        return false;
    }
    return true;
}

bool tk_trig_open_pool(uint8_t track)
{
    if (!tk_trig_ready(track))
        return false;
    return dk_ui_toast(dk_ui_core_handle(), kind[track] == TK_NATIVE_MIDI
                                                ? "MIDI: UP TO 16 NOTES"
                                                : "AUDIO: ONE VOICE") == 0;
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
