#include "keyboard-panel.h"
/* OS1.17 KeyboardView supplies physical key, resolved track and pitch here.
 * Intercept before its stock mono audition and held-note vector. The caller
 * still registers the native key-up closure, so scale/octave edits cannot
 * change the pitch released. No raw button-to-note translation lives here.
 * The track keyboard plays every track it owns, CANOPY's included; CANOPY has
 * no panel path of its own. */
#include <stdint.h>
extern uint32_t dkrt_instrument_type(void);
extern uint8_t *ip_panel_track_object(uint32_t, uint32_t);
extern uint32_t ip_panel_track_type(uint8_t *);
extern uint32_t ip_panel_selected_track(uint32_t);
extern uint8_t ip_panel_active(uint32_t);
extern uint32_t ip_panel_project_settings(uint32_t);
extern uint32_t ip_panel_trig_mode(uint32_t);
extern const uint8_t ip_panel_trig_return[];
extern void ip_panel_stock_toggle(uint32_t);
extern void ip_panel_original_on(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                 uint32_t, uint32_t);
extern void ip_panel_original_off(uint8_t *, uint32_t);
extern uint32_t ip_panel_stock_key(uint8_t *, uint8_t *);
extern void ip_panel_stock_detach(uint8_t *, uint8_t *);
extern uint8_t ip_panel_live_record(uint32_t);
extern uint8_t ip_panel_trig_held(uint32_t);
extern uint8_t ip_panel_step_record(uint32_t);
extern uint32_t ip_panel_key_down(uint32_t);
static uint8_t *panel_view;
static uint32_t panel_track = IP_NONE;
/* Shared step auditions also have a native note-off to run. */
static uint32_t shared_step_keys;
/* Native live recording keeps its physical key-up wrapper. The upper byte
 * stores the original track plus one, and the lower byte stores its pitch. */
static uint16_t shared_live_record[16];
volatile uint32_t ip_panel_debug[8];
/* Key presses handed to the stock path for recording (proof counter). */
volatile uint32_t ip_panel_recorded;
/* The last press the track keyboard plays outside recording (a key in keyboard mode, a TRIG press): key, track, pitch and
 * velocity as the stock caller passed them (evidence for the device tests). */
volatile uint32_t ip_panel_press[4];
static void press_seen(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity)
{
    ip_panel_press[0] = key;
    ip_panel_press[1] = track;
    ip_panel_press[2] = pitch;
    ip_panel_press[3] = velocity;
}
static uint32_t word(const uint8_t *p, uint32_t n)
{
    return *(const uint32_t *)(p + n);
}
/* DT2 1.17 pattern object (finding 349): each step's notes are a linked
 * list of 6-byte entries {track, step, note, velocity, ...} at +0x6a50, with
 * the head entry index of (track, step) at +0x12a50 (s16, negative = none). */
enum { NL_ENTRIES = 0x6a50, NL_HEAD = 0x12a50, NL_COUNT = 8192 };
static int32_t note_head(const uint8_t *p, uint32_t track, uint32_t step)
{
    const uint8_t *h = p + NL_HEAD + 2 * (128 * track + step);
    return (int16_t)((h[0] << 8) | h[1]);
}
/* The step's first note, or -1. */
static int32_t step_note(const uint8_t *p, uint32_t track, uint32_t step)
{
    int32_t head = note_head(p, track, step);
    if (head < 0 || head >= NL_COUNT)
        return -1;
    const uint8_t *e = p + NL_ENTRIES + 6 * (uint32_t)head;
    return e[0] == track && e[1] == step && e[2] < 128 ? e[2] : -1;
}
extern int tk_panel_on(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity)
    __attribute__((weak));
extern int tk_panel_trig_on(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity)
    __attribute__((weak));
extern int tk_panel_off(uint32_t key) __attribute__((weak));
/* The note action's caller is the stock performance handler: a TRIG press outside keyboard mode auditions the key's track.
 * (The host driver defines it: there the caller is the test.) */
#ifndef IP_PANEL_PERFORMANCE_CALLER
#define IP_PANEL_PERFORMANCE_CALLER() (__builtin_return_address(0) == ip_panel_trig_return)
#endif
extern int tk_panel_native_record_on(uint32_t key, uint32_t track, uint32_t pitch,
                                     uint32_t velocity) __attribute__((weak));
