#include "track-native.h"
#include "track-canopy.h"
#include "track-pool.h"

extern uintptr_t tk_fw_project(void);
extern uintptr_t tk_fw_active_kit(uintptr_t);
extern int32_t tk_fw_track_type(uintptr_t, uint32_t);
extern uint32_t tk_fw_midi_mode(uint32_t);
extern int32_t tk_fw_midi_channel(uint32_t);
extern void tk_fw_midi_dispatch(const int32_t *);
extern void tk_fw_audio_dispatch(const int32_t *);
extern uint32_t pp_current_pattern(void);
extern const uint8_t tk_fw_midi_mail_lit[];
extern const uint8_t tk_fw_midi_free_lit[];
extern const uint8_t tk_fw_midi_hold_lit[];
extern const uint8_t tk_seq_audio_free_lit[];
extern const uint8_t tk_seq_audio_bucket_free_lit[], tk_seq_audio_lock_free_lit[];
extern uint32_t *tk_seq_audio_event_alloc(void);
extern uint32_t *tk_seq_audio_lock_alloc(void);
extern void tk_seq_audio_event_queue(uint32_t *);
extern uintptr_t tk_seq_audio_track_sound(uint32_t);
extern int rtm_is_type(unsigned type) __attribute__((weak));
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

static uintptr_t literal(const uint8_t *operand)
{
    /* ColdFire absolute operands can be only halfword aligned. */
    return (uintptr_t)operand[0] << 24 | (uintptr_t)operand[1] << 16 | (uintptr_t)operand[2] << 8 |
           operand[3];
}

static uintptr_t read_pointer(uintptr_t address)
{
    return *(const volatile uint32_t *)address;
}

int tk_native_kind(uint8_t track)
{
    if (track >= TK_TRACKS)
        return TK_NATIVE_UNSUPPORTED;
    uintptr_t project = tk_fw_project();
    uintptr_t kit = project ? tk_fw_active_kit(project) : 0;
    if (!kit)
        return TK_NATIVE_UNSUPPORTED;
    int type = tk_fw_track_type(kit, track);
    bool midi = tk_fw_midi_mode(track) != 0;
    if (midi)
        return type == 5 ? TK_NATIVE_MIDI : TK_NATIVE_UNSUPPORTED;
    if (tk_canopy_is_owner(track, type))
        return TK_NATIVE_CANOPY;
    if (type >= 7 && rtm_is_type && rtm_is_type((unsigned)type) && tk_pool_fw_relay_ready())
        return TK_NATIVE_RELAY;
    return type >= 0 && type <= 6 && type != 5 ? TK_NATIVE_AUDIO : TK_NATIVE_UNSUPPORTED;
}

bool tk_native_relay_sound(uintptr_t sound)
{
    /* A pool Sound is only halfword-aligned. The type is one byte. */
    return sound >= 0x42000000u && sound < 0x48000000u - 0xa2u && !(sound & 1u) && rtm_is_type &&
           rtm_is_type(*(const uint8_t *)(sound + 0xa2u));
}

int tk_native_channel(uint8_t track)
{
    return tk_fw_midi_channel(track);
}

bool tk_native_try_seed_send(uint8_t track, uint8_t pitch, uint8_t velocity,
                             const TkSequenceAudio *audio)
{
    if (track >= TK_TRACKS || pitch > 127 || (velocity && !audio))
        return false;
    uint32_t sr = dkrt_irq_mask();
    if (!read_pointer(literal(tk_seq_audio_free_lit)) ||
        !read_pointer(literal(tk_seq_audio_bucket_free_lit)) ||
        (velocity && audio->count && !read_pointer(literal(tk_seq_audio_lock_free_lit)))) {
        dkrt_irq_restore(sr);
        return false;
    }
    uint32_t *event = tk_seq_audio_event_alloc();
    uint32_t *lock = velocity && audio->count ? tk_seq_audio_lock_alloc() : NULL;
    /* The scheduler owns this native event and its lock record after enqueue.
     * Source 2 uses the native audition release arbitration so a resequenced
     * note-off cannot end a new source-1 stock pattern note of the same pitch. */
    event[1] = velocity ? 1 : 2;
    event[4] = track;
    event[5] = 2;
    event[7] = 0;
    event[8] = UINT32_MAX;
    event[9] = (uint32_t)pitch << 8 | velocity;
    event[10] = 127u << 24;
    event[0xb] = 0;
    event[0xe] = velocity ? audio->flags : 0;
    event[0xf] = velocity ? (uint32_t)audio->sound : 0;
    event[0x11] = 0;
    event[0x12] = 0;
    event[0x13] = 0;
    event[0x15] = (uint32_t)(uintptr_t)lock;
    event[0x17] = track;
    event[0x19] = UINT32_MAX;
    if (lock) {
        lock[2] = audio->count;
        uint16_t *values = (uint16_t *)(lock + 5);
        for (uint8_t i = 0; i < audio->count; ++i, values += 4) {
            values[0] = audio->locks[i].parameter;
            values[1] = audio->locks[i].value;
        }
    }
    tk_seq_audio_event_queue(event);
    dkrt_irq_restore(sr);
    return true;
}

