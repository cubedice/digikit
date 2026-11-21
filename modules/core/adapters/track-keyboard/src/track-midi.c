#ifdef __m68k__
#pragma GCC optimize("no-jump-tables")
#endif

#include "track-midi.h"
#include "track-capture.h"
#include "track-memory.h"
#include "track-service.h"

extern uint32_t dkrt_blocks(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

#define MIDI_CHANNELS 16
#define MIDI_ROUTES (2 * MIDI_CHANNELS)
#define MIDI_QUEUE 64
#define MIDI_RELEASE_RESERVE 8
#define MIDI_KEYS (2 * TK_TRACKS * TK_NOTES)

enum MidiKind { MIDI_PRESS, MIDI_RELEASE, MIDI_PEDAL, MIDI_RESET, MIDI_SELECT };

typedef struct {
    uint32_t time;
    uint16_t tracks, record;
    uint8_t kind, route, note, value;
    bool step;
} MidiEvent;

typedef struct {
    uint16_t tracks, claimed, recorded;
    uint8_t route, note;
    bool used, stock, disconnected;
} MidiKey;

/* Only MIDI's native per-destination calls consume this handoff. Each
 * physical parser completes its call before receiving its next message.
 */
typedef struct {
    uint16_t tracks, record;
    uint8_t channel, note;
    bool on;
} NativeBypass;

static MidiEvent queue[MIDI_QUEUE];
#ifdef __m68k__
extern void *tk_fw_alloc(uint32_t size);
static MidiKey *keys;

bool tk_midi_init(void)
{
    if (keys)
        return true;
    MidiKey *memory = tk_fw_alloc(sizeof(*memory) * MIDI_KEYS);
    if (!memory)
        return false;
    memset(memory, 0, sizeof(*memory) * MIDI_KEYS);
    keys = memory;
    return true;
}
#else
static MidiKey keys[MIDI_KEYS];
#endif
static NativeBypass bypass[2];
/* Overflowing or deliberately canceled strikes still own their next release. */
static uint32_t ignored[MIDI_ROUTES][4];
static uint16_t assigned[MIDI_ROUTES], pedals[MIDI_ROUTES];
static uint16_t active, learning, panic, key_slots;
static int8_t selector_octave[TK_TRACKS], selector_channel[TK_TRACKS];
static uint8_t write_at, read_at, rx_ports;
static bool busy;

static uint16_t track_bit(uint8_t track)
{
    return track < TK_TRACKS ? (uint16_t)(1u << track) : 0;
}

static bool enqueue_capture(uint8_t kind, uint16_t tracks, uint8_t route, uint8_t note,
                            uint8_t value, uint16_t record, bool step)
{
    if (!tracks)
        return true;
    unsigned queued = (write_at + MIDI_QUEUE - read_at) % MIDI_QUEUE;
    bool onset = kind == MIDI_PRESS || kind == MIDI_SELECT || (kind == MIDI_PEDAL && value);
    if (onset && queued >= MIDI_QUEUE - 1u - MIDI_RELEASE_RESERVE) {
        busy = true;
        return false;
    }
    uint8_t next = (uint8_t)((write_at + 1u) % MIDI_QUEUE);
    if (next == read_at) {
        busy = true;
        panic |= tracks;
        return false;
    }
    queue[write_at] = (MidiEvent){.time = record ? dkrt_blocks() : 0,
                                  .tracks = tracks,
                                  .record = record,
                                  .kind = kind,
                                  .route = route,
                                  .note = note,
                                  .value = value,
                                  .step = step};
    write_at = next;
    return true;
}

static bool enqueue(uint8_t kind, uint16_t tracks, uint8_t route, uint8_t note, uint8_t value)
{
    return enqueue_capture(kind, tracks, route, note, value, 0, false);
}

/* Called with IRQs masked. Keep physical identities until their releases. */
static void revoke(uint16_t tracks)
{
    active &= (uint16_t)~tracks;
    learning &= (uint16_t)~tracks;
    for (unsigned i = 0; i < key_slots; ++i)
        if (keys[i].used)
            keys[i].tracks &= (uint16_t)~tracks;
    for (unsigned i = 0; i < MIDI_ROUTES; ++i) {
        assigned[i] &= (uint16_t)~tracks;
        pedals[i] &= (uint16_t)~tracks;
    }
    for (uint8_t i = read_at; i != write_at; i = (uint8_t)((i + 1u) % MIDI_QUEUE)) {
        queue[i].tracks &= (uint16_t)~tracks;
        queue[i].record &= (uint16_t)~tracks;
    }
}

static void clear(uint8_t track)
{
    for (unsigned route = 0; route < MIDI_ROUTES; ++route)
        tk_service_clear_source(track, route < MIDI_CHANNELS ? TK_USB : TK_DIN,
                                (uint8_t)(route % MIDI_CHANNELS));
}

void tk_midi_cancel_track(uint8_t track)
{
    uint16_t tracks = track_bit(track);
    if (!tracks)
        return;
    uint32_t sr = dkrt_irq_mask();
    bool owned = false;
    for (unsigned i = 0; i < MIDI_ROUTES; ++i)
        owned |= ((assigned[i] | pedals[i]) & tracks) != 0;
    revoke(tracks);
    dkrt_irq_restore(sr);
    if (owned)
        clear(track);
}

void tk_midi_reset(void)
{
    uint32_t sr = dkrt_irq_mask();
    uint16_t owned = panic;
    for (unsigned i = 0; i < MIDI_ROUTES; ++i)
        owned |= assigned[i] | pedals[i];
    revoke(UINT16_MAX);
    read_at = write_at;
    panic = 0;
    busy = false;
    dkrt_irq_restore(sr);
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (owned & track_bit(track))
            clear(track);
}

static void dispatch(const MidiEvent *event)
{
    if (!event->tracks)
        return;
    uint8_t route = event->route;
    uint8_t channel = route % MIDI_CHANNELS;
    uint8_t source = route < MIDI_CHANNELS ? TK_USB : TK_DIN;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        if (!(event->tracks & track_bit(track)))
            continue;
        if (event->record & track_bit(track))
            tk_capture_input_at(track, source, channel, event->note, event->note, event->value,
                                event->step, event->time);
        switch (event->kind) {
        case MIDI_PRESS:
            tk_runtime_learn_note(track, event->note, event->value);
            tk_service_press(track, source, channel, event->note, event->note, event->value);
            break;
        case MIDI_RELEASE:
            tk_service_release(track, source, channel, event->note);
            break;
        case MIDI_PEDAL:
            tk_service_pedal(track, source, channel, event->value != 0);
            break;
        case MIDI_RESET:
            tk_service_clear_source(track, source, channel);
            break;
        case MIDI_SELECT:
            tk_service_select_chord(track, (enum TkChord)(TK_CHORD_MAJ + event->note % 12));
            tk_capture_shape_changed(track);
            break;
        }
    }
}

