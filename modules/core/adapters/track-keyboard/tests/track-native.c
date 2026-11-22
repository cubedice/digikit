#include "../src/track-native.h"
#include "../src/track-pool.h"

#include <assert.h>

typedef struct {
    uint8_t track, pitch, velocity;
    bool midi;
    uint8_t ports;
} Sent;

static Sent sent[64];
static unsigned count;
static bool blocked;
static uint32_t blocks, busy;
static uint8_t output_ports = 2;
static int kind[2] = {TK_NATIVE_MIDI, TK_NATIVE_MIDI};
static uint16_t helper_mask;
static uint16_t quiet_mask = UINT16_MAX;
static uint16_t route_ready = UINT16_MAX;
static bool automatic;
static bool relay_ready;

bool tk_pool_fw_relay_ready(void)
{
    return relay_ready;
}

uint8_t tk_pool_fw_limit(uint8_t track)
{
    return (track == 0 && (helper_mask || automatic)) ? 2 : 1;
}

bool tk_pool_fw_automatic(uint8_t track)
{
    return track == 0 && automatic;
}

bool tk_pool_fw_start_ready(uint8_t owner, uint8_t track)
{
    assert(owner == 0);
    return (route_ready & (1u << track)) != 0;
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
    return quiet_mask;
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

uint32_t dkrt_blocks(void)
{
    return blocks;
}

uint8_t tk_native_midi_ports(void)
{
    return output_ports;
}

void tk_runtime_midi_busy(void)
{
    ++busy;
}

bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity)
{
    if (blocked)
        return false;
    assert(count < sizeof sent / sizeof sent[0]);
    sent[count++] = (Sent){track, pitch, velocity, midi, 0};
    return true;
}

