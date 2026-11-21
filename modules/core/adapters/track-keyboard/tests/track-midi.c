#include "../src/track-midi.h"
#include "../src/track-capture.h"
#include "../src/track-service.h"

#include <assert.h>
#include <string.h>

typedef struct {
    uint8_t kind, track, source, channel, note, velocity;
} Seen;

static Seen seen[128];
static uint8_t count;
static uint8_t selected;
static uint16_t enabled, learning;
static uint32_t routes[16];
static int8_t octaves[TK_TRACKS], channels[TK_TRACKS];
static bool masked, receive_notes = true;
static uint8_t receive_ports = 3;
static uint32_t mode, block;
static unsigned busy_notices;

static void record(uint8_t kind, uint8_t track, uint8_t source, uint8_t channel, uint8_t note,
                   uint8_t velocity)
{
    assert(!masked && count < sizeof seen / sizeof *seen);
    seen[count++] = (Seen){kind, track, source, channel, note, velocity};
}

uint32_t dkrt_blocks(void)
{
    return ++block;
}

uint32_t tk_capture_midi_mode(void)
{
    assert(masked);
    return mode;
}

bool tk_capture_input_at(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                         uint8_t velocity, bool step, uint32_t now)
{
    assert(id == pitch && now <= block);
    record(step ? 'B' : 'W', track, source, channel, pitch, velocity);
    return true;
}

void tk_capture_shape_changed(uint8_t track)
{
    assert(!masked && track < TK_TRACKS);
}

uint32_t dkrt_irq_mask(void)
{
    assert(!masked);
    masked = true;
    return 0;
}

void dkrt_irq_restore(uint32_t sr)
{
    assert(masked && sr == 0);
    masked = false;
}

uint8_t tk_midi_selected_track(void)
{
    assert(masked);
    return selected;
}

int tk_midi_notes_enabled(void)
{
    assert(masked);
    return receive_notes;
}

uint8_t tk_midi_receive_ports(void)
{
    assert(masked);
    return receive_ports;
}

uint32_t tk_midi_route(uint8_t channel, int auto_channel)
{
    assert(masked && channel < 16);
    return auto_channel ? (uint32_t)1 << selected : routes[channel];
}

uint16_t tk_runtime_learn_mask(void)
{
    return learning;
}

void tk_runtime_midi_busy(void)
{
    assert(!masked);
    ++busy_notices;
}

bool tk_runtime_learn_note(uint8_t track, uint8_t pitch, uint8_t velocity)
{
    if (!(learning & (1u << track)))
        return false;
    record('L', track, 0, 0, pitch, velocity);
    return true;
}

bool tk_service_enabled(uint8_t track)
{
    return (enabled & (1u << track)) != 0;
}

bool tk_service_selector(uint8_t track, bool active, int8_t *octave, int8_t *channel)
{
    assert(active && !masked);
    *octave = octaves[track];
    *channel = channels[track];
    return true;
}

bool tk_service_press(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                      uint8_t velocity)
{
    assert(id == pitch);
    if (!(enabled & (1u << track)))
        return false;
    record('P', track, source, channel, pitch, velocity);
    return true;
}

void tk_service_release(uint8_t track, uint8_t source, uint8_t channel, uint32_t id)
{
    if (enabled & (1u << track))
        record('R', track, source, channel, (uint8_t)id, 0);
}

void tk_service_pedal(uint8_t track, uint8_t source, uint8_t channel, bool down)
{
    record('D', track, source, channel, 0, (uint8_t)down);
}

void tk_service_clear_source(uint8_t track, uint8_t source, uint8_t channel)
{
    record('C', track, source, channel, 0, 0);
}

void tk_service_select_chord(uint8_t track, enum TkChord chord)
{
    record('S', track, 0, 0, (uint8_t)chord, 0);
}

static int feed(uint32_t source, uint8_t status, uint8_t note, uint8_t value)
{
    const uint8_t message[] = {status, note, value};
    int owned = tk_midi_channel_message(message, sizeof message, source, 1);
    return owned ? owned > 0 : tk_midi_channel_message(message, sizeof message, source, 0);
}