static void disconnect_port(uint8_t port)
{
    uint16_t tracks[MIDI_CHANNELS];
    uint8_t first = (uint8_t)(port * MIDI_CHANNELS);
    uint32_t sr = dkrt_irq_mask();
    for (uint8_t channel = 0; channel < MIDI_CHANNELS; ++channel) {
        uint8_t route = (uint8_t)(first + channel);
        tracks[channel] = assigned[route] | pedals[route];
        assigned[route] = pedals[route] = 0;
        for (unsigned i = 0; i < 4; ++i)
            ignored[route][i] = 0;
    }
    for (unsigned i = 0; i < key_slots; ++i)
        if (keys[i].used && keys[i].route / MIDI_CHANNELS == port) {
            keys[i].tracks = 0;
            keys[i].disconnected = true;
        }
    for (uint8_t i = read_at; i != write_at; i = (uint8_t)((i + 1u) % MIDI_QUEUE))
        if (queue[i].route / MIDI_CHANNELS == port)
            queue[i].tracks = queue[i].record = 0;
    dkrt_irq_restore(sr);
    for (uint8_t channel = 0; channel < MIDI_CHANNELS; ++channel) {
        MidiEvent event = {
            .tracks = tracks[channel], .kind = MIDI_RESET, .route = (uint8_t)(first + channel)};
        dispatch(&event);
    }
}

