#include "../src/track-service.h"
#include "dk_fw_draw.h"
#include "dk_fw_mem.h"
#include "dk_fw_text.h"
#include "dk_fw_view.h"

#include <assert.h>
#include <string.h>

void tk_trig_draw(uint8_t *, uint8_t *);
uint8_t tk_trig_key(uint8_t *, uint8_t *);
uint8_t tk_trig_turn(uint8_t *, uint8_t *);
uint8_t tk_trig_turn_interface(uint8_t *, uint8_t *);
uint32_t tk_trig_parameter(uint8_t *, uint32_t);

_Alignas(4) static uint8_t view[0x194], editor[0x270], bitmap[1];
static uint32_t stock_pages[] = {1, 2}, midi_pages[] = {23, 24}, other_pages[] = {7, 8};
static uint32_t pages[4], event[6];
static uint8_t selected_track;
static unsigned allocations, stock_keys, stock_turns, drawn, invalidations, edited, notes;
static unsigned lock_writes, track_writes;
static bool active, held, record;
static TkParams config;
const uint32_t tk_trig_label_font = 0;

static uint32_t read(const uint8_t *p, unsigned offset)
{
    uint32_t result;
    memcpy(&result, p + offset, sizeof result);
    return result;
}

static void write(uint8_t *p, unsigned offset, uint32_t value)
{
    memcpy(p + offset, &value, sizeof value);
}

uint32_t tk_trig_stock_track(uint32_t model)
{
    assert(model == 1);
    return selected_track;
}

uint32_t tk_trig_stock_alloc(uint32_t size)
{
    assert(size == sizeof pages);
    ++allocations;
    return (uint32_t)(uintptr_t)pages;
}

uint8_t tk_trig_stock_held(uint32_t seq)
{
    assert(seq == (uint32_t)(uintptr_t)editor);
    return held;
}

uint8_t tk_trig_stock_live_record(uint32_t seq)
{
    assert(seq == (uint32_t)(uintptr_t)editor);
    return record;
}

void tk_trig_stock_mark_edited(uint32_t seq, uint32_t mark)
{
    assert(seq == (uint32_t)(uintptr_t)editor && mark == 1);
    ++edited;
}

uint8_t tk_trig_stock_key(uint8_t *p, uint8_t *ev)
{
    assert(p == view);
    uint32_t number = read(ev, 12);
    ++stock_keys;
    if (number == 1) {
        unsigned pages_count = (read(p, 0x80) - read(p, 0x7c)) / 4u;
        write(p, 0x90, (read(p, 0x90) + 1u) % pages_count);
    }
    return 0;
}

uint8_t tk_trig_stock_turn(uint8_t *p, uint8_t *ev)
{
    assert(p == view && ev == (uint8_t *)event);
    ++stock_turns;
    return 0;
}

uint32_t tk_trig_stock_parameter(uint8_t *p, uint32_t encoder)
{
    assert(p == view);
    return 100 + encoder;
}

void tk_trig_stock_draw(uint8_t *p, uint8_t *b)
{
    assert(p == view && b == bitmap);
    ++drawn;
}

void dk_fw_mem_free(uint8_t *p)
{
    assert(p == (uint8_t *)stock_pages || p == (uint8_t *)midi_pages);
}

void dk_fw_view_invalidate(uint8_t *p)
{
    assert(p == view);
    ++invalidations;
}