static uint32_t note_flags(uint8_t track)
{
    uint32_t pattern = pp_current_pattern();
    if (!pattern)
        return 0x10081;
    const uint8_t *trig = (const uint8_t *)(uintptr_t)(pattern + track * 0x6a5u + 0x482u);
    return ((uint32_t)(int16_t)((uint16_t)trig[0] << 8 | trig[1]) | 0x10001u) & ~2u;
}

bool tk_native_try_helper_send(uint8_t owner, uint8_t track, uint8_t pitch, uint8_t velocity,
                               const TkSequenceAudio *audio)
{
    if (owner >= TK_TRACKS)
        return false;
    if (!velocity || audio)
        return tk_native_try_seed_send(track, pitch, velocity, audio);
    const TkSequenceAudio live = {.sound = tk_seq_audio_track_sound(owner),
                                  .flags = note_flags(owner)};
    return live.sound && tk_native_try_seed_send(track, pitch, velocity, &live);
}

extern const uint8_t tk_midi_wire_ports_lit[], tk_midi_wire_message_lit[];
extern void tk_midi_stock_append(uint32_t, const uint8_t *);
extern void tk_midi_stock_flush(uint32_t, const uint8_t *);
extern void tk_midi_wire_send(uint32_t, const uint8_t *, uint32_t);

uint8_t tk_native_midi_ports(void)
{
    return (uint8_t)(read_pointer(literal(tk_midi_wire_ports_lit)) & 3u);
}

void tk_midi_routed_note(uint32_t length, const uint8_t *bytes, const uint32_t *worker)
{
    unsigned route = worker[4] >> 24;
    if ((route & 0xfcu) != 0xa0u) {
        tk_midi_stock_append(length, bytes);
        return;
    }
    /* The preceding buffered message may be a same-pitch release. Its
     * native routing and order must survive this separate note send. */
    uint8_t *pending = (uint8_t *)literal(tk_midi_wire_message_lit);
    if (*pending) {
        tk_midi_stock_flush(*pending, pending + 1);
        *pending = 0;
    }
    static const uint8_t destination[] = {0, 3, 2, 4};
    uint32_t port = destination[route & 3u]; /* Native bits: DIN, USB. */
    if (port)
        tk_midi_wire_send(length, bytes, port);
}

bool tk_native_try_midi_send(uint8_t track, uint8_t pitch, uint8_t velocity, uint8_t ports)
{
    if (velocity && !tk_fw_midi_mode(track))
        return false;
    const uint32_t source = 0x40;
    /* Pending worker messages can still claim held-note entries. Leave
     * room for those messages before enqueueing another onset.
     */
    uint32_t queued = read_pointer(literal(tk_fw_midi_mail_lit) + 4);
    uintptr_t events = read_pointer(literal(tk_fw_midi_free_lit));
    if (queued >= 32 || !events || !read_pointer(events + 0x5c))
        return false;
    if (velocity) {
        uintptr_t holds = read_pointer(literal(tk_fw_midi_hold_lit));
        for (uint32_t pending = 0; holds && pending < queued + 4; ++pending)
            holds = read_pointer(holds + 0x14);
        if (!holds)
            return false;
    }
    /* Word 4 bit 7 requests MIDI output. Native length 127 holds until
     * our release. The worker matches note-offs by original track,
     * including owners on a previous channel.
     */
    const int32_t event[16] = {
        [0] = track,
        [1] = pitch,
        [2] = velocity,
        [3] = velocity ? 1 : 2,
        [4] = (int32_t)(0xa0000000u | ((uint32_t)ports << 24) | (velocity ? 0x10081u : 0)),
        [5] = -1,
        [8] = (int32_t)source,
        [9] = -1,
        [10] = 127,
        [14] = -1,
        [15] = -1};
    tk_fw_midi_dispatch(event);
    return true;
}

bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity)
{
    if (midi || (velocity && tk_fw_midi_mode(track)))
        return false;
    const uint32_t source = 0x40;
    uintptr_t free = read_pointer(literal(tk_seq_audio_free_lit));
    for (uint32_t remaining = velocity ? 4 : 2; free && remaining; --remaining)
        free = read_pointer(free + 0x68);
    if (!free)
        return false;
    /* Dispatch past the note wrapper so generated notes never enter the
     * recorder's channel-input inbox. Keep the audio path for old releases. */
    const int32_t event[16] = {
        [0] = track,
        [1] = pitch,
        [2] = velocity,
        [3] = velocity ? 1 : 2,
        [4] = velocity ? (int32_t)note_flags(track) : 0,
        [5] = -1,
        [8] = (int32_t)source,
        [9] = -1,
        [10] = -1,
        [14] = -1,
        [15] = -1,
    };
    tk_fw_audio_dispatch(event);
    return true;
}