void tk_midi_poll(void)
{
    uint32_t sr = dkrt_irq_mask();
    uint8_t ports = tk_midi_notes_enabled() ? tk_midi_receive_ports() : 0;
    dkrt_irq_restore(sr);
    uint8_t disconnected = rx_ports & (uint8_t)~ports;
    rx_ports = ports;
    for (uint8_t port = 0; port < 2; ++port)
        if (disconnected & (1u << port))
            disconnect_port(port);

    uint16_t enabled = 0;
    uint16_t learned = tk_runtime_learn_mask();
    int8_t octaves[TK_TRACKS], channels[TK_TRACKS];
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        octaves[track] = -1;
        channels[track] = -1;
        if (!tk_service_enabled(track))
            continue;
        enabled |= track_bit(track);
        tk_service_selector(track, true, &octaves[track], &channels[track]);
    }

    sr = dkrt_irq_mask();
    uint16_t disabled = active & (uint16_t)~enabled;
    uint16_t stopped_learn = learning & (uint16_t)~(learned | enabled);
    uint16_t cleanup = (uint16_t)((disabled | stopped_learn | panic) & (active | learning));
    if (cleanup)
        revoke(cleanup);
    panic = 0;
    bool missed = busy;
    busy = false;
    active = enabled;
    learning = learned;
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        selector_octave[i] = octaves[i];
        selector_channel[i] = channels[i];
    }
    dkrt_irq_restore(sr);

    if (missed)
        tk_runtime_midi_busy();
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (cleanup & track_bit(track))
            clear(track);
    for (unsigned i = 0; i < MIDI_QUEUE; ++i) {
        sr = dkrt_irq_mask();
        if (read_at == write_at) {
            dkrt_irq_restore(sr);
            break;
        }
        MidiEvent event = queue[read_at];
        read_at = (uint8_t)((read_at + 1u) % MIDI_QUEUE);
        dkrt_irq_restore(sr);
        dispatch(&event);
    }
}

static int controller(uint8_t route, uint8_t note, uint8_t value, uint32_t native, int existing)
{
    if (note != 64 && note != 120 && note != 121 && note != 123)
        return 0;
    uint16_t eligible = (uint16_t)native & active;
    if (note == 64 && value >= 64u) {
        if (!existing && eligible && enqueue(MIDI_PEDAL, eligible, route, 0, 1))
            pedals[route] |= eligible;
    } else {
        /* A release/reset follows ownership even after an Auto Channel focus
         * change or a port/channel assignment change. Leave a newly native
         * destination able to see this controller as well.
         */
        uint16_t tracks = pedals[route];
        if (note == 120 || note == 123) {
            tracks |= assigned[route];
            if (tracks)
                for (unsigned i = 0; i < key_slots; ++i)
                    if (keys[i].used && keys[i].route == route)
                        keys[i].tracks = 0;
            assigned[route] = 0;
        }
        enqueue(note == 120 || note == 123 ? MIDI_RESET : MIDI_PEDAL, tracks, route, 0, 0);
        pedals[route] = 0;
    }
    return !existing && eligible && !(native & ~active);
}

static void bypass_native(uint8_t route, uint8_t note, bool on, uint16_t tracks, uint16_t record)
{
    bypass[route / MIDI_CHANNELS] = (NativeBypass){.tracks = tracks,
                                                   .record = record,
                                                   .channel = route % MIDI_CHANNELS,
                                                   .note = note,
                                                   .on = on};
}

int tk_midi_native_action(uint32_t track, uint32_t channel, uint32_t note, uint32_t source,
                          uint32_t on)
{
    if (track >= TK_TRACKS || (source != 2 && source != 4 && source != 8 && source != 16))
        return TK_MIDI_NATIVE;
    uint32_t sr = dkrt_irq_mask();
    NativeBypass *skip = &bypass[source >= 8];
    uint16_t bit = track_bit((uint8_t)track);
    bool matched =
        (skip->tracks & bit) && skip->channel == channel && skip->note == note && skip->on == on;
    int action = TK_MIDI_NATIVE;
    if (matched)
        action = skip->record & bit ? TK_MIDI_RECORD_ONLY : TK_MIDI_SUPPRESS;
    if (matched)
        skip->tracks &= (uint16_t)~bit;
    dkrt_irq_restore(sr);
    return action;
}

