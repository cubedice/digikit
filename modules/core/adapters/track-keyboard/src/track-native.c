#include "track-native.h"
#include "track-memory.h"
#include "track-pool.h"

extern void tk_runtime_midi_busy(void) __attribute__((weak));

enum NoteState { NOTE_FREE, NOTE_PENDING, NOTE_SOUNDING, NOTE_RELEASE };
enum { UNITS_PER_BYTE = 1500, AUTO_WAIT_BLOCKS = 120 };

typedef struct {
    uint32_t token, order;
    uint8_t pitch, velocity, state, channel, destination, ports, kind;
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
    if (tk_pool_fw_reserved(track) || !empty || active >= limit || !event->velocity ||
        event->velocity > 127)
        return false;
    bool native_seed = !midi && (event->token & 0x80000000u) && tk_sequence_audio(track);
    *empty = (NativeNote){.token = event->token,
                          .order = ++serial,
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
        return false;
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
    replenish_midi();
    uint16_t skipped = 0;
    /* The stock audition mailbox owns 16 backing records. Dispatch a bounded
     * batch and yield the task to its consumers. Releases win over new notes.
     */
    for (unsigned n = 0; n < 4 && (starts || releases);) {
        uint8_t track = 0;
        NativeNote *entry = releases ? oldest(NOTE_RELEASE, &track, 0) : NULL;
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
