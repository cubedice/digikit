#include "track-native.h"

extern uintptr_t tk_fw_project(void);
extern uintptr_t tk_fw_active_kit(uintptr_t);
extern int32_t tk_fw_track_type(uintptr_t, uint32_t);
extern uint32_t tk_fw_midi_mode(uint32_t);
extern int32_t tk_fw_midi_channel(uint32_t);
extern void tk_fw_note_on(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, int32_t,
                          int32_t, int32_t);
extern void tk_fw_note_off(uint32_t, uint32_t, uint32_t);
extern void tk_fw_midi_dispatch(const int32_t *);
extern void tk_fw_audio_dispatch(const int32_t *);
extern const uint8_t tk_fw_note_mail_lit[];
extern const uint8_t tk_fw_midi_mail_lit[];
extern const uint8_t tk_fw_midi_free_lit[];
extern const uint8_t tk_fw_midi_hold_lit[];

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
    return type >= 0 && type <= 6 && type != 5 ? TK_NATIVE_AUDIO : TK_NATIVE_UNSUPPORTED;
}

int tk_native_channel(uint8_t track)
{
    return tk_fw_midi_channel(track);
}

bool tk_native_try_send(uint8_t track, bool midi, uint8_t pitch, uint8_t velocity)
{
    bool same_kind = (tk_fw_midi_mode(track) != 0) == midi;
    if (!same_kind && velocity)
        return false;
    const uint32_t source = 0x40;
    if (midi) {
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
        const int32_t event[16] = {[0] = track,
                                   [1] = pitch,
                                   [2] = velocity,
                                   [3] = velocity ? 1 : 2,
                                   [4] = velocity ? 0x10081 : 0,
                                   [5] = -1,
                                   [8] = (int32_t)source,
                                   [9] = -1,
                                   [10] = 127,
                                   [14] = -1,
                                   [15] = -1};
        tk_fw_midi_dispatch(event);
        return true;
    }
    if (!same_kind) {
        /* The note is already owned by the old task. A native audition release
         * would select the new task. Submit the old task's release record. The
         * audio worker looks up the Sound retained with the original voice.
         */
        const int32_t release[16] = {
            [0] = track, [1] = pitch, [3] = 2, [5] = -1, [8] = (int32_t)source};
        tk_fw_audio_dispatch(release);
        return true;
    }
    /* Leave half of the native audition ring to the stock input paths. */
    uintptr_t mail = read_pointer(literal(tk_fw_note_mail_lit));
    if (!mail || read_pointer(mail + 4) >= 8)
        return false;
    if (velocity)
        tk_fw_note_on(track, pitch, velocity, source, 0, -1, -1, -1, -1);
    else
        tk_fw_note_off(track, pitch, source);
    return true;
}
