#include "track-native.h"
#include "track-memory.h"
#include "track-pool.h"

extern void tk_runtime_midi_busy(void) __attribute__((weak));

enum NoteState { NOTE_FREE, NOTE_PENDING, NOTE_SOUNDING, NOTE_RELEASE };

typedef struct {
    uint32_t token, order;
    uint8_t pitch, velocity, state, channel, destination;
    bool midi, native_seed;
} NativeNote;

/* Include one extra slot for a new note submitted before the preceding
 * note-off is drained. Offs update an existing slot and need no queue space.
 */
typedef NativeNote NativeTrack[TK_NOTES + 1];
#ifdef __m68k__
extern void *tk_fw_alloc(uint32_t size);
static NativeTrack *notes;

bool tk_native_init(void)
{
    if (notes)
        return true;
    NativeTrack *memory = tk_fw_alloc(sizeof(*memory) * TK_TRACKS);
    if (!memory)
        return false;
    memset(memory, 0, sizeof(*memory) * TK_TRACKS);
    notes = memory;
    return true;
}
#else
static NativeTrack notes[TK_TRACKS];
#endif
static uint32_t serial;
static unsigned starts, releases;
static uint16_t active_helpers;
/* Generated audio bypasses the stock audition counter. Report dispatch
 * ownership independently so it can be compared with the stock route.
 */
volatile uint32_t tk_native_audio_active;
static const bool audio_binding = false;
static const bool midi_binding = true;

bool tk_native_event(void *context, uint8_t track, const TkNoteEvent *event)
{
    if (!context || track >= TK_TRACKS || !event->token || event->pitch > 127)
        return false;
    bool midi = *(const bool *)context;
    NativeNote *empty = NULL;
    unsigned active = 0;
    for (unsigned i = 0; i <= TK_NOTES; ++i) {
        NativeNote *entry = &notes[track][i];
        if (entry->state == NOTE_FREE) {
            empty = entry;
            continue;
        }
        if (entry->token == event->token) {
            if (event->on)
                return false;
            if (entry->state == NOTE_PENDING) {
                *entry = (NativeNote){0};
                --starts;
            } else if (entry->state == NOTE_SOUNDING) {
                entry->state = NOTE_RELEASE;
                ++releases;
            }
            return true;
        }
        if (entry->state != NOTE_RELEASE)
            ++active;
    }
    if (!event->on)
        return true;
    if (tk_pool_fw_reserved(track) || !empty ||
        active >= (midi ? TK_NOTES : tk_pool_fw_voices(track)) || !event->velocity ||
        event->velocity > 127)
        return false;
    *empty = (NativeNote){.token = event->token,
                          .order = ++serial,
                          .pitch = event->pitch,
                          .velocity = event->velocity,
                          .state = NOTE_PENDING,
                          .destination = track,
                          .midi = midi,
                          .native_seed =
                              !midi && (event->token & 0x80000000u) && tk_sequence_audio(track)};
    ++starts;
    return true;
}

void tk_native_backend(bool midi, TkTrackBackend *out)
{
    *out = (TkTrackBackend){
        .output = tk_native_event,
        .context = (void *)(midi ? &midi_binding : &audio_binding),
        .voices = midi ? TK_NOTES : 1,
    };
}

bool tk_native_idle(uint8_t track)
{
    if (track >= TK_TRACKS)
        return false;
    for (unsigned i = 0; i <= TK_NOTES; ++i)
        if (notes[track][i].state != NOTE_FREE)
            return false;
    return true;
}

static NativeNote *oldest(uint8_t state, uint8_t *owner)
{
    NativeNote *first = NULL;
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        for (unsigned i = 0; i <= TK_NOTES; ++i) {
            NativeNote *entry = &notes[track][i];
            if (entry->state != state)
                continue;
            if (!first || serial - entry->order > serial - first->order) {
                first = entry;
                *owner = track;
            }
        }
    return first;
}