bool tk_native_try_midi_send(uint8_t track, uint8_t pitch, uint8_t velocity, uint8_t ports)
{
    if (!tk_native_try_send(track, true, pitch, velocity))
        return false;
    sent[count - 1].ports = ports;
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

static void midi_wire_budget(const TkTrackBackend *backend)
{
    kind[0] = TK_NATIVE_MIDI;
    blocks += 1500;
    uint32_t prior_busy = busy;
    unsigned first = count;
    output_ports = 1;
    for (uint32_t note = 1; note <= 8; ++note) {
        assert(post(backend, 0, note, 60, 100));
        tk_native_drain();
        if (note == 8)
            output_ports = 2;
        assert(post(backend, 0, note, 60, 0));
        tk_native_drain();
    }
    assert(count == first + 16 && sent[count - 1].ports == 1 && !sent[count - 1].velocity);
    output_ports = 3;
    assert(post(backend, 0, 9, 60, 100));
    assert(!tk_native_sounding(0, 9));
    tk_native_drain();
    assert(count == first + 16 && busy == prior_busy + 1 && tk_native_idle(0));
    assert(!tk_native_sounding(0, 9));
    output_ports = 2;
    assert(post(backend, 0, 10, 60, 100));
    tk_native_drain();
    assert(count == first + 17 && sent[count - 1].ports == 2);
    assert(tk_native_sounding(0, 10));
    assert(post(backend, 0, 10, 60, 0));
    tk_native_drain();
    assert(!tk_native_sounding(0, 10));
    output_ports = 3;
    blocks += 4;
    assert(post(backend, 0, 11, 60, 100));
    tk_native_drain();
    assert(count == first + 19 && sent[count - 1].ports == 3);
    assert(post(backend, 0, 11, 60, 0));
    tk_native_drain();
    assert(count == first + 20 && sent[count - 1].ports == 3 && tk_native_idle(0));
}

static void relay_checks(void)
{
    TkTrackBackend relay;
    tk_native_relay_backend(&relay);
    assert(relay.voices == 1);
    kind[0] = TK_NATIVE_RELAY;
    helper_mask = 1u << 9; /* A stale request from a former sample machine. */
    assert(post(&relay, 0, 1, 60, 100));
    assert(!post(&relay, 0, 2, 64, 100));
    tk_native_drain();
    assert(count == 1 && sent[0].track == 0 && sent[0].pitch == 60 && !sent[0].midi);
    assert(post(&relay, 0, 1, 60, 0));
    assert(post(&relay, 0, 3, 64, 90));
    tk_native_drain();
    assert(count == 3 && sent[1].track == 0 && sent[1].velocity == 0);
    assert(sent[2].track == 0 && sent[2].pitch == 64 && sent[2].velocity == 90);
    assert(post(&relay, 0, 1, 60, 0)); /* A stale release cannot stop the new phrase. */
    tk_native_drain();
    assert(count == 3);
    assert(post(&relay, 0, 3, 64, 0));
    assert(post(&relay, 0, 4, 67, 80));
    kind[0] = TK_NATIVE_AUDIO;
    tk_native_drain();
    assert(count == 4 && sent[3].pitch == 64 && sent[3].velocity == 0);
    assert(tk_native_idle(0)); /* The queued RELAY onset cannot enter the new machine. */
    count = 0;
    kind[0] = TK_NATIVE_RELAY;
    relay_ready = true;
    assert(post(&relay, 0, 5, 60, 100));
    assert(post(&relay, 0, 6, 72, 100));
    assert(!post(&relay, 0, 7, 76, 100));
    tk_native_drain();
    assert(count == 2 && sent[0].track == 0 && sent[1].track == 9);
    helper_mask = 0;
    tk_native_drain();
    assert(count == 3 && sent[2].track == 9 && sent[2].velocity == 0);
    assert(post(&relay, 0, 6, 72, 0));
    assert(post(&relay, 0, 5, 60, 0));
    tk_native_drain();
    assert(count == 4 && sent[3].track == 0 && sent[3].velocity == 0);
    relay_ready = false;
    kind[0] = TK_NATIVE_MIDI;
    count = 0;
}

static void automatic_checks(const TkTrackBackend *midi, const TkTrackBackend *audio)
{
    unsigned first = count;
    kind[0] = TK_NATIVE_AUDIO;
    automatic = true;
    output_ports = TK_MIDI_OUT_USB;
    blocks += 1500;
    assert(post(audio, 0, 20, 60, 100));
    assert(post(audio, 0, 21, 64, 100));
    assert(!post(audio, 0, 22, 67, 100));
    assert(tk_native_audio_waiting() == 1 && tk_native_audio_pending(0) == 2);
    assert(post(midi, 1, 20, 72, 100));
    tk_native_drain();
    assert(count == first + 2 && sent[first].track == 0 && sent[first + 1].track == 1);
    assert(tk_native_audio_waiting() == 1 && tk_native_audio_pending(0) == 1);
    assert(tk_native_sounding(0, 20) && !tk_native_sounding(0, 21));

    /* The helper is reserved before its routing reaches the DSP. */
    helper_mask = 1u << 9;
    route_ready = 1;
    tk_native_drain();
    assert(count == first + 2 && tk_native_audio_pending(0) == 1);
    route_ready |= helper_mask;
    tk_native_drain();
    assert(count == first + 3 && sent[first + 2].track == 9);
    assert(tk_native_audio_waiting() == 0 && tk_native_audio_pending(0) == 0);
    assert(tk_native_sounding(0, 21));

    assert(post(audio, 0, 21, 64, 0));
    assert(post(midi, 1, 20, 72, 0));
    tk_native_drain();
    assert(count == first + 5 && sent[first + 3].track == 9 && !sent[first + 3].velocity);
    assert(sent[first + 4].track == 1 && !sent[first + 4].velocity);
    helper_mask = 0;
    assert(post(audio, 0, 22, 67, 80));
    assert(post(audio, 0, 22, 67, 0)); /* Key-up cancels an unplayed helper. */
    assert(!tk_native_audio_waiting());
    tk_native_drain();
    assert(count == first + 5);

    assert(post(audio, 0, 23, 67, 90));
    blocks += 119;
    tk_native_drain();
    assert(tk_native_audio_pending(0) == 1 && !tk_native_sounding(0, 23));
    ++blocks;
    helper_mask = 1u << 9;
    tk_native_drain();
    assert(!tk_native_audio_waiting() && count == first + 5);
    helper_mask = 0;
    assert(post(audio, 0, 20, 60, 0));
    tk_native_drain();
    assert(tk_native_idle(0) && tk_native_idle(1));

    /* A failed pool map delays a stock chord only until its first pitch
     * can fall back. Even a ready helper must not start an expired pitch.
     */
    unsigned fallback = count;
    route_ready = 0;
    helper_mask = 1u << 9;
    assert(post(audio, 0, 26, 60, 100));
    assert(post(audio, 0, 27, 64, 90));
    blocks += 119;
    tk_native_drain();
    assert(count == fallback && tk_native_audio_pending(0) == 2);
    ++blocks;
    route_ready = helper_mask;
    tk_native_drain();
    assert(count == fallback + 1 && sent[fallback].track == 0 && sent[fallback].pitch == 60);
    assert(tk_native_sounding(0, 26) && !tk_native_sounding(0, 27));
    assert(!tk_native_audio_waiting());
    assert(post(audio, 0, 27, 64, 0));
    assert(post(audio, 0, 26, 60, 0));
    tk_native_drain();
    assert(count == fallback + 2 && sent[fallback + 1].track == 0 &&
           sent[fallback + 1].pitch == 60 && !sent[fallback + 1].velocity);
    assert(tk_native_idle(0));
    helper_mask = 0;

    /* A standalone RELAY head also waits for the new mode to reach DSP. */
    TkTrackBackend relay;
    tk_native_relay_backend(&relay);
    kind[0] = TK_NATIVE_RELAY;
    relay_ready = true;
    route_ready = 0;
    assert(post(&relay, 0, 25, 67, 90));
    tk_native_drain();
    assert(tk_native_audio_pending(0) == 1 && !tk_native_sounding(0, 25));
    route_ready = 1;
    tk_native_drain();
    assert(tk_native_sounding(0, 25) && !tk_native_audio_waiting());
    assert(post(&relay, 0, 25, 67, 0));
    tk_native_drain();
    assert(tk_native_idle(0));

    route_ready = 0;
    unsigned before = count;
    assert(post(&relay, 0, 28, 67, 90));
    tk_native_drain();
    blocks += 120;
    tk_native_drain();
    assert(count == before && !tk_native_audio_waiting());
    assert(tk_native_idle(0));
}

void track_native_checks(void)
{
    relay_checks();
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
    quiet_mask = helper_mask; /* A new phrase still installs its Sound on T1. */
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
    midi_wire_budget(&midi);
    automatic_checks(&midi, &audio);
}
