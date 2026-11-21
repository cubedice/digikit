#include "track-service.h"

#ifdef __m68k__
#pragma GCC optimize("no-jump-tables")
#endif

#include "dk_fw_draw.h"
#include "dk_fw_mem.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"

extern uint32_t tk_trig_stock_track(uint32_t);
extern uint32_t tk_trig_stock_alloc(uint32_t);
extern uint8_t tk_trig_stock_key(uint8_t *, uint8_t *);
extern uint8_t tk_trig_stock_turn(uint8_t *, uint8_t *);
extern void tk_trig_stock_draw(uint8_t *, uint8_t *);
extern uint32_t tk_trig_stock_parameter(uint8_t *, uint32_t);
extern uint8_t tk_trig_stock_held(uint32_t);
extern uint8_t tk_trig_stock_live_record(uint32_t);
extern void tk_trig_stock_mark_edited(uint32_t, uint32_t);
extern const uint32_t tk_trig_label_font;
extern void tk_runtime_touch(uint8_t track);
extern bool tk_trig_lock_batch(uint8_t track, const uint32_t steps[4], enum TkControl control,
                               int value, bool clear) __attribute__((weak));
extern uint16_t tk_trig_pitch_display(uint8_t track, uint16_t *held) __attribute__((weak));
extern bool tk_trig_open_notes(uint8_t track, int step) __attribute__((weak));
extern bool tk_trig_open_pool(uint8_t track) __attribute__((weak));

/* DTII OS 1.17 ParameterPageView fields. The installed vtable is specific to
 * the TRIG view. SRC, AMP, FX and MOD use other vtables. */
enum {
    EDITOR = 0x6c,
    TRACK_MODEL = 0x74,
    PAGES_BEGIN = 0x7c,
    PAGES_END = 0x80,
    PAGES_CAPACITY = 0x84,
    PAGE_INDEX = 0x90,
    SHOW_VALUES = 0x190,
    HELD_STEP_MASK = 0x258,
    EVENT_NUMBER = 12,
    EVENT_VALUE = 16,
    EVENT_PUSHED = 20,
    FIRST_ENCODER_KEY = 41,
    TRACK_LEVEL_INDEX = 8,
    AUDIO_TRIG_ID = 1,
    AUDIO_RETRIG_ID = 2,
    MIDI_TRIG_ID = 23,
    MIDI_RETRIG_ID = 24,
    STOCK_PAGES = 2,
    ARP_PAGE = 2,
    KEYS_PAGE = 3,
    ALL_PAGES = 4,
};

typedef struct {
    const char *label;
    enum TkControl control;
    int16_t low, high;
    bool discrete;
} TrigCell;

static const TrigCell arp[] = {
    {"ARP", TK_CTL_ARP, 0, 1, true},       {"ORDR", TK_CTL_ORDER, 0, 4, true},
    {"GATE", TK_CTL_GATE, 1, 100, false},  {"RATE", TK_CTL_RATE, 0, TK_RATE_5Q, true},
    {"OCTS", TK_CTL_OCTAVES, -3, 3, true}, {"SWNG", TK_CTL_SWING, 50, 75, false},
    {"RST", TK_CTL_RESET, 0, 2, true},     {"CHRD", TK_CTL_CHORD, 0, TK_CHORD_COUNT - 1, true},
};
static const TrigCell keys[] = {
    {"POLY", TK_CTL_POLY, 0, 1, true},          {"CHRD", TK_CTL_CHORD, 0, TK_CHORD_COUNT - 1, true},
    {"VOIC", TK_CTL_VOICING, 0, 3, true},       {"STRM", TK_CTL_STRUM, 0, 500, false},
    {0, TK_CONTROL_COUNT, 0, 0, false},         {0, TK_CONTROL_COUNT, 0, 0, false},
    {"VCES", TK_CTL_VOICES, 1, TK_NOTES, true}, {"POOL", TK_CONTROL_COUNT, 0, 0, false},
};

typedef struct {
    uint8_t track, page, encoder, step;
    int32_t residue;
    bool use;
} EncoderTrace;
static EncoderTrace trace;
static uint8_t click_track, click_page, click_key;
static int click_step;
static bool click_modified;
static uint8_t owned_clicks;

static uint32_t word(const uint8_t *p, uint16_t offset)
{
    return *(const uint32_t *)(p + offset);
}

