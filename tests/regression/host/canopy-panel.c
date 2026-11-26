/* The KEYBOARD view handler (keyboard-panel/src/cf/panel.c) with the firmware and the track keyboard modeled: CANOPY has no panel
 * path of its own, so its tracks are the track keyboard's to play, and recording goes through the stock note action. */
#include <assert.h>
#include <stdio.h>
#include "../../../runtime/platform/keyboard-panel/src/cf/panel.c"
static uint8_t view[0x200], other[0x200], object[4];
static uint32_t machine = 9, selected, instrument_track = 0, original_on, original_off, detaches;
static uint8_t active = 1, live, held, step_record, keyboard_held, routed = 1, native_record = 1;
static uint32_t tk_downs, tk_ups, tk_cancels, tk_focus = 99, tk_notes, tk_canceled;
static uint32_t native_downs, capture_presses, capture_step, capture_track, capture_pitch;
static uint32_t commits, commit_track, commit_step, commit_note;
static uint32_t guarded_track = 16, guarded_pitch;
static uint32_t seq_object[0x100];
#define SEQ ((uint32_t)(uintptr_t)seq_object)
uint8_t ip_panel_live_record(uint32_t seq)
{
    assert(seq == SEQ);
    return live;
}
uint8_t ip_panel_trig_held(uint32_t seq)
{
    assert(seq == SEQ);
    return held;
}
uint8_t ip_panel_step_record(uint32_t seq)
{
    assert(seq == SEQ);
    return step_record;
}
uint32_t ip_panel_key_down(uint32_t key)
{
    assert(key == 0x12);
    return keyboard_held;
}
uint32_t ip_panel_project_settings(uint32_t controller)
{
    (void)controller;
    return 0;
}
uint32_t ip_panel_trig_mode(uint32_t settings)
{
    (void)settings;
    return 0;
}
const uint8_t ip_panel_trig_return[1] = {0};
uint32_t dkrt_instrument_type(void)
{
    return 9;
}
/* The track's object, and its machine type: the instrument type (9) on CANOPY's track. */
static uint32_t object_track;
uint8_t *ip_panel_track_object(uint32_t manager, uint32_t track)
{
    (void)manager;
    object_track = track;
    return track < 16 ? object : 0;
}
uint32_t ip_panel_track_type(uint8_t *p)
{
    assert(p == object);
    return object_track == instrument_track ? machine : 0;
}
uint32_t ip_panel_selected_track(uint32_t manager)
{
    (void)manager;
    return selected;
}
uint8_t ip_panel_active(uint32_t manager)
{
    (void)manager;
    return active;
}
void ip_panel_stock_toggle(uint32_t manager)
{
    (void)manager;
    active ^= 1;
}
void ip_panel_original_on(uint8_t *v, uint32_t key, uint32_t track, uint32_t pitch,
                          uint32_t velocity, uint32_t a, uint32_t b, uint32_t c)
{
    assert(v == view && key < 16 && track < 16 && velocity == 88 && a == 5 && b == 6 && c == 7);
    /* the stock audition is guarded exactly while a live-recorded press runs it */
    if (guarded_track != 16)
        assert(guarded_track == track && guarded_pitch == pitch);
    ++original_on;
}
void ip_panel_original_off(uint8_t *v, uint32_t key)
{
    assert(v == view && key < 16);
    ++original_off;
}
static uint8_t pattern[0x1dc00];
static uint32_t entries;
/* The stock note-list writer behind the hook: it updates the fake pattern (finding 349). */
void ip_note_list_original(uint8_t *editor, uint32_t track, uint32_t step, uint32_t *records)
{
    (void)editor;
    uint8_t *e = pattern + 0x6a50 + 6 * entries;
    e[0] = (uint8_t)track;
    e[1] = (uint8_t)step;
    e[2] = ((const uint8_t *)(uintptr_t)records[0])[0];
    uint8_t *h = pattern + 0x12a50 + 2 * (128 * track + step);
    h[0] = (uint8_t)(entries >> 8);
    h[1] = (uint8_t)entries;
    ++entries;
}
uint32_t ip_panel_stock_key(uint8_t *v, uint8_t *e)
{
    (void)e;
    assert(v == view);
    return 17;
}
void ip_panel_stock_detach(uint8_t *v, uint8_t *m)
{
    (void)m;
    assert(v == view || v == other);
    ++detaches;
}
/* The track keyboard, modeled: it plays the tracks it routes. */
int tk_panel_on(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity)
{
    assert(key < 16 && track < 16 && pitch < 128 && velocity == 88);
    if (!routed)
        return 0;
    tk_notes |= 1u << key;
    ++tk_downs;
    return 1;
}
int tk_panel_off(uint32_t key)
{
    uint32_t bit = 1u << key;
    int owned = !!((tk_notes | tk_canceled) & bit);
    if (tk_notes & bit)
        ++tk_ups;
    tk_notes &= ~bit;
    tk_canceled &= ~bit;
    return owned;
}
int tk_panel_native_record_on(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity)
{
    assert(guarded_track == 16 && live && key < 16 && track < 16 && pitch < 128 && velocity == 88);
    if (!native_record)
        return 0;
    tk_notes |= 1u << key;
    ++native_downs;
    return 1;
}
void tk_panel_record_audition(uint32_t track, uint32_t pitch)
{
    assert((guarded_track == 16) != (track == 16));
    guarded_track = track;
    guarded_pitch = pitch;
}
int tk_panel_retrig(uint32_t key, uint32_t track)
{
    (void)key;
    (void)track;
    return 0;
}
void tk_panel_cancel(void)
{
    tk_canceled |= tk_notes;
    tk_notes = 0;
    ++tk_cancels;
}
void tk_panel_focus(uint32_t track)
{
    tk_focus = track;
}
void tk_capture_panel_press(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity,
                            uint32_t step)
{
    (void)key;
    (void)velocity;
    ++capture_presses;
    capture_track = track;
    capture_pitch = pitch;
    capture_step = step;
}
void tk_capture_panel_release(uint32_t key)
{
    (void)key;
}
void tk_capture_note_list_commit(uint32_t notes, uint32_t track, uint32_t step, uint32_t note,
                                 uint32_t velocity)
{
    assert(notes == (uint32_t)(uintptr_t)(pattern + 0x6a50) && velocity == 100);
    ++commits;
    commit_track = track;
    commit_step = step;
    commit_note = note;
}
/* A stock note editor: its vtable entry +0x28 gives the pattern's note entries. */
static uint32_t editor_notes(uint8_t *editor)
{
    (void)editor;
    return (uint32_t)(uintptr_t)(pattern + 0x6a50);
}
static uint32_t editor_vtable[16], editor[4];
static void press(uint32_t key, uint32_t track, uint32_t pitch)
{
    ip_panel_on(view, key, track, pitch, 88, 5, 6, 7);
}
int main(void)
{
    *(uint32_t *)(view + 0x98) = SEQ;
    for (unsigned i = 0; i < 16 * 128; ++i)
        pattern[0x12a50 + 2 * i] = pattern[0x12a51 + 2 * i] = 0xff;

    /* Playing: the track keyboard takes every track it routes, CANOPY's too; the stock action runs for none of them. */
    press(3, 0, 65);
    assert(tk_downs == 1 && !original_on);
    ip_panel_off(view, 3);
    assert(tk_ups == 1 && !original_off);
    press(3, 1, 65);
    assert(tk_downs == 2 && !original_on);
    ip_panel_off(view, 3);
    /* A track it does not route stays stock: the stock action sounds and releases it. */
    routed = 0;
    press(3, 0, 65);
    assert(tk_downs == 2 && original_on == 1);
    ip_panel_off(view, 3);
    assert(original_off == 1);
    routed = 1;

    /* Live recording on CANOPY's track: the track keyboard sounds the note while the stock note action records it, with the
     * stock audition guarded for that track and pitch; the release keeps the original track after the selection changes. */
    live = 1;
    press(8, 0, 60);
    assert(native_downs == 1 && original_on == 2 && guarded_track == 16 && ip_panel_recorded == 1);
    assert(capture_presses == 1 && !capture_step);
    live = 0;
    selected = 1;
    ip_panel_off(view, 8);
    assert(original_off == 2 && !tk_notes && guarded_track == 16);
    selected = 0;
    /* A track the track keyboard does not record natively is stock live recording. */
    live = 1;
    native_record = 0;
    press(8, 2, 60);
    assert(native_downs == 1 && original_on == 3 && tk_cancels == 1);
    ip_panel_off(view, 8);
    native_record = 1;
    live = 0;

    /* Step entry on CANOPY's track: the stock note action records, the track keyboard auditions, and both releases pair. */
    step_record = 1;
    unsigned downs = tk_downs, cancels = tk_cancels;
    press(9, 0, 62);
    assert(original_on == 4 && tk_downs == downs + 1 && tk_cancels == cancels && capture_step);
    assert(ip_panel_recorded == 2 && (tk_notes & (1u << 9)));
    ip_panel_off(view, 9);
    assert(!tk_notes && original_off == 4);
    /* The same on another track: stock records and auditions, the track keyboard's notes are cancelled. */
    press(9, 1, 62);
    assert(original_on == 5 && tk_downs == downs + 1 && tk_cancels == cancels + 1);
    ip_panel_off(view, 9);
    step_record = 0;
    /* A held trig on CANOPY's track takes the note the same way. */
    held = 1;
    press(4, 0, 64);
    assert(original_on == 6 && tk_downs == downs + 2 && capture_step);
    ip_panel_off(view, 4);
    held = 0;

    /* Step entry with KEYBOARD held: the stock key handler writes the note without the note action. The track keyboard's
     * capture takes the press and, after the stock writer, the committed note. */
    {
        uint32_t ev[8] = {0};
        ev[3] = 0x19 + 5;
        ev[4] = 1;
        step_record = keyboard_held = 1;
        selected = 0;
        unsigned presses = capture_presses;
        assert(ip_panel_key_event(view, (uint8_t *)ev) == 17);
        assert(capture_presses == presses + 1 && capture_track == 0 && capture_pitch == 128 &&
               capture_step);
        editor[0] = (uint32_t)(uintptr_t)editor_vtable;
        editor_vtable[0x28 / 4] = (uint32_t)(uintptr_t)editor_notes;
        static uint8_t record[4] = {67, 100, 0, 0};
        uint32_t records[2] = {(uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)(record + 4)};
        ip_note_list_write((uint8_t *)editor, 0, 20, records);
        assert(commits == 1 && commit_track == 0 && commit_step == 20 && commit_note == 67);
        assert(step_note(pattern, 0, 20) == 67);
        /* A write with no editor is the stock writer alone. */
        ip_note_list_write(0, 0, 21, records);
        assert(commits == 1 && step_note(pattern, 0, 21) == 67);
        /* The release event of a shared step audition releases it even without a note-off callback. */
        keyboard_held = 0;
        press(6, 0, 60);
        assert(tk_notes & (1u << 6));
        ev[3] = 0x19 + 6;
        ev[4] = 0x10;
        ip_panel_key_event(view, (uint8_t *)ev);
        assert(!(tk_notes & (1u << 6)));
        step_record = 0;
    }

    /* Step entry with KEYBOARD held while keyboard mode is off: the note action runs, the stock action records the note and the
     * track keyboard sounds it on CANOPY's track; the note-off pairs both. On another track the stock action alone sounds it. */
    step_record = keyboard_held = 1;
    active = 0;
    downs = tk_downs;
    unsigned ons = original_on, offs = original_off;
    press(10, 0, 69);
    assert(original_on == ons + 1 && tk_downs == downs + 1 && (tk_notes & (1u << 10)));
    ip_panel_off(view, 10);
    assert(!(tk_notes & (1u << 10)) && original_off == offs + 1);
    press(10, 2, 69);
    assert(original_on == ons + 2 && tk_downs == downs + 1);
    ip_panel_off(view, 10);
    /* Without KEYBOARD held the press is no keyboard note: stock alone. */
    keyboard_held = 0;
    press(10, 0, 69);
    assert(original_on == ons + 3 && tk_downs == downs + 1);
    ip_panel_off(view, 10);
    active = 1;
    step_record = 0;

    /* While a step entry holds KEYBOARD on CANOPY's track, the track keyboard keeps its focus there. */
    step_record = 1;
    press(7, 0, 60);
    active = 0;
    ip_panel_update(view);
    assert(tk_focus == 0);
    step_record = 0;
    ip_panel_update(view);
    assert(tk_focus == IP_NONE);
    active = 1;
    ip_panel_off(view, 7);

    /* Leaving keyboard mode and detaching the view cancel the track keyboard's panel notes. */
    cancels = tk_cancels;
    ip_panel_toggle(0);
    assert(!active && tk_cancels == cancels + 1);
    ip_panel_toggle(0);
    assert(active && tk_cancels == cancels + 1);
    ip_panel_detach(other, 0);
    ip_panel_detach(view, 0);
    assert(detaches == 2 && tk_cancels == cancels + 3);
    puts("ok");
    return 0;
}
