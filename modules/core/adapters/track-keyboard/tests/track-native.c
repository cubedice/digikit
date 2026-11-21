#include "../src/track-native.h"
#include "../src/track-pool.h"

#include <assert.h>

typedef struct {
    uint8_t track, pitch, velocity;
    bool midi;
} Sent;

static Sent sent[16];
static unsigned count;
static bool blocked;
static int kind[2] = {TK_NATIVE_MIDI, TK_NATIVE_MIDI};
static uint16_t helper_mask;

uint8_t tk_pool_fw_voices(uint8_t track)
{
    return (track == 0 && helper_mask) ? 2 : 1;
}

uint16_t tk_pool_fw_helpers(uint8_t track)
{
    return track == 0 ? helper_mask : 0;
}

bool tk_pool_fw_reserved(uint8_t track)
{
    return (helper_mask & (1u << track)) != 0;
}

uint16_t tk_pool_fw_quiet(void)
{
    return UINT16_MAX;
}

void tk_pool_fw_note_edge(uint8_t track)
{
    (void)track;
}

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

const TkSequenceAudio *tk_sequence_audio(uint8_t track)
{
    (void)track;
    return NULL;
}

bool tk_native_try_seed_send(uint8_t track, uint8_t pitch, uint8_t velocity,
                             const TkSequenceAudio *audio)
{
    (void)audio;
    return tk_native_try_send(track, false, pitch, velocity);
}

bool tk_native_try_helper_send(uint8_t owner, uint8_t track, uint8_t pitch, uint8_t velocity,
                               const TkSequenceAudio *audio)
{
    assert(owner == 0 && track == 9);
    return tk_native_try_seed_send(track, pitch, velocity, audio);
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

    TkTrackBackend audio;
    tk_native_backend(false, &audio);
    helper_mask = 1u << 9;
    assert(post(&audio, 0, 3, 60, 111));
    assert(post(&audio, 0, 4, 64, 82));
    assert(!post(&audio, 9, 1, 55, 90)); /* Reserved destinations cannot claim keys. */
    tk_native_drain();
    assert(count == 6 && sent[4].track == 0 && sent[5].track == 9);
    helper_mask = 0;
    tk_native_drain();
    assert(count == 7 && sent[6].track == 9 && sent[6].velocity == 0);
    assert(post(&audio, 0, 4, 64, 0)); /* Stale release must not touch the owner. */
    assert(post(&audio, 0, 3, 60, 0));
    tk_native_drain();
    assert(count == 8 && sent[7].track == 0 && sent[7].velocity == 0);
    assert(tk_native_idle(0));
}