extern void tk_panel_record_audition(uint32_t track, uint32_t pitch) __attribute__((weak));
extern int tk_panel_retrig(uint32_t key, uint32_t track) __attribute__((weak));
extern void tk_panel_cancel(void) __attribute__((weak));
extern void tk_panel_focus(uint32_t track) __attribute__((weak));
extern void tk_capture_panel_press(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
    __attribute__((weak));
extern void tk_capture_panel_release(uint32_t key) __attribute__((weak));
extern void tk_capture_note_list_commit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
    __attribute__((weak));

/* A CANOPY track: its machine is the admitted instrument type. */
static int canopy_track(uint8_t *view, uint32_t track)
{
    uint32_t type = dkrt_instrument_type();
    uint8_t *object =
        track < 16 && type >= 7 && type < 32 ? ip_panel_track_object(word(view, 0xa4), track) : 0;
    return object && ip_panel_track_type(object) == type;
}
void ip_panel_on(uint8_t *view, uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity,
                 uint32_t a, uint32_t b, uint32_t c)
{
    int keyboard = ip_panel_active(word(view, 0xa0));
    /* KeyboardView +0x98 holds the recorder state. */
    uint32_t seq = word(view, 0x98);
    int live = seq && ip_panel_live_record(seq), held = seq && ip_panel_trig_held(seq),
        step = seq && ip_panel_step_record(seq);
    int recording = live || held || step;
    /* Step recording takes its notes with KEYBOARD held while keyboard mode is off (finding 349): such a press is a keyboard
     * note too, so a CANOPY track's entry sounds through the track keyboard. */
    int entry = step && !keyboard && key < 16 && ip_panel_key_down(0x12);
    int canopy = (keyboard || entry) && key < 16 && canopy_track(view, track);
    /* Step entry and a held trig on a CANOPY track: the stock note action
     * records the note and the track keyboard sounds it (CANOPY has no stock
     * voice of its own to audition with). */
    int shared_step = canopy && (step || held) && !live && IP_HAVE(tk_panel_on);
    if (key < 16 && shared_live_record[key])
        return;
    if (IP_HAVE(tk_capture_panel_press) && recording)
        tk_capture_panel_press(key, track, pitch, velocity, !live && (step || held));
    if (keyboard && live && !held && !step && key < 16 && IP_HAVE(tk_panel_record_audition) &&
        IP_HAVE(tk_panel_native_record_on) &&
        tk_panel_native_record_on(key, track, pitch, velocity)) {
        ++ip_panel_recorded;
        shared_live_record[key] = (uint16_t)(((track + 1u) << 8) | pitch);
        tk_panel_record_audition(track, pitch);
        ip_panel_original_on(view, key, track, pitch, velocity, a, b, c);
        tk_panel_record_audition(16, 0);
        return;
    }
    if (keyboard) {
        if (recording) {
            if (IP_HAVE(tk_panel_cancel) && !shared_step)
                tk_panel_cancel();
        } else if (IP_HAVE(tk_panel_on) && tk_panel_on(key, track, pitch, velocity)) {
            press_seen(key, track, pitch, velocity);
            return;
        }
    }
    if (shared_step) {
        ++ip_panel_recorded;
        ip_panel_original_on(view, key, track, pitch, velocity, a, b, c);
        panel_view = view;
        panel_track = track;
        ++ip_panel_debug[0];
        ip_panel_debug[2] = key;
        ip_panel_debug[3] = track;
        ip_panel_debug[4] = pitch;
        ip_panel_debug[5] = velocity;
        if (tk_panel_on(key, track, pitch, velocity))
            shared_step_keys |= 1u << key;
        return;
    }
    /* The same stock action also auditions keyboard and recording notes.
     * Check the performance caller and its trig mode. */
    if (!keyboard && IP_HAVE(tk_panel_retrig) && key < 16 && track < 16 && pitch < 128 &&
        IP_PANEL_PERFORMANCE_CALLER()) {
        uint32_t controller = word(view, 0x94);
        uint32_t settings = controller ? ip_panel_project_settings(controller) : 0;
        if (settings && ip_panel_trig_mode(settings) == 2)
            tk_panel_retrig(key, track);
        /* A TRIG press that neither records nor retriggers auditions the key's track. CANOPY has no stock voice to audition
         * with: on its track the track keyboard plays the press, and its release ends it (ip_panel_off). */
        else if (!recording && IP_HAVE(tk_panel_trig_on) && canopy_track(view, track) &&
                 tk_panel_trig_on(key, track, pitch, velocity)) {
            press_seen(key, track, pitch, velocity);
            return;
        }
    }
    ip_panel_original_on(view, key, track, pitch, velocity, a, b, c);
}
void ip_panel_off(uint8_t *view, uint32_t key)
{
    if (IP_HAVE(tk_capture_panel_release))
        tk_capture_panel_release(key);
    uint32_t mask = key < 16 ? 1u << key : 0;
    uint16_t recorded_live = mask ? shared_live_record[key] : 0;
    int shared = !(IP_HAVE(tk_panel_retrig) && tk_panel_retrig(key, IP_NONE)) &&
                 IP_HAVE(tk_panel_off) && tk_panel_off(key);
    if (shared && !recorded_live && !(shared_step_keys & mask))
        return;
    shared_step_keys &= ~mask;
    if (key < 16)
        ++ip_panel_debug[1];
    if (recorded_live) {
        shared_live_record[key] = 0;
        tk_panel_record_audition((recorded_live >> 8) - 1u, recorded_live & 0xffu);
    }
    ip_panel_original_off(view, key);
    if (recorded_live)
        tk_panel_record_audition(16, 0);
}
/* Step recording (finding 349): with KEYBOARD held, a trig key writes its
 * note on the cursor step at once, inside the stock KeyboardView key handler,
 * without the note action (ip_panel_on). */
static int step_recording(uint8_t *view, uint32_t *track_out)
{
    uint32_t seq = word(view, 0x98);
    if (!seq || !ip_panel_step_record(seq))
        return 0;
    uint32_t track = ip_panel_selected_track(word(view, 0xa0));
    if (!canopy_track(view, track))
        return 0;
    *track_out = track;
    return 1;
}
/* Every stock note commit goes through the note-list writer
 * 0x400392be(editor, track, step, records) (hooked below); records are 4-byte
 * {note, velocity, ...} items. The track keyboard's capture takes each commit
 * once the stock writer has updated the pattern. */
extern void ip_note_list_original(uint8_t *, uint32_t, uint32_t, uint32_t *);
void ip_note_list_write(uint8_t *editor, uint32_t track, uint32_t step, uint32_t *records)
{
    uint32_t note = 128;
    uint32_t velocity = 0;
    if (records && IP_HAVE(tk_capture_note_list_commit) && track < 16 && step < 128) {
        const uint8_t *first = (const uint8_t *)(uintptr_t)records[0],
                      *end = (const uint8_t *)(uintptr_t)records[1];
        if (first && first < end && first[0] < 128) {
            note = first[0];
            velocity = first[1];
        }
    }
    /* Publish only after the stock writer has finished updating the pattern. */
    ip_note_list_original(editor, track, step, records);
    if (note < 128 && editor) {
        typedef uint32_t (*EditorNotes)(uint8_t *);
        uint32_t vtable = word(editor, 0);
        EditorNotes get_notes =
            vtable ? (EditorNotes)(uintptr_t)word((const uint8_t *)(uintptr_t)vtable, 0x28) : 0;
        uint32_t notes = get_notes ? get_notes(editor) : 0;
        if (notes >= NL_ENTRIES &&
            step_note((const uint8_t *)(uintptr_t)(notes - NL_ENTRIES), track, step) == (int)note)
            tk_capture_note_list_commit(notes, track, step, note, velocity);
    }
}
void ip_panel_update(uint8_t *view)
{
    uint32_t manager = word(view, 0xa0);
    uint32_t selected = ip_panel_active(manager) ? ip_panel_selected_track(manager) : IP_NONE;
    uint32_t step_track;
    /* KEYBOARD is only held during step entry: keep the entered notes. */
    int keep_step =
        panel_view == view && step_recording(view, &step_track) && step_track == panel_track;
    if (IP_HAVE(tk_panel_focus))
        tk_panel_focus(keep_step ? step_track : selected);
    if (keep_step)
        return;
    if (panel_view == view && selected != panel_track) {
        panel_view = 0;
        ++ip_panel_debug[6];
    }
    /* The pinned stock KeyboardView update is an empty return. Unlike a
     * key callback, this also sees selection handled by another view. */
}
void ip_panel_toggle(uint32_t manager)
{
    ip_panel_stock_toggle(manager);
    if (IP_HAVE(tk_panel_cancel) && !ip_panel_active(manager))
        tk_panel_cancel();
    /* KeyboardView is retained when the main controller toggles KEYBOARD.
     * Its detach callback therefore cannot be the only release boundary. */
    if (panel_view && !ip_panel_active(manager)) {
        panel_view = 0;
        ++ip_panel_debug[6];
    }
}
uint32_t ip_panel_key_event(uint8_t *view, uint8_t *event)
{
    /* Trig keys are codes 0x19..0x28 (physical keys 0..15). */
    uint32_t key = event ? word(event, 0xc) - 0x19u : 16u;
    /* Firmware flags: bit 0 is press, bit 3 is auto-repeat, bit 4 is
     * release. The note-off callback may run before the release event. */
    uint32_t flags = event ? word(event, 0x10) : 0;
    int release = key < 16 && (flags & 0x10u);
    int step_press = key < 16 && (flags & 1u) && !(flags & 8u) && ip_panel_key_down(0x12);
    if (IP_HAVE(tk_capture_panel_press) && step_press) {
        uint32_t seq = word(view, 0x98);
        if (seq && ip_panel_step_record(seq))
            tk_capture_panel_press(key, ip_panel_selected_track(word(view, 0xa0)), 128, 100, 1);
    }
    if (IP_HAVE(tk_capture_panel_release) && release)
        tk_capture_panel_release(key);
    uint32_t result = ip_panel_stock_key(view, event);
    if (release) {
        /* Some native paths finish the key event without a note-off callback. */
        uint32_t mask = 1u << key;
        if (shared_live_record[key]) {
            ip_panel_off(view, key);
        } else if (shared_step_keys & mask) {
            shared_step_keys &= ~mask;
            if (IP_HAVE(tk_panel_off))
                tk_panel_off(key);
        }
    }
    ip_panel_update(view);
    return result;
}
void ip_panel_detach(uint8_t *view, uint8_t *manager)
{
    if (IP_HAVE(tk_panel_cancel))
        tk_panel_cancel();
    if (panel_view == view) {
        panel_view = 0;
        ++ip_panel_debug[7];
    }
    ip_panel_stock_detach(view, manager);
}