static void expect_event(uint8_t at, uint8_t kind, uint8_t track, uint8_t source, uint8_t channel,
                         uint8_t note, uint8_t velocity)
{
    const Seen expected = {kind, track, source, channel, note, velocity};
    assert(at < count);
    assert(!memcmp(&seen[at], &expected, sizeof expected));
}

int main(void)
{
    for (unsigned i = 0; i < TK_TRACKS; ++i)
        octaves[i] = channels[i] = -1;
    enabled = 3;
    routes[2] = 3;
    routes[15] = 0x10000; /* Native auxiliary target. */
    tk_midi_poll();
    assert(!feed(2, 0x9f, 60, 100));
    assert(!feed(2, 0x92, 4, 100));
    assert(!feed(1, 0x92, 60, 100));
    assert(feed(4, 0x90, 60, 110));
    assert(feed(16, 0x90, 60, 90));
    assert(count == 0);
    tk_midi_poll();
    assert(count == 2);
    expect_event(0, 'P', 0, TK_USB, 0, 60, 110);
    expect_event(1, 'P', 0, TK_DIN, 0, 60, 90);

    assert(feed(4, 0xb0, 64, 127));
    selected = 1;
    assert(feed(4, 0x90, 60, 0));
    assert(feed(16, 0x80, 60, 42));
    assert(feed(4, 0xb0, 64, 0));
    tk_midi_poll();
    assert(count == 6);
    expect_event(2, 'D', 0, TK_USB, 0, 0, 1);
    expect_event(3, 'R', 0, TK_USB, 0, 60, 0);
    expect_event(4, 'R', 0, TK_DIN, 0, 60, 0);
    expect_event(5, 'D', 0, TK_USB, 0, 0, 0);

    assert(feed(2, 0x92, 72, 105));
    assert(feed(2, 0xb2, 123, 0));
    routes[2] = 1;
    assert(feed(2, 0x82, 72, 0));
    tk_midi_poll();
    assert(count == 10);
    expect_event(6, 'P', 0, TK_USB, 2, 72, 105);
    expect_event(7, 'P', 1, TK_USB, 2, 72, 105);
    expect_event(8, 'C', 0, TK_USB, 2, 0, 0);
    expect_event(9, 'C', 1, TK_USB, 2, 0, 0);

    octaves[0] = 3;
    channels[1] = 6;
    tk_midi_poll();
    assert(feed(2, 0x92, 37, 100));
    assert(feed(8, 0x96, 70, 100));
    assert(feed(2, 0x82, 37, 0));
    assert(feed(8, 0x86, 70, 0));
    tk_midi_poll();
    assert(count == 12);
    expect_event(10, 'S', 0, 0, 0, TK_CHORD_MAJ6, 0);
    expect_event(11, 'S', 1, 0, 0, TK_CHORD_USER, 0);

    /* Same-note selector inputs become literal pitches while learning. */
    learning = 1;
    tk_midi_poll();
    assert(feed(2, 0x92, 37, 100));
    tk_midi_poll();
    expect_event(12, 'L', 0, 0, 0, 37, 100);
    expect_event(13, 'P', 0, TK_USB, 2, 37, 100);
    learning = 0;
    tk_midi_cancel_track(0);
    assert(feed(2, 0x82, 37, 0));
    uint8_t canceled = count;
    tk_midi_poll();
    assert(count == canceled);

    /* The adapter keeps the existing native destination in mixed fan-out. */
    enabled = 1;
    routes[2] = 3;
    tk_midi_poll();
    uint8_t before_mixed = count;
    assert(!feed(2, 0x92, 60, 100));
    assert(tk_midi_native_action(0, 2, 60, 8, 1) == TK_MIDI_NATIVE);
    assert(tk_midi_native_action(1, 2, 60, 2, 1) == TK_MIDI_NATIVE);
    assert(tk_midi_native_action(0, 2, 60, 2, 1) == TK_MIDI_SUPPRESS);
    assert(tk_midi_native_action(0, 2, 60, 2, 1) == TK_MIDI_NATIVE);
    assert(!feed(2, 0x82, 60, 0));
    assert(tk_midi_native_action(0, 2, 60, 2, 0) == TK_MIDI_SUPPRESS);
    assert(tk_midi_native_action(1, 2, 60, 2, 0) == TK_MIDI_NATIVE);
    tk_midi_poll();
    expect_event(before_mixed, 'P', 0, TK_USB, 2, 60, 100);
    expect_event(before_mixed + 1, 'R', 0, TK_USB, 2, 60, 0);

    assert(feed(4, 0x90, 60, 105) == 0); /* Current Auto Channel track is disabled. */
    selected = 0;
    receive_notes = false;
    assert(!feed(4, 0x90, 60, 105));
    receive_notes = true;
    assert(feed(4, 0x90, 60, 105));
    tk_midi_reset(); /* Discards this queued onset and owns its later release. */
    assert(feed(4, 0x80, 60, 0));
    canceled = count;
    tk_midi_poll();
    assert(count == canceled);

    /* Share physical note identity with the native writer but keep audition
     * exclusively in the keyboard service. Capture mode is latched at press.
     */
    mode = TK_CAPTURE_STEP;
    assert(!feed(4, 0x90, 60, 105));
    assert(tk_midi_native_action(0, 0, 60, 4, 1) == TK_MIDI_RECORD_ONLY);
    assert(feed(4, 0x90, 60, 105));
    mode = TK_CAPTURE_OFF;
    assert(!feed(4, 0x90, 60, 0));
    assert(tk_midi_native_action(0, 0, 60, 4, 0) == TK_MIDI_NATIVE);
    tk_midi_poll();
    expect_event(canceled, 'B', 0, TK_USB, 0, 60, 105);
    expect_event(canceled + 1, 'P', 0, TK_USB, 0, 60, 105);
    expect_event(canceled + 2, 'W', 0, TK_USB, 0, 60, 0);
    expect_event(canceled + 3, 'R', 0, TK_USB, 0, 60, 0);

    count = 0;
    assert(feed(4, 0x90, 60, 100));
    assert(feed(16, 0x90, 60, 100));
    tk_midi_poll();
    receive_ports = 2;
    tk_midi_poll();
    assert(count == 3);
    expect_event(2, 'C', 0, TK_USB, 0, 0, 0);
    receive_ports = 3;
    tk_midi_poll();
    assert(feed(4, 0x90, 60, 100)); /* A fresh strike replaces its detached gesture. */
    assert(feed(16, 0x80, 60, 0));
    assert(feed(4, 0x80, 60, 0));
    tk_midi_poll();
    expect_event(3, 'P', 0, TK_USB, 0, 60, 100);
    expect_event(4, 'R', 0, TK_DIN, 0, 60, 0);
    expect_event(5, 'R', 0, TK_USB, 0, 60, 0);

    /* Onsets reserve queue room for releases and report contention. */
    count = 0;
    for (unsigned i = 0; i < 64; ++i)
        assert(feed(4, 0x90, (uint8_t)(i + 48), 100));
    tk_midi_poll();
    assert(count == 55 && busy_notices == 1);
    for (unsigned i = 0; i < 64; ++i)
        assert(feed(4, 0x80, (uint8_t)(i + 48), 0));
    tk_midi_poll();
    assert(count == 110 && busy_notices == 1);

    /* Exhausting the reserved release queue triggers bounded cleanup. */
    count = 0;
    for (unsigned i = 0; i < 64; ++i)
        assert(feed(4, 0x90, (uint8_t)(i + 48), 100));
    for (unsigned i = 0; i < 10; ++i)
        assert(feed(4, 0x80, (uint8_t)(i + 48), 0));
    tk_midi_poll();
    assert(count == 32 && busy_notices == 2);
    for (unsigned i = 10; i < 64; ++i)
        assert(feed(4, 0x80, (uint8_t)(i + 48), 0));
    tk_midi_poll();
    assert(count == 32 && busy_notices == 2);
    assert(feed(4, 0x90, 60, 100));
    tk_midi_poll();
    expect_event(32, 'P', 0, TK_USB, 0, 60, 100);
    return 0;
}
