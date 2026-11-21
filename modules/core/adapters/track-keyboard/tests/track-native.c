#include "../src/track-native.h"

#include <assert.h>

typedef struct {
    uint8_t track, pitch, velocity;
    bool midi;
} Sent;

static Sent sent[8];
static unsigned count;
static bool blocked;
static int kind[2] = {TK_NATIVE_MIDI, TK_NATIVE_MIDI};

int tk_native_kind(uint8_t track)
{
    return kind[track];
}

int tk_native_channel(uint8_t track)
{
    (void)track;
    return 3;
}

bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity)
{
    if (blocked)
        return false;
    assert(count < sizeof sent / sizeof sent[0]);
    sent[count++] = (Sent){track, pitch, velocity, midi};
    return true;
}

static bool post(const TkTrackBackend *backend, uint8_t track, uint32_t token, uint8_t pitch,
                 uint8_t velocity)
{
    const TkNoteEvent note = {
        .token = token, .pitch = pitch, .velocity = velocity, .on = velocity != 0};
    return backend->output(backend->context, track, &note);
}

void track_native_checks(void)
{
    TkTrackBackend midi;
    tk_native_backend(true, &midi);
    assert(post(&midi, 0, 1, 60, 100));
    tk_native_drain();
    assert(count == 1 && sent[0].velocity == 100 && sent[0].track == 0);

    /* The worker has one owner per channel/pitch. Another track on the
     * channel cannot replace a held generated voice or release it. */
    assert(post(&midi, 1, 1, 60, 99));
    tk_native_drain();
    assert(count == 1 && tk_native_idle(1));
    assert(post(&midi, 1, 1, 60, 0));
    assert(post(&midi, 1, 2, 67, 98));
    assert(post(&midi, 1, 2, 67, 0)); /* Fast release cancels an unsent start. */

    blocked = true;
    assert(post(&midi, 0, 1, 60, 0));
    assert(post(&midi, 0, 2, 60, 90));
    tk_native_drain();
    assert(count == 1 && !tk_native_idle(0));
    blocked = false;
    tk_native_drain();
    assert(count == 3 && sent[1].pitch == 60 && sent[1].velocity == 0);
    assert(sent[2].pitch == 60 && sent[2].velocity == 90);

    assert(post(&midi, 0, 2, 60, 0));
    kind[0] = TK_NATIVE_AUDIO;
    tk_native_drain();
    assert(count == 4 && sent[3].midi && sent[3].velocity == 0);
    assert(tk_native_idle(0));
}
