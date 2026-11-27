#include "track-native.h"
#include "track-memory.h"
#include "track-pool.h"

extern void tk_runtime_midi_busy(void) __attribute__((weak));

/* NOTE_HELD: an owner's note whose key is up while hidden voices of its track still sound; its stock note-off waits for them. */
enum NoteState { NOTE_FREE, NOTE_PENDING, NOTE_SOUNDING, NOTE_RELEASE, NOTE_HELD };
enum { UNITS_PER_BYTE = 1500, AUTO_WAIT_BLOCKS = 120 };

typedef struct {
    uint32_t token, order;
    uint32_t shape; /* the chord the destination's voice plays as copies (tk_pool_link_chord) */
    uint8_t pitch, velocity, state, channel, destination, ports, kind;
    uint8_t hidden; /* 1 .. TK_COPIES: the note plays on that hidden voice of its destination */
    bool midi, native_seed, seed_direct;
    uint16_t queued_at;
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
static uint16_t audio_waiting;
static uint8_t audio_pending[TK_TRACKS];
/* Hidden voices (finding 375): how many a track has, which are taken (bit h), their note serials, and whether the owner's own note was
 * faded (its gate word closed) for them. */
static uint8_t hidden_capacity[TK_TRACKS], hidden_used[TK_TRACKS];
static uint8_t hidden_serial[TK_TRACKS][TK_COPIES + 1];
static bool owner_faded[TK_TRACKS];
/* The hidden voice each track took last: the next one starts after it, so a key reuses the voice released longest ago and seldom cuts a
 * release still fading in the DSP. */
static uint8_t hidden_last[TK_TRACKS];
/* Generated audio bypasses the stock audition counter. Report dispatch
 * ownership independently so it can be compared with the stock route.
 */
volatile uint32_t tk_native_audio_active;
static const uint8_t audio_binding = TK_NATIVE_AUDIO;
static const uint8_t midi_binding = TK_NATIVE_MIDI;
static const uint8_t relay_binding = TK_NATIVE_RELAY;
extern uint32_t dkrt_blocks(void);

/* Pay for a release at onset. USB uses four framing bytes per message.
 * The DIN limit leaves 425 bytes per second for stock clock and controllers. */
static uint32_t budget_at;
static uint32_t usb_budget = 256u * UNITS_PER_BYTE;
static uint32_t din_budget = 48u * UNITS_PER_BYTE;

static void replenish_midi(void)
{
    uint32_t now = dkrt_blocks();
    uint32_t elapsed = now - budget_at;
    budget_at = now;
    if (elapsed > 1500)
        elapsed = 1500;
    usb_budget += elapsed * TK_MIDI_USB_BYTES_PER_S;
    din_budget += elapsed * TK_MIDI_DIN_BYTES_PER_S;
    if (usb_budget > 256u * UNITS_PER_BYTE)
        usb_budget = 256u * UNITS_PER_BYTE;
    if (din_budget > 48u * UNITS_PER_BYTE)
        din_budget = 48u * UNITS_PER_BYTE;
}

static bool midi_room(uint8_t ports)
{
    return (!(ports & TK_MIDI_OUT_USB) || usb_budget >= TK_MIDI_USB_PAIR_BYTES * UNITS_PER_BYTE) &&
           (!(ports & TK_MIDI_OUT_DIN) || din_budget >= TK_MIDI_DIN_PAIR_BYTES * UNITS_PER_BYTE);
}

uint16_t tk_native_audio_waiting(void)
{
    return audio_waiting;
}

uint8_t tk_native_audio_pending(uint8_t owner)
{
    return owner < TK_TRACKS ? audio_pending[owner] : 0;
}

/* The 20 low bits of a shape word: the copies in bits 18-19, the intervals (6 bits, signed) in 12-17, 6-11 and 0-5. */
static uint32_t shape_of(const TkNoteEvent *event)
{
    uint8_t copies = event->copies <= TK_COPIES ? event->copies : TK_COPIES;
    uint32_t shape = (uint32_t)copies << 18;
    for (uint8_t i = 0; i < copies; ++i)
        shape |= ((uint32_t)event->intervals[i] & 63u) << (12u - 6u * i);
    return shape;
}

void tk_native_set_hidden(uint8_t track, uint8_t voices)
{
    if (track < TK_TRACKS)
        hidden_capacity[track] = voices <= TK_COPIES ? voices : TK_COPIES;
}

/* The owner's held note (NOTE_HELD) still counts against the track's limit here (tk_native_event), while the engine has released its key:
 * without the slot taken off, the engine would send a key the track declines, and the key would show as held and never sound. */
uint8_t tk_native_voices(uint8_t track, uint8_t limit)
{
    return track < TK_TRACKS && owner_faded[track] && limit > 1 ? (uint8_t)(limit - 1u) : limit;
}

/* The 18 low bits of a hidden-voice word: the gate in bit 17, the serial in 14-16, the pitch in 7-13, the velocity in 0-6. */
static uint32_t hidden_bits(bool gate, uint8_t serial, uint8_t pitch, uint8_t velocity)
{
    return (gate ? UINT32_C(1) << 17 : 0) | (uint32_t)(serial & 7u) << 14 |
           (uint32_t)(pitch & 127u) << 7 | (velocity & 127u);
}

static void finish_start(uint8_t owner, const NativeNote *note)
{
    if (!note->midi && --audio_pending[owner] == 0)
        audio_waiting &= (uint16_t)~(1u << owner);
    --starts;
}

bool tk_native_event(void *context, uint8_t track, const TkNoteEvent *event)
{
    if (!context || track >= TK_TRACKS || !event->token || event->pitch > 127)
        return false;
    uint8_t kind = *(const uint8_t *)context;
    bool midi = kind == TK_NATIVE_MIDI;
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
                finish_start(track, entry);
                *entry = (NativeNote){0};
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
    unsigned limit = midi ? TK_NOTES : 1;
    if (kind == TK_NATIVE_AUDIO || (kind == TK_NATIVE_RELAY && tk_pool_fw_relay_ready()))
        limit = tk_pool_fw_limit(track);
    if (kind == TK_NATIVE_AUDIO)
        limit += hidden_capacity[track];
    if (tk_pool_fw_reserved(track) || !empty || active >= limit || !event->velocity ||
        event->velocity > 127)
        return false;
    bool native_seed = !midi && (event->token & 0x80000000u) && tk_sequence_audio(track);
    *empty = (NativeNote){.token = event->token,
                          .order = ++serial,
                          .shape = kind == TK_NATIVE_AUDIO ? shape_of(event) : 0,
                          .pitch = event->pitch,
                          .velocity = event->velocity,
                          .state = NOTE_PENDING,
                          .destination = track,
                          .kind = kind,
                          .midi = midi,
                          .native_seed = native_seed,
                          .seed_direct = native_seed && event->lane == TK_DIRECT,
                          .queued_at = midi ? 0 : (uint16_t)dkrt_blocks()};
    if (!midi) {
        ++audio_pending[track];
        audio_waiting |= (uint16_t)(1u << track);
    }
    ++starts;
    return true;
}

static bool seed_waiting(uint8_t track)
{
    if (!tk_pool_fw_waiting_helpers(track))
        return false;
    uint16_t now = (uint16_t)dkrt_blocks();
    for (unsigned i = 0; i <= TK_NOTES; ++i) {
        const NativeNote *entry = &notes[track][i];
        if (entry->state == NOTE_PENDING && entry->seed_direct && entry->kind == TK_NATIVE_AUDIO &&
            (uint16_t)(now - entry->queued_at) < AUTO_WAIT_BLOCKS)
            return true;
    }
    return false;
}

void tk_native_backend(bool midi, TkTrackBackend *out)
{
    *out = (TkTrackBackend){
        .output = tk_native_event,
        .context = (void *)(midi ? &midi_binding : &audio_binding),
        .voices = midi ? TK_NOTES : 1,
        .seed_waiting = midi ? NULL : seed_waiting,
    };
}

void tk_native_relay_backend(TkTrackBackend *out)
{
    *out =
        (TkTrackBackend){.output = tk_native_event, .context = (void *)&relay_binding, .voices = 1};
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

bool tk_native_sounding(uint8_t track, uint32_t token)
{
    if (track >= TK_TRACKS || !token)
        return false;
    for (unsigned i = 0; i <= TK_NOTES; ++i)
        if (notes[track][i].token == token)
            return notes[track][i].state == NOTE_SOUNDING;
    return false;
}

static NativeNote *oldest(uint8_t state, uint8_t *owner, uint16_t skipped)
{
    NativeNote *first = NULL;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        if (skipped & (1u << track))
            continue;
        for (unsigned i = 0; i <= TK_NOTES; ++i) {
            NativeNote *entry = &notes[track][i];
            if (entry->state != state)
                continue;
            if (!first || serial - entry->order > serial - first->order) {
                first = entry;
                *owner = track;
            }
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
    bool sent = entry->midi ? tk_native_try_midi_send(track, entry->pitch, velocity, entry->ports)
                            : tk_native_try_send(track, false, entry->pitch, velocity);
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

/* A hidden voice of the owner (finding 375) for a pitch no track of its group can take: while the owner's own voice sounds a note without
 * copies, a note without copies plays on a free hidden voice instead of a helper track. The hidden voice copies the owner's voice record and
 * plays under its amp envelope, so without the owner's own note it would be silent. */
static bool take_hidden(uint8_t owner, NativeNote *note)
{
    if (note->kind != TK_NATIVE_AUDIO || (note->shape >> 18) || !hidden_capacity[owner])
        return false;
    bool sounds = false;
    for (unsigned i = 0; i <= TK_NOTES; ++i) {
        const NativeNote *own = &notes[owner][i];
        if ((own->state == NOTE_SOUNDING || own->state == NOTE_HELD) && own->destination == owner &&
            !own->hidden) {
            if (own->shape >> 18)
                return false; /* its records play the owner's chord */
            sounds = true;
        }
    }
    if (!sounds)
        return false;
    uint8_t capacity = hidden_capacity[owner];
    for (uint8_t k = 1; k <= capacity; ++k) {
        uint8_t h = (uint8_t)((hidden_last[owner] + k - 1u) % capacity + 1u);
        if (!(hidden_used[owner] & (1u << h))) {
            hidden_last[owner] = h;
            note->destination = owner;
            note->hidden = h;
            return true;
        }
    }
    return false;
}

/* With no hidden voice left the owner's held note gets its stock note-off, and the owner's gate opens again: the DSP keeps fading the
 * released note and plays the owner's next note-on from any source (a key, a trig of the sequencer) at full level. */
static void release_held(uint8_t owner)
{
    if (hidden_used[owner])
        return;
    for (unsigned i = 0; i <= TK_NOTES; ++i)
        if (notes[owner][i].state == NOTE_HELD) {
            notes[owner][i].state = NOTE_RELEASE;
            ++releases;
        }
    if (owner_faded[owner]) {
        tk_pool_link_hidden(owner, 0, hidden_bits(true, 0, 0, 0));
        owner_faded[owner] = false;
    }
}

static bool choose_audio_destination(uint8_t owner, NativeNote *note, bool automatic, bool expired)
{
    if (tk_pool_fw_reserved(owner))
        return false;
    uint16_t group = (uint16_t)((1u << owner) | tk_pool_fw_helpers(owner));
    uint16_t active = group & tk_native_audio_active;
    /* A stock owner can play alone when helper setup times out. An extra
     * pitch is too late to add, and RELAY needs a mapped head on every start.
     */
    if (automatic && expired && (active || note->kind != TK_NATIVE_AUDIO))
        return active && take_hidden(owner, note);
    if (!active) {
        /* The first note installs the resolved Sound and locks on the owner
         * before subsequent notes inherit that track's live controls. */
        note->destination = owner;
        return !automatic || expired || tk_pool_fw_start_ready(owner, owner);
    }
    uint16_t available = group & ~active;
    if (automatic)
        for (uint8_t track = 0; track < TK_TRACKS; ++track)
            if ((available & (1u << track)) && !tk_pool_fw_start_ready(owner, track))
                available &= (uint16_t)~(1u << track);
    if (!available)
        return take_hidden(owner, note);
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
    replenish_midi();
    uint16_t skipped = 0;
    /* The stock audition mailbox owns 16 backing records. Dispatch a bounded
     * batch and yield the task to its consumers. Releases win over new notes.
     */
    for (unsigned n = 0; n < 4 && (starts || releases);) {
        uint8_t track = 0;
        NativeNote *entry = releases ? oldest(NOTE_RELEASE, &track, 0) : NULL;
        if (entry && entry->hidden) {
            /* A hidden voice's key is up: its gate closes (the DSP fades it at the note's own velocity). */
            tk_pool_link_hidden(track, entry->hidden,
                                hidden_bits(false, hidden_serial[track][entry->hidden],
                                            entry->pitch, entry->velocity));
            hidden_used[track] &= (uint8_t)~(1u << entry->hidden);
            *entry = (NativeNote){0};
            --releases;
            ++n;
            release_held(track);
            continue;
        }
        if (entry && !entry->midi && entry->destination == track && hidden_used[track]) {
            /* The owner's key is up while hidden voices sound: its sample fades, its stock note-off waits for them. */
            tk_pool_link_hidden(track, 0, 0);
            owner_faded[track] = true;
            entry->state = NOTE_HELD;
            --releases;
            ++n;
            continue;
        }
        if (entry) {
            if (!send(track, entry, 0))
                return;
            if (entry->kind == TK_NATIVE_AUDIO && (entry->shape >> 18))
                /* No copies at the destination's next note-on (a trig of its own plays one note); the copies of this one play their
                 * release at the same intervals. */
                tk_pool_link_chord(entry->destination, entry->shape & ~(UINT32_C(3) << 18));
            if (!entry->midi) {
                tk_native_audio_active &= ~(1u << entry->destination);
                active_helpers &= (uint16_t)~(1u << entry->destination);
                tk_pool_fw_note_edge(entry->destination);
            }
            *entry = (NativeNote){0};
            --releases;
            ++n;
            continue;
        }
        entry = oldest(NOTE_PENDING, &track, skipped);
        if (!entry)
            return;
        int kind = tk_native_kind(track);
        bool pooled =
            kind == TK_NATIVE_AUDIO || (kind == TK_NATIVE_RELAY && tk_pool_fw_relay_ready());
        bool automatic = pooled && tk_pool_fw_automatic(track);
        bool expired = (uint16_t)(budget_at - entry->queued_at) >= AUTO_WAIT_BLOCKS;
        if (kind != entry->kind ||
            (pooled && !choose_audio_destination(track, entry, automatic, expired))) {
            if (kind == entry->kind && automatic && !expired) {
                /* Later starts on the same track wait behind its oldest. */
                skipped |= (uint16_t)(1u << track);
                continue;
            }
            finish_start(track, entry);
            *entry = (NativeNote){0};
            ++n;
            continue;
        }
        if (entry->midi) {
            int channel = tk_native_channel(track);
            uint8_t ports = tk_native_midi_ports();
            bool busy = ports && !midi_room(ports);
            if (!ports || busy || channel < 0 || channel > 15 ||
                midi_collision(entry, (uint8_t)channel)) {
                if (busy && tk_runtime_midi_busy)
                    tk_runtime_midi_busy();
                finish_start(track, entry);
                *entry = (NativeNote){0};
                ++n;
                continue;
            }
            entry->channel = (uint8_t)channel;
            entry->ports = ports;
        }
        if (entry->hidden) {
            /* A hidden voice: no stock note, its word starts it (a new serial with the gate open). */
            uint8_t h = entry->hidden;
            hidden_used[track] |= (uint8_t)(1u << h);
            hidden_serial[track][h] = (uint8_t)((hidden_serial[track][h] + 1u) & 7u);
            tk_pool_link_hidden(
                track, h,
                hidden_bits(true, hidden_serial[track][h], entry->pitch, entry->velocity));
            finish_start(track, entry);
            entry->state = NOTE_SOUNDING;
            ++n;
            continue;
        }
        /* The destination's chord is queued ahead of the note (a note without copies clears it). The frame word may reach the DSP after
         * the note-on; the DSP then takes the copies late. */
        if (entry->kind == TK_NATIVE_AUDIO)
            tk_pool_link_chord(entry->destination, entry->shape);
        if (entry->kind == TK_NATIVE_AUDIO && owner_faded[entry->destination]) {
            tk_pool_link_hidden(entry->destination, 0,
                                hidden_bits(true, 0, 0, 0)); /* the owner's own note sounds again */
            owner_faded[entry->destination] = false;
        }
        if (!send(track, entry, entry->velocity))
            return;
        if (entry->midi) {
            if (entry->ports & TK_MIDI_OUT_USB)
                usb_budget -= TK_MIDI_USB_PAIR_BYTES * UNITS_PER_BYTE;
            if (entry->ports & TK_MIDI_OUT_DIN)
                din_budget -= TK_MIDI_DIN_PAIR_BYTES * UNITS_PER_BYTE;
        } else {
            tk_native_audio_active |= 1u << entry->destination;
            if (entry->destination != track)
                active_helpers |= (uint16_t)(1u << entry->destination);
            tk_pool_fw_note_edge(entry->destination);
        }
        finish_start(track, entry);
        entry->state = NOTE_SOUNDING;
        ++n;
    }
}