static bool midi_collision(const NativeNote *entry, uint8_t channel)
{
    /* A native shared channel/pitch has one track owner. Leave the first
     * generated owner sounding until its own release.
     */
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        for (unsigned i = 0; i <= TK_NOTES; ++i) {
            const NativeNote *other = &notes[track][i];
            if (other->state == NOTE_SOUNDING && other->midi && other->channel == channel &&
                other->pitch == entry->pitch)
                return true;
        }
    return false;
}

static bool send(uint8_t track, const NativeNote *entry, uint8_t velocity)
{
    if (entry->destination != track)
        return tk_native_try_helper_send(track, entry->destination, entry->pitch, velocity,
                                         entry->native_seed ? tk_sequence_audio(track) : NULL);
    if (entry->native_seed)
        return tk_native_try_seed_send(track, entry->pitch, velocity, tk_sequence_audio(track));
    bool sent = tk_native_try_send(track, entry->midi, entry->pitch, velocity);
    if (!sent && entry->midi && tk_runtime_midi_busy)
        tk_runtime_midi_busy();
    return sent;
}

static void retire_unassigned(void)
{
    if (!active_helpers)
        return;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        uint16_t assigned = tk_pool_fw_helpers(track);
        for (unsigned i = 0; i <= TK_NOTES; ++i) {
            NativeNote *note = &notes[track][i];
            if (note->state == NOTE_SOUNDING && note->destination != track &&
                !(assigned & (1u << note->destination))) {
                note->state = NOTE_RELEASE;
                ++releases;
            }
        }
    }
}

static bool choose_audio_destination(uint8_t owner, NativeNote *note)
{
    if (tk_pool_fw_reserved(owner))
        return false;
    uint16_t group = (uint16_t)((1u << owner) | tk_pool_fw_helpers(owner));
    uint16_t active = group & tk_native_audio_active;
    if (!active) {
        /* The first note installs the resolved Sound and locks on the owner
         * before subsequent notes inherit that track's live controls. */
        note->destination = owner;
        return true;
    }
    uint16_t available = group & ~active;
    if (!available)
        return false;
    uint16_t quiet = available & tk_pool_fw_quiet();
    uint16_t preferred = quiet ? quiet : available;
    if (preferred & (1u << owner)) {
        note->destination = owner;
        return true;
    }
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (preferred & (1u << track)) {
            note->destination = track;
            return true;
        }
    return false;
}

void tk_native_drain(void)
{
    retire_unassigned();
    /* The stock audition mailbox owns 16 backing records. Dispatch a bounded
     * batch and yield the task to its consumers. Releases win over new notes.
     */
    for (unsigned n = 0; n < 4 && (starts || releases); ++n) {
        uint8_t track = 0;
        NativeNote *entry = releases ? oldest(NOTE_RELEASE, &track) : NULL;
        if (entry) {
            if (!send(track, entry, 0))
                return;
            if (!entry->midi) {
                tk_native_audio_active &= ~(1u << entry->destination);
                active_helpers &= (uint16_t)~(1u << entry->destination);
                tk_pool_fw_note_edge(entry->destination);
            }
            *entry = (NativeNote){0};
            --releases;
            continue;
        }
        entry = oldest(NOTE_PENDING, &track);
        if (!entry)
            return;
        int kind = tk_native_kind(track);
        if (kind != (entry->midi ? TK_NATIVE_MIDI : TK_NATIVE_AUDIO) ||
            (!entry->midi && !choose_audio_destination(track, entry))) {
            *entry = (NativeNote){0};
            --starts;
            continue;
        }
        if (entry->midi) {
            int channel = tk_native_channel(track);
            if (channel < 0 || channel > 15 || midi_collision(entry, (uint8_t)channel)) {
                *entry = (NativeNote){0};
                --starts;
                continue;
            }
            entry->channel = (uint8_t)channel;
        }
        if (!send(track, entry, entry->velocity))
            return;
        if (!entry->midi) {
            tk_native_audio_active |= 1u << entry->destination;
            if (entry->destination != track)
                active_helpers |= (uint16_t)(1u << entry->destination);
            tk_pool_fw_note_edge(entry->destination);
        }
        entry->state = NOTE_SOUNDING;
        --starts;
    }
}
