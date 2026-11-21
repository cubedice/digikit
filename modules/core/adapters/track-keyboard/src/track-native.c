#include "track-native.h"

enum NoteState { NOTE_FREE, NOTE_PENDING, NOTE_SOUNDING, NOTE_RELEASE };

typedef struct {
    uint32_t token, order;
    uint8_t pitch, velocity, state, channel;
    bool midi;
} NativeNote;

/* Include one extra slot for a new note submitted before the preceding
 * note-off is drained. Offs update an existing slot and need no queue space.
 */
static NativeNote notes[TK_TRACKS][TK_NOTES + 1];
static uint32_t serial;
static unsigned starts, releases;
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
    if (!empty || active >= (midi ? TK_NOTES : 1u) || !event->velocity || event->velocity > 127)
        return false;
    *empty = (NativeNote){.token = event->token,
                          .order = ++serial,
                          .pitch = event->pitch,
                          .velocity = event->velocity,
                          .state = NOTE_PENDING,
                          .midi = midi};
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

void tk_native_drain(void)
{
    /* The stock audition mailbox owns 16 backing records. Dispatch a bounded
     * batch and yield the task to its consumers. Releases win over new notes.
     */
    for (unsigned n = 0; n < 4 && (starts || releases); ++n) {
        uint8_t track = 0;
        NativeNote *entry = releases ? oldest(NOTE_RELEASE, &track) : NULL;
        if (entry) {
            if (!tk_native_try_send(track, entry->midi, entry->pitch, 0))
                return;
            *entry = (NativeNote){0};
            --releases;
            continue;
        }
        entry = oldest(NOTE_PENDING, &track);
        if (!entry)
            return;
        int kind = tk_native_kind(track);
        if (kind != (entry->midi ? TK_NATIVE_MIDI : TK_NATIVE_AUDIO)) {
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
        if (!tk_native_try_send(track, entry->midi, entry->pitch, entry->velocity))
            return;
        entry->state = NOTE_SOUNDING;
        --starts;
    }
}