void dk_fw_draw_fill_rect(uint8_t *b, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
{
    assert(b == bitmap && x0 >= 23 && y0 >= 0 && x1 <= 122 && y1 <= 52);
    (void)color;
}

void dk_fw_draw_invert_rect(uint8_t *b, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t color)
{
    dk_fw_draw_fill_rect(b, x0, y0, x1, y1, color);
}

int32_t dk_fw_text_draw_box(uint8_t *b, uint8_t *f, int32_t x, int32_t y, int32_t align,
                            int32_t flags, const char *prefix, const char *fmt, ...)
{
    assert(b == bitmap && x >= 23 && y >= 0 && x < 122 && y <= 52);
    assert(align == 0 && flags == 0 && !prefix);
    (void)f;
    (void)fmt;
    return 0;
}

bool tk_trig_ready(uint8_t track)
{
    return active && track == 0;
}

uint8_t tk_trig_voice_capacity(uint8_t track)
{
    assert(track == 0);
    return 1;
}

void tk_runtime_touch(uint8_t track)
{
    assert(track < TK_TRACKS);
}

bool tk_trig_display(uint8_t track, TkParams *out)
{
    assert(track == 0);
    *out = config;
    return true;
}

bool tk_trig_write(uint8_t track, enum TkControl control, int value)
{
    assert(track == 0 && !held && !record);
    ++track_writes;
    return tk_set_control(&config, control, value);
}

bool tk_trig_lock_display(uint8_t track, uint8_t step, TkParams *out, uint16_t *mask)
{
    assert(track == 0 && step == 17 && held);
    *out = config;
    out->chord = TK_CHORD_MIN;
    *mask = 1u << TK_CTL_CHORD;
    return true;
}

bool tk_trig_lock_write(uint8_t track, uint8_t step, enum TkControl control, int value, bool clear)
{
    assert(track == 0 && step == 17 && held && control == TK_CTL_CHORD);
    assert(clear ? value == 0 : value == TK_CHORD_SUS4);
    ++lock_writes;
    return true;
}

bool tk_trig_open_notes(uint8_t track)
{
    assert(track == 0 && !held && !record);
    ++notes;
    return true;
}

static uint8_t key(unsigned number, unsigned flags)
{
    event[3] = number;
    event[4] = flags;
    return tk_trig_key(view, (uint8_t *)event);
}

static uint8_t turn(unsigned encoder, int count)
{
    event[3] = encoder;
    event[4] = (uint32_t)count;
    return tk_trig_turn_interface(view + 4, (uint8_t *)event);
}

static void init_vector(uint32_t *original)
{
    write(view, 0x7c, (uint32_t)(uintptr_t)original);
    write(view, 0x80, (uint32_t)(uintptr_t)(original + 2));
    write(view, 0x84, (uint32_t)(uintptr_t)(original + 2));
    write(view, 0x90, 0);
}

int main(void)
{
    write(view, 0x6c, (uint32_t)(uintptr_t)editor);
    write(view, 0x74, 1);
    init_vector(stock_pages);
    config = tk_defaults();
    tk_trig_draw(view, bitmap);
    assert(drawn == 1 && !allocations);
    assert(tk_trig_parameter(view, 0) == 100);

    active = true;
    key(1, 16);
    assert(read(view, 0x90) == 1 && allocations == 1);
    key(1, 16);
    assert(read(view, 0x90) == 2 && pages[0] == 1 && pages[1] == 2);
    tk_trig_draw(view, bitmap);
    assert(tk_trig_parameter(view, 0) == 0 && tk_trig_parameter(view, 8) == 108);
    assert(turn(1, 2) && !track_writes);
    assert(turn(1, 2) && track_writes == 1 && config.arp == 1);
    turn(9, 4);
    assert(stock_turns == 1);
    key(48, 1);
    turn(8, 4);
    key(48, 16);
    assert(!notes && config.chord == TK_CHORD_MAJ);
    key(48, 1);
    key(48, 16);
    assert(notes == 1);

    held = true;
    write(editor, 0x258, 1u << 17);
    tk_trig_draw(view, bitmap);
    turn(8, 4);
    key(48, 1);
    key(48, 16);
    assert(lock_writes == 2 && edited == 2 && notes == 1 && track_writes == 2);
    write(editor, 0x258, (1u << 15) | (1u << 17));
    assert(turn(8, 4) && key(48, 1) && key(48, 16));
    assert(lock_writes == 2 && track_writes == 2);
    held = false;
    record = true;
    assert(turn(8, 4) && track_writes == 2);
    record = false;

    key(1, 16);
    assert(read(view, 0x90) == 3);
    tk_trig_draw(view, bitmap);
    assert(turn(7, 4) && track_writes == 2);
    key(42, 1);
    selected_track = 5;
    key(42, 16);
    assert(!read(view, 0x90) && read(view, 0x80) - read(view, 0x7c) == 8);
    assert(notes == 1);
    selected_track = 0;
    tk_trig_draw(view, bitmap);
    assert(allocations == 1 && read(view, 0x80) - read(view, 0x7c) == 16);
    assert(stock_keys == 3 && invalidations == 4);

    init_vector(other_pages);
    tk_trig_draw(view, bitmap);
    assert(allocations == 1 && read(view, 0x7c) == (uint32_t)(uintptr_t)other_pages);

    init_vector(midi_pages);
    key(1, 16);
    assert(allocations == 2 && read(view, 0x90) == 1);
    assert(pages[0] == 23 && pages[1] == 24 && pages[2] == 24 && pages[3] == 24);
    assert(tk_trig_parameter(view, 0) == 100);
    key(1, 16);
    assert(read(view, 0x90) == 2 && tk_trig_parameter(view, 0) == 0);
    tk_trig_draw(view, bitmap);
    key(1, 16);
    assert(read(view, 0x90) == 3 && tk_trig_parameter(view, 8) == 108);
    active = false;
    tk_trig_draw(view, bitmap);
    assert(!read(view, 0x90) && read(view, 0x80) - read(view, 0x7c) == 8);
    assert(pages[0] == 23 && pages[1] == 24 && tk_trig_parameter(view, 0) == 100);
    return 0;
}