static void put(uint8_t *p, uint16_t offset, uint32_t value)
{
    *(uint32_t *)(p + offset) = value;
}

static uint8_t current_track(const uint8_t *view)
{
    uint32_t model = word(view, TRACK_MODEL);
    uint32_t track = model ? tk_trig_stock_track(model) : UINT32_MAX;
    return track < TK_TRACKS ? (uint8_t)track : TK_TRACKS;
}

/* The stock destructor still frees this vector. Use the firmware allocator,
 * preserve the first two IDs, and reuse their descriptors behind custom pages.
 * The custom parameter resolver hides A–H from stock editor operations. */
static bool update_pages(uint8_t *view, uint8_t track)
{
    uint32_t begin = word(view, PAGES_BEGIN), end = word(view, PAGES_END);
    uint32_t capacity = word(view, PAGES_CAPACITY);
    if (!begin || (begin & 3u) || capacity < end || end < begin ||
        (end - begin != STOCK_PAGES * 4u && end - begin != ALL_PAGES * 4u))
        return false;
    uint32_t *list = (uint32_t *)(uintptr_t)begin;
    bool audio = list[0] == AUDIO_TRIG_ID && list[1] == AUDIO_RETRIG_ID;
    bool midi = list[0] == MIDI_TRIG_ID && list[1] == MIDI_RETRIG_ID;
    if (!audio && !midi)
        return false;
    if (track < TK_TRACKS)
        tk_runtime_touch(track);
    bool active = track < TK_TRACKS && tk_trig_ready(track);
    if (active && capacity - begin < ALL_PAGES * 4u) {
        uint32_t fresh = tk_trig_stock_alloc(ALL_PAGES * 4u);
        if (!fresh)
            return false;
        uint32_t *expanded = (uint32_t *)(uintptr_t)fresh;
        expanded[0] = list[0];
        expanded[1] = list[1];
        put(view, PAGES_BEGIN, fresh);
        put(view, PAGES_CAPACITY, fresh + ALL_PAGES * 4u);
        begin = fresh;
        dk_fw_mem_free((uint8_t *)list);
        list = expanded;
    }
    if (active) {
        /* The first MIDI page has a specialized note visualization.
         * Use its second page for the generic stock frame. */
        uint32_t frame = midi ? list[1] : list[0];
        list[ARP_PAGE] = frame;
        list[KEYS_PAGE] = frame;
    } else if (word(view, PAGE_INDEX) >= STOCK_PAGES) {
        put(view, PAGE_INDEX, 0);
    }
    put(view, PAGES_END, begin + (active ? ALL_PAGES : STOCK_PAGES) * 4u);
    return active;
}

/* -1 is a track edit. TK_STEPS is a refused step context. The native held
 * bitset is read only while its owner says this is a held-grid gesture.
 * Refuse multiple held trigs until the service offers one atomic edit. */
static int step_context(const uint8_t *view)
{
    uint32_t editor = word(view, EDITOR);
    if (!editor)
        return TK_STEPS;
    if (!tk_trig_stock_held(editor))
        return tk_trig_stock_live_record(editor) ? TK_STEPS : -1;
    const uint8_t *seq = (const uint8_t *)(uintptr_t)editor;
    int step = TK_STEPS;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t mask = word(seq, HELD_STEP_MASK + 4 * i);
        if (!mask)
            continue;
        if (step < TK_STEPS || (mask & (mask - 1)))
            return TK_STEPS;
        unsigned bit = 0;
        while (!(mask & (1u << bit)))
            ++bit;
        step = (int)(32u * i + bit);
    }
    return step;
}

static bool group_context(const uint8_t *view, int step, uint32_t steps[4])
{
    uint32_t editor = word(view, EDITOR);
    if (step != TK_STEPS || !editor || !tk_trig_lock_batch || !tk_trig_stock_held(editor))
        return false;
    const uint8_t *seq = (const uint8_t *)(uintptr_t)editor;
    uint32_t any = 0;
    for (unsigned i = 0; i < 4; ++i) {
        steps[i] = word(seq, HELD_STEP_MASK + 4 * i);
        any |= steps[i];
    }
    return any != 0;
}

uint32_t tk_trig_parameter(uint8_t *view, uint32_t encoder)
{
    if (word(view, PAGE_INDEX) >= ARP_PAGE && encoder < TRACK_LEVEL_INDEX)
        return 0;
    return tk_trig_stock_parameter(view, encoder);
}

