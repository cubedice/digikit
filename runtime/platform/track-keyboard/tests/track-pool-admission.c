#include "../src/track-pool-admission.c"

#include <assert.h>

enum { OWNER = 0, HELPER_1 = 8, HELPER_2 = 9, HELPER_MASK = (1 << HELPER_1) | (1 << HELPER_2) };
static const uint32_t pattern_address = 0x42000000;
static const TkSequenceAudio sound = {.sound = 1};
static uint32_t block;
static uint16_t quiet = UINT16_MAX;
static uint8_t focused = OWNER;

uintptr_t tk_fw_project(void)
{
    return 1;
}

uintptr_t tk_fw_active_kit(uintptr_t project)
{
    return project;
}

int32_t tk_fw_track_type(uintptr_t kit, uint32_t track)
{
    (void)kit;
    (void)track;
    return TK_NATIVE_AUDIO;
}

uint32_t tk_fw_midi_mode(uint32_t track)
{
    (void)track;
    return 0;
}

uintptr_t tk_pool_fw_audio_queue(void)
{
    return 0;
}

uint32_t dkrt_irq_mask(void)
{
    return 0;
}

void dkrt_irq_restore(uint32_t sr)
{
    assert(sr == 0);
}

uint32_t pp_current_pattern(void)
{
    return pattern_address;
}

int32_t dkrt_module(uint32_t index, DkrtModule *out)
{
    (void)index;
    (void)out;
    return -1;
}

uint32_t tk_pattern_fw_revision(void)
{
    return 0;
}

uint32_t tk_capture_midi_mode(void)
{
    return TK_CAPTURE_OFF;
}

bool tk_canopy_is_owner(uint8_t track, int type)
{
    (void)track;
    (void)type;
    return false;
}

uint8_t tk_midi_selected_track(void)
{
    return focused;
}

void tk_sequence_native_targets(TkSequenceNativeTarget targets[TK_SEQUENCE_TARGETS])
{
    targets[0].address = pattern_address;
}

int tk_sequence_target_kind(uint32_t kit, uint8_t track)
{
    return tk_fw_track_type(kit, track);
}

const TkSequenceAudio *tk_sequence_audio(uint8_t track)
{
    return track == OWNER ? &sound : NULL;
}

uint32_t dkrt_blocks(void)
{
    return block;
}

int tk_native_kind(uint8_t track)
{
    return tk_fw_track_type(1, track);
}

int tk_native_channel(uint8_t track)
{
    (void)track;
    return -1;
}

uint8_t tk_native_midi_ports(void)
{
    return 0;
}

bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity)
{
    (void)track;
    (void)midi;
    (void)pitch;
    (void)velocity;
    assert(false);
    return false;
}

bool tk_native_try_midi_send(uint8_t track, uint8_t pitch, uint8_t velocity, uint8_t ports)
{
    (void)ports;
    return tk_native_try_send(track, true, pitch, velocity);
}

bool tk_native_try_seed_send(uint8_t track, uint8_t pitch, uint8_t velocity,
                             const TkSequenceAudio *audio)
{
    (void)pitch;
    (void)velocity;
    assert(track == OWNER || track == HELPER_1 || track == HELPER_2);
    assert(audio == &sound);
    return true;
}

bool tk_native_try_helper_send(uint8_t owner, uint8_t track, uint8_t pitch, uint8_t velocity,
                               const TkSequenceAudio *audio)
{
    assert(owner == OWNER && track != owner);
    return tk_native_try_seed_send(track, pitch, velocity, audio);
}

static void poll(const TkTrackConfig config[TK_TRACKS])
{
    uint16_t packet[4 * TK_POOL_LINK_WORDS] = {0};
    tk_pool_link_write((uintptr_t)packet, 0);
    uint8_t tag = (uint8_t)(packet[2 * TK_POOL_CHORD_ENTRIES + 3] ^ 0x544bu);
    uint16_t sequence = (uint16_t)++block;
    uint16_t reply[0x60 / 2 + 4] = {0};
    reply[0x60 / 2] = (uint16_t)(0x4b51u ^ tag);
    reply[0x60 / 2 + 1] = quiet;
    reply[0x60 / 2 + 2] = sequence;
    reply[0x60 / 2 + 3] = (uint16_t)(sequence ^ quiet ^ 0x32f7u ^ tag);
    tk_pool_link_reply(reply);
    tk_pool_fw_update(pattern_address, pattern_address, config, false);
    tk_native_drain();
}