static int note_message(uint8_t route, bool on, uint8_t note, uint8_t value, uint32_t native,
                        int existing)
{
    uint32_t bit = (uint32_t)1u << (note % 32u);
    uint32_t *discarded = &ignored[route][note / 32u];
    if (*discarded & bit) {
        if (!on)
            *discarded &= ~bit;
        return 1;
    }

    MidiKey *free_key = NULL;
    for (unsigned i = 0; i < key_slots; ++i) {
        MidiKey *key = &keys[i];
        if (!key->used) {
            if (!free_key)
                free_key = key;
        } else if (key->route == route && key->note == note) {
            if (on && key->disconnected) {
                if (existing)
                    return 0;
                key->used = false;
                free_key = key;
                break;
            }
            uint16_t recording = on ? 0 : key->recorded;
            if (!on) {
                enqueue_capture(MIDI_RELEASE, key->tracks, route, note, 0, recording, false);
                key->used = false;
                while (key_slots && !keys[key_slots - 1].used)
                    --key_slots;
            }
            if (!key->stock && !recording)
                return 1;
            bypass_native(route, note, on, key->claimed & (uint16_t)~recording, 0);
            return existing ? -1 : 0;
        }
    }
    if (existing || !on || !(active | learning) || !tk_midi_notes_enabled())
        return 0;

    uint8_t channel = route % MIDI_CHANNELS;
    uint16_t selected = track_bit(tk_midi_selected_track());
    uint16_t select = 0;
    uint16_t owned = (uint16_t)native & (active | learning);
    bool mixed = (native & ~(active | learning)) != 0;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        uint16_t t = track_bit(track);
        if (!(active & t) || (learning & t))
            continue;
        if (selector_channel[track] >= 0) {
            /* An otherwise unused channel can address the selected track.
             * Never consume the dedicated channel of a different track.
             */
            if (selector_channel[track] == channel && (selected & t) && !(native & ~t)) {
                owned |= t;
                select |= t;
            }
        } else if ((native & t) && selector_octave[track] == (int8_t)(note / 12u)) {
            select |= t;
        }
    }
    /* Preserve native single-note trig shortcuts. An explicitly configured
     * selector range may deliberately use those keys without triggering it.
     */
    if (!owned || (note < 16u && (mixed || select != owned)))
        return 0;
    if (!free_key && key_slots != MIDI_KEYS)
        free_key = &keys[key_slots++];
    if (!free_key) {
        busy = true;
        if (mixed)
            return 0;
        *discarded |= bit;
        return 1;
    }
    uint16_t tracks = owned & (uint16_t)~select;
    uint32_t mode = tk_capture_midi_mode();
    uint16_t record = mode == TK_CAPTURE_OFF ? 0 : (tracks & active & (uint16_t)~learning);
    enqueue(MIDI_SELECT, select, route, note, value);
    if (!enqueue_capture(MIDI_PRESS, tracks, route, note, value, record, mode == TK_CAPTURE_STEP))
        tracks = record = 0;
    assigned[route] |= tracks;
    *free_key = (MidiKey){.tracks = tracks,
                          .claimed = owned,
                          .recorded = record,
                          .route = route,
                          .note = note,
                          .used = true,
                          .stock = mixed};
    if (mixed || record)
        bypass_native(route, note, true, owned, record);
    return !mixed && !record;
}

int tk_midi_channel_message(const uint8_t *bytes, uint32_t len, uint32_t source, int existing_only)
{
    if (!bytes || len != 3 || bytes[1] > 127 || bytes[2] > 127)
        return 0;
    if (source != 2 && source != 4 && source != 8 && source != 16)
        return 0;
    uint8_t type = bytes[0] & 0xf0u;
    if (type != 0x80 && type != 0x90 && type != 0xb0)
        return 0;

    uint8_t route = (uint8_t)((bytes[0] & 15u) + (source < 8 ? 0 : MIDI_CHANNELS));
    uint32_t sr = dkrt_irq_mask();
    if (existing_only)
        bypass[route / MIDI_CHANNELS].tracks = 0;
    uint32_t native = 0;
    if (!existing_only && (active | learning))
        native = tk_midi_route((uint8_t)(route % MIDI_CHANNELS), source == 4 || source == 16);
    int consumed = type == 0xb0 ? controller(route, bytes[1], bytes[2], native, existing_only)
                                : note_message(route, type == 0x90 && bytes[2], bytes[1], bytes[2],
                                               native, existing_only);
    dkrt_irq_restore(sr);
    return consumed;
}