static bool read_controls(uint8_t track, int step, TkParams *out, uint16_t *mask)
{
    *mask = 0;
    if (step >= 0 && step < TK_STEPS)
        return tk_trig_lock_display(track, (uint8_t)step, out, mask);
    return tk_trig_display(track, out);
}

static void draw_value(uint8_t *bitmap, const uint8_t *font, int x, int y, const TrigCell *cell,
                       int value, bool held_values)
{
    static const char *const order[] = {"PLAY", "UP", "DOWN", "UPDN", "RAND"};
    static const char *const reset[] = {"TRIG", "PATT", "CONT"};
    static const char *const voicing[] = {"CLOSE", "OPEN", "INV1", "INV2"};
    static const char *const chord[] = {
        "PLAY", "MAJ",  "MAJ6", "SUS2", "MIN6", "MIN",  "SUS4",
        "ADD9", "DOM7", "DIM",  "MIN7", "USER", "MAJ7",
    };
    uint8_t *typeface = (uint8_t *)font;
    const char *text = 0;
    switch (cell->control) {
    case TK_CTL_ARP:
    case TK_CTL_POLY:
        text = value ? "ON" : "OFF";
        break;
    case TK_CTL_ORDER:
        text = order[value];
        break;
    case TK_CTL_RESET:
        text = reset[value];
        break;
    case TK_CTL_CHORD:
        text = chord[value];
        break;
    case TK_CTL_VOICING:
        text = voicing[value];
        break;
    case TK_CTL_STRUM:
        if (!value)
            text = "OFF";
        else
            dk_fw_text_draw_box(bitmap, typeface, x, y, 0, 0, 0, "%dMS", value);
        break;
    case TK_CTL_GATE:
    case TK_CTL_SWING:
        dk_fw_text_draw_box(bitmap, typeface, x, y, 0, 0, 0, "%d%%", value);
        break;
    case TK_CTL_RATE: {
        uint8_t num, den;
        if (tk_rate_division((uint8_t)value, &num, &den))
            dk_fw_text_draw_box(bitmap, typeface, x, y, 0, 0, 0, num == 1 ? "%d/Q" : "1/%d",
                                (int)den);
        break;
    }
    case TK_CTL_OCTAVES:
        dk_fw_text_draw_box(bitmap, typeface, x, y, 0, 0, 0, "%+d", value);
        break;
    case TK_CTL_VOICES:
        dk_fw_text_draw_box(bitmap, typeface, x, y, 0, 0, 0, "%d", value);
        break;
    default:
        text = held_values ? "INFO" : ">";
        break;
    }
    if (text)
        dk_fw_text_draw_box(bitmap, typeface, x, y, 0, 0, 0, "%s", text);
}

static void draw_keyboard(uint8_t *bitmap, uint8_t track)
{
    static const uint8_t white[] = {0, 2, 4, 5, 7, 9, 11};
    static const uint8_t black[] = {1, 3, 6, 8, 10};
    static const uint8_t gap[] = {0, 1, 3, 4, 5};
    uint16_t held = 0;
    uint16_t sounding = tk_trig_pitch_display ? tk_trig_pitch_display(track, &held) : 0;
    dk_fw_draw_fill_rect(bitmap, 75, 24, 117, 38, 1);
    for (unsigned i = 0; i < sizeof white; ++i) {
        int x = 75 + 6 * (int)i;
        uint16_t mask = (uint16_t)(1u << white[i]);
        dk_fw_draw_fill_rect(bitmap, x, 24, x, 38, 0);
        if (sounding & mask)
            dk_fw_draw_fill_rect(bitmap, x + 2, 25, x + 4, 29, 0);
        else if (held & mask)
            dk_fw_draw_fill_rect(bitmap, x + 3, 26, x + 3, 26, 0);
    }
    dk_fw_draw_fill_rect(bitmap, 117, 24, 117, 38, 0);
    for (unsigned i = 0; i < sizeof black; ++i) {
        int x = 79 + 6 * gap[i];
        uint16_t mask = (uint16_t)(1u << black[i]);
        dk_fw_draw_fill_rect(bitmap, x, 31, x + 3, 38, 0);
        if (sounding & mask)
            dk_fw_draw_fill_rect(bitmap, x + 1, 32, x + 2, 36, 1);
        else if (held & mask)
            dk_fw_draw_fill_rect(bitmap, x + 1, 32, x + 1, 32, 1);
    }
}