static void chord(const TkTrackBackend *audio, uint32_t token, uint8_t root, bool on)
{
    const uint8_t intervals[] = {0, 4, 7};
    for (unsigned i = 0; i < sizeof intervals; ++i) {
        TkNoteEvent event = {.token = token + i,
                             .pitch = (uint8_t)(root + intervals[i]),
                             .velocity = on ? 100 : 0,
                             .lane = TK_DIRECT,
                             .on = on};
        assert(audio->output(audio->context, OWNER, &event));
    }
}

int main(void)
{
    /* Supply the results of an already cached native pattern scan. Only
     * the two stock helper tracks are empty in this pattern. */
    last_pattern = scan_patterns[0] = scan_patterns[1] = pattern_address;
    last_kit = tk_fw_project();
    generation = 1;
    empty_tracks = HELPER_MASK;
    TkTrackConfig config[TK_TRACKS] = {0};
    config[OWNER].params.poly = 1;
    TkTrackBackend audio;
    tk_native_backend(false, &audio);
    poll(config);

    const uint32_t first = UINT32_C(0x80000001), next = UINT32_C(0x80000004);
    const uint32_t after_release = UINT32_C(0x80000007), while_focused = UINT32_C(0x8000000a);
    chord(&audio, first, 60, true);
    for (unsigned i = 0; i < 32; ++i)
        poll(config);
    assert(tk_native_audio_active == (1u << OWNER | HELPER_MASK));

    /* The next trig releases three notes and starts its root within the
     * dispatch batch. Its other attacks encounter the previous DSP tails. */
    quiet &= (uint16_t)~HELPER_MASK;
    chord(&audio, first, 60, false);
    chord(&audio, next, 62, true);
    poll(config);
    poll(config);
    assert(tk_native_audio_active == (1u << OWNER | HELPER_MASK));
    assert(tk_pool_fw_helpers(OWNER) == HELPER_MASK);
    for (unsigned i = 0; i < 3; ++i)
        assert(tk_native_sounding(OWNER, next + i));

    chord(&audio, next, 62, false);
    poll(config);
    poll(config);
    assert(tk_native_idle(OWNER) && !tk_native_audio_active);
    assert(!tk_pool_fw_helpers(OWNER) && pool.draining[OWNER] == HELPER_MASK);
    assert(!(tk_pool_fw_quiet() & HELPER_MASK));

    /* From a real draining state, a new chord reclaims its own sounding
     * tails without waiting for the DSP to report the helpers quiet. */
    chord(&audio, after_release, 64, true);
    poll(config);
    poll(config);
    assert(tk_native_audio_active == (1u << OWNER | HELPER_MASK));
    assert(tk_pool_fw_helpers(OWNER) == HELPER_MASK && !pool.draining[OWNER]);
    for (unsigned i = 0; i < 3; ++i)
        assert(tk_native_sounding(OWNER, after_release + i));

    chord(&audio, after_release, 64, false);
    poll(config);
    poll(config);
    assert(!tk_pool_fw_helpers(OWNER) && pool.draining[OWNER] == HELPER_MASK);
    focused = HELPER_1;
    chord(&audio, while_focused, 65, true);
    poll(config);
    poll(config);
    assert(tk_pool_fw_helpers(OWNER) == (1u << HELPER_2));
    assert(tk_native_audio_active == (1u << OWNER | 1u << HELPER_2));
    assert(pool.draining[OWNER] == (1u << HELPER_1));
    assert(tk_pool_fw_reserved(HELPER_1) && tk_pool_fw_reserved(HELPER_2));
    chord(&audio, while_focused, 65, false);
    poll(config);
    poll(config);
    quiet = UINT16_MAX;
    for (unsigned i = 0; i < 32; ++i)
        poll(config);
    assert(!tk_pool_fw_reserved(HELPER_1) && !tk_pool_fw_reserved(HELPER_2));
    assert(tk_native_idle(OWNER));
    return 0;
}