void tk_trig_draw(uint8_t *view, uint8_t *bitmap)
{
    uint8_t track = current_track(view);
    bool active = update_pages(view, track);
    uint32_t page = word(view, PAGE_INDEX);
    tk_trig_stock_draw(view, bitmap);
    if (!active || (page != ARP_PAGE && page != KEYS_PAGE))
        return;
    const TrigCell *cells = page == ARP_PAGE ? arp : keys;
    uint8_t *font = (uint8_t *)(uintptr_t)tk_trig_label_font;
    dk_fw_draw_fill_rect(bitmap, 23, 0, 122, 52, 0);
    bool held = view[SHOW_VALUES] != 0;
    /* The anchor yields the font descriptor expected by draw_box. The
     * formatted-text primitive expects a wrapper around that descriptor. */
    dk_fw_text_draw_box(bitmap, font, 24, 45, 0, 0, 0, "%s",
                        held ? "VALUES" : (page == ARP_PAGE ? "ARPEGGIATOR" : "KEYBOARD"));
    dk_fw_draw_fill_rect(bitmap, 23, 43, 121, 43, 1);
    int step = step_context(view);
    TkParams params;
    uint16_t locks;
    if (!read_controls(track, step, &params, &locks) || !tk_valid(&params))
        return;
    if (page == KEYS_PAGE)
        draw_keyboard(bitmap, track);
    for (unsigned i = 0; i < 8; ++i) {
        const TrigCell *cell = &cells[i];
        if (!cell->label)
            continue;
        /* Read sideways by knob row. Each row gives two controls a 49 px
         * label/value budget and leaves the original page dots untouched. */
        int x = 23 + (int)(i / 4u) * 50;
        int y = 34 - (int)(i % 4u) * 9;
        int value = 0;
        bool parameter = tk_get_control(&params, cell->control, &value);
        if (cell->control == TK_CTL_VOICES && value > tk_trig_voice_capacity(track))
            value = tk_trig_voice_capacity(track);
        dk_fw_text_draw_box(bitmap, font, x + 1, y, 0, 0, 0, "%s", cell->label);
        if (held || parameter || (cell->control == TK_CONTROL_COUNT && tk_trig_open_pool))
            draw_value(bitmap, font, x + 24, y, cell, value, held);
        if (parameter && (locks & (1u << cell->control)))
            dk_fw_draw_invert_rect(bitmap, x, y - 1, x + 47, y + 6, 1);
    }
}

static bool action_for(uint32_t page, uint8_t key)
{
    return (page == ARP_PAGE && key == FIRST_ENCODER_KEY + 7 && tk_trig_open_notes) ||
           (page == KEYS_PAGE && ((key == FIRST_ENCODER_KEY + 1 && tk_trig_open_notes) ||
                                  (key == FIRST_ENCODER_KEY + 7 && tk_trig_open_pool)));
}

uint8_t tk_trig_key(uint8_t *view, uint8_t *event)
{
    uint8_t track = current_track(view);
    bool active = update_pages(view, track);
    uint32_t page = word(view, PAGE_INDEX), key = word(event, EVENT_NUMBER);
    uint32_t flags = word(event, EVENT_VALUE);
    if (key < FIRST_ENCODER_KEY || key >= FIRST_ENCODER_KEY + 8)
        return tk_trig_stock_key(view, event);
    uint8_t i = (uint8_t)(key - FIRST_ENCODER_KEY);
    uint8_t bit = (uint8_t)(1u << i);
    if (!(flags & 1u) && (owned_clicks & bit)) {
        owned_clicks &= (uint8_t)~bit;
        if (active && page == click_page && track == click_track && key == click_key &&
            step_context(view) == click_step) {
            const TrigCell *cell = &(page == ARP_PAGE ? arp : keys)[i];
            if (click_step >= 0) {
                if (click_modified && cell->control == TK_CTL_CHORD && tk_trig_open_notes) {
                    tk_trig_open_notes(track, click_step);
                } else if (!click_modified && cell->control < TK_CTL_VOICES) {
                    uint32_t steps[4];
                    tk_trig_stock_mark_edited(word(view, EDITOR), 1);
                    if (group_context(view, click_step, steps))
                        tk_trig_lock_batch(track, steps, cell->control, 0, true);
                    else if (click_step < TK_STEPS)
                        tk_trig_lock_write(track, (uint8_t)click_step, cell->control, 0, true);
                    dk_fw_view_invalidate(view);
                }
            } else if (page == KEYS_PAGE && i == 7 && tk_trig_open_pool) {
                tk_trig_open_pool(track);
            } else if (tk_trig_open_notes) {
                tk_trig_open_notes(track, -1);
            }
        }
        click_key = 0;
        return 1;
    }
    if (!active || (page != ARP_PAGE && page != KEYS_PAGE))
        return tk_trig_stock_key(view, event);
    if (flags & 1u) {
        owned_clicks |= bit;
        if (flags & 8u) /* Key repeats do not create or clear another lock. */
            return 1;
        int step = step_context(view);
        const TrigCell *cell = &(page == ARP_PAGE ? arp : keys)[i];
        bool modified = (flags & 2u) != 0;
        uint32_t steps[4];
        bool held_action =
            step >= 0 && (modified ? step < TK_STEPS && cell->control == TK_CTL_CHORD
                                   : cell->control < TK_CTL_VOICES &&
                                         (step < TK_STEPS || group_context(view, step, steps)));
        click_key = 0;
        if (held_action || (step == -1 && !modified && action_for(page, (uint8_t)key))) {
            click_track = track;
            click_page = (uint8_t)page;
            click_step = step;
            click_modified = modified;
            click_key = (uint8_t)key;
        }
    }
    return 1;
}

uint8_t tk_trig_turn(uint8_t *view, uint8_t *event)
{
    uint8_t track = current_track(view);
    bool active = update_pages(view, track);
    uint32_t page = word(view, PAGE_INDEX), knob = word(event, EVENT_NUMBER);
    if (knob == 0 || knob > 8 || !active || (page != ARP_PAGE && page != KEYS_PAGE))
        return tk_trig_stock_turn(view, event);
    click_key = 0;
    uint8_t i = (uint8_t)(knob - 1u);
    const TrigCell *cell = &(page == ARP_PAGE ? arp : keys)[i];
    int step = step_context(view);
    uint32_t steps[4];
    bool group = group_context(view, step, steps);
    if ((step == TK_STEPS && !group) || cell->control >= TK_CONTROL_COUNT ||
        (step >= 0 && cell->control == TK_CTL_VOICES))
        return 1;
    TkParams params;
    uint16_t locks;
    int current;
    if (!read_controls(track, step, &params, &locks) ||
        !tk_get_control(&params, cell->control, &current))
        return 1;
    int high = cell->control == TK_CTL_VOICES ? tk_trig_voice_capacity(track) : cell->high;
    if (high < cell->low)
        return 1;
    if (current > high)
        current = high;
    int32_t delta = (int32_t)word(event, EVENT_VALUE);
    if (delta > 64)
        delta = 64;
    if (delta < -64)
        delta = -64;
    if (cell->discrete) {
        if (!trace.use || trace.page != page || trace.track != track || trace.encoder != i ||
            trace.step != (uint8_t)step || (delta > 0 && trace.residue < 0) ||
            (delta < 0 && trace.residue > 0))
            trace = (EncoderTrace){track, (uint8_t)page, i, (uint8_t)step, 0, true};
        int sum = trace.residue + delta;
        delta = sum / 4;
        trace.residue = sum % 4;
    } else {
        trace.use = false;
        if (event[EVENT_PUSHED])
            delta *= 4;
    }
    if (!delta)
        return 1;
    int value = current + delta;
    if (value < cell->low)
        value = cell->low;
    if (value > high)
        value = high;
    if (value == current && step < 0)
        return 1;
    if (step < 0)
        tk_trig_write(track, cell->control, value);
    else {
        tk_trig_stock_mark_edited(word(view, EDITOR), 1);
        if (group)
            tk_trig_lock_batch(track, steps, cell->control, value, false);
        else
            tk_trig_lock_write(track, (uint8_t)step, cell->control, value, false);
    }
    dk_fw_view_invalidate(view);
    return 1;
}

uint8_t tk_trig_turn_interface(uint8_t *receiver, uint8_t *event)
{
    return tk_trig_turn(receiver - 4, event);
}
