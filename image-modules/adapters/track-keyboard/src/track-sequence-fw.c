#include "track-sequence.h"
#include "track-canopy.h"
#include "track-capture.h"
#include "track-memory.h"
#include "track-native.h"
#include "track-pattern-fw.h"
#include "track-pool.h"
#include "tk-seam.h"
#include "../../instrument-play/src/cf/src-control.h"

extern const uint32_t tk_seq_length_table[128];
extern const uint8_t tk_seq_audio_mute_lit[], tk_seq_audio_solo_lit[];
extern const uint8_t tk_seq_midi_mute_lit[], tk_seq_midi_solo_lit[];
extern const uint8_t tk_seq_record_ui_lit[];
extern const uint8_t tk_seq_queued_lit[], tk_seq_direct_target_lit[];
extern const uint8_t tk_seq_direct_request_lit[], tk_seq_direct_countdown_lit[];
#ifdef DK_SERVICE
#define pp_position ((volatile uint32_t *)TK_DATA(pp_position))
#else
extern volatile uint32_t pp_position[16];
#endif
extern uint32_t pp_current_pattern(void);
extern uint32_t pp_transport_running(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

enum {
    TRACK_BYTES = 0x6a5,
    NOTE_TABLE = 0x6a50,
    NOTE_COUNT = 8192,
    NOTE_NEXT = 0xd000,
    DEFAULT_VELOCITY = 0x480,
    DEFAULT_LENGTH = 0x481,
    DEFAULT_PITCH = 0x485,
    SOUND_LOCKS = 0x400,
    STAGED_EVENTS = 257,
};

typedef struct {
    const uint32_t *volatile event;
    uintptr_t pattern;
    uint8_t track, step, from_start;
} Staged;

extern void *tk_fw_alloc(uint32_t size);
static Staged *volatile staged;
static Staged start_cache[TK_TRACKS];
static uint16_t midi_tracks, relay_tracks;

volatile uint32_t tk_sequence_audio_hits, tk_sequence_audio_locked_hits, tk_sequence_midi_hits;
volatile uint32_t tk_sequence_midi_info[5]; /* scheduled, due, matched, enabled, decoded */

static void barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

bool tk_sequence_fw_init(void)
{
    if (staged)
        return true;
    Staged *memory = tk_fw_alloc(sizeof(*memory) * STAGED_EVENTS);
    if (!memory)
        return false;
    memset(memory, 0, sizeof(*memory) * STAGED_EVENTS);
    barrier();
    staged = memory;
    return true;
}

static uintptr_t literal(const uint8_t *operand)
{
    return (uintptr_t)operand[0] << 24 | (uintptr_t)operand[1] << 16 | (uintptr_t)operand[2] << 8 |
           operand[3];
}

static uint32_t read32(const uint8_t *operand)
{
    return *(const volatile uint32_t *)literal(operand);
}

void tk_sequence_native_targets(TkSequenceNativeTarget targets[TK_SEQUENCE_TARGETS])
{
    uint32_t sr = dkrt_irq_mask();
    /* Finding 365 identifies the pattern/kit pairs used by the scheduler.
     * The longwords following the queued pattern are the scheduled, queued,
     * and outgoing kits. The direct request stores its kit next to its pattern. */
    const volatile uint32_t *queued = (const volatile uint32_t *)literal(tk_seq_queued_lit);
    const volatile uint32_t *direct = (const volatile uint32_t *)literal(tk_seq_direct_target_lit);
    targets[0] = (TkSequenceNativeTarget){pp_current_pattern(), queued[2]};
    targets[1] = (TkSequenceNativeTarget){queued[0], queued[3]};
    bool pending =
        read32(tk_seq_direct_request_lit) || (int32_t)read32(tk_seq_direct_countdown_lit) > 0;
    targets[2] =
        pending ? (TkSequenceNativeTarget){direct[0], direct[1]} : (TkSequenceNativeTarget){0};
    dkrt_irq_restore(sr);
}

int tk_sequence_target_kind(uint32_t kit, uint8_t track)
{
    if (kit < 0x40000000u || kit > 0x48000000u - 0x5574u || (kit & 3u) || track >= TK_TRACKS)
        return TK_NATIVE_UNSUPPORTED;
    if (*(const uint32_t *)(uintptr_t)kit != UINT32_C(0x4b495420)) /* KIT record */
        return TK_NATIVE_UNSUPPORTED;
    /* An inactive kit is serialized data. Read the Sound field directly
     * because the active-kit accessor calls methods that are not bound yet. */
    const uint8_t *native = (const uint8_t *)(uintptr_t)kit;
    int type = native[0xd6u + 0x450u * track];
    const uint8_t *mode = native + 0x552eu;
    uint16_t midi = (uint16_t)((uint16_t)mode[0] << 8 | mode[1]);
    if (midi & (UINT32_C(1) << track))
        return type == 5 ? TK_NATIVE_MIDI : TK_NATIVE_UNSUPPORTED;
    return type >= 0 && type <= 6 && type != 5 ? TK_NATIVE_AUDIO : TK_NATIVE_UNSUPPORTED;
}

uint32_t tk_capture_midi_mode(void)
{
    uintptr_t ui = read32(tk_seq_record_ui_lit);
    if (!ui)
        return TK_CAPTURE_OFF;
    const volatile uint8_t *state = (const volatile uint8_t *)ui;
    if (state[0x269] && state[0x272])
        return TK_CAPTURE_STEP;
    if (state[0x271] && (pp_position[5] & 1u))
        return TK_CAPTURE_LIVE;
    const volatile uint32_t *held = (const volatile uint32_t *)(ui + 0x258);
    return (held[0] | held[1] | held[2] | held[3]) ? TK_CAPTURE_HELD : TK_CAPTURE_OFF;
}

/* Live recording uses the recorder placement below. The note-list observer
 * accounts for step recording and edits made with a held trig. */
void tk_capture_note_list_commit(uint32_t note_table, uint32_t track, uint32_t step, uint32_t pitch,
                                 uint32_t velocity)
{
    if (tk_capture_midi_mode() != TK_CAPTURE_LIVE)
        tk_capture_note_commit(note_table, track, step, pitch, velocity);
}

void tk_fw_capture_native_live(const uint32_t *track_editor, uint32_t track, uint32_t step,
                               uint32_t pitch, uint32_t velocity)
{
    if (!track_editor || track >= TK_TRACKS || step >= TK_STEPS || pitch > 127)
        return;
    /* The recorder already resolved the editor and the potentially remapped
     * step. Each pitch retains a placement when the four-slot list is full. */
    const uint32_t *editor = (const uint32_t *)(uintptr_t)track_editor[0xc];
    if (!editor || !*editor)
        return;
    const uint32_t *methods = (const uint32_t *)(uintptr_t)*editor;
    typedef uint32_t (*EditNotes)(const uint32_t *);
    EditNotes notes = (EditNotes)(uintptr_t)methods[0xa];
    if (notes)
        tk_capture_note_commit(notes(editor), track, step, pitch, velocity <= 127 ? velocity : 100);
}

static Staged *slot_for(const uint32_t *event)
{
    return &staged[((uintptr_t)event >> 2) % STAGED_EVENTS];
}

static void stage(Staged *slot, const uint32_t *event, uint32_t track, uint32_t step,
                  uintptr_t note_table, bool from_start)
{
    slot->event = NULL;
    barrier();
    slot->pattern = note_table - NOTE_TABLE;
    slot->track = (uint8_t)track;
    slot->step = (uint8_t)step;
    slot->from_start = from_start;
    barrier();
    slot->event = event;
}

void tk_fw_seq_stage(const uint32_t *event, uint32_t track, uint32_t step, uintptr_t note_table)
{
    if (staged && event && track < TK_TRACKS && step < TK_STEPS && note_table >= NOTE_TABLE)
        stage(slot_for(event), event, track, step, note_table, false);
}

void tk_fw_seq_start_stage(const uint32_t *event, uint32_t track, uint32_t step,
                           uintptr_t note_table)
{
    /* Stock can build the first record before the adapter heap is ready. */
    if (event && track < TK_TRACKS && step < TK_STEPS && note_table >= NOTE_TABLE)
        stage(&start_cache[track], event, track, step, note_table, false);
}

void tk_fw_seq_start_copy(const uint32_t *event, const uint32_t *source, uint32_t track)
{
    if (!staged || !event || !source || track >= TK_TRACKS)
        return;
    const Staged *cached = &start_cache[track];
    if (cached->event != source)
        return;
    barrier();
    uintptr_t pattern = cached->pattern;
    uint32_t step = cached->step;
    if (pattern && cached->event == source)
        stage(slot_for(event), event, track, step, pattern + NOTE_TABLE, true);
}

void tk_fw_seq_stage_midi(const uint32_t *event, uint32_t track, uint32_t step,
                          uintptr_t note_table)
{
    ++tk_sequence_midi_info[0];
    tk_fw_seq_stage(event, track, step, note_table);
}

static bool take(const uint32_t *event, uint32_t track, uintptr_t note_table, uintptr_t track_data,
                 bool require_start, TkSequenceHit *hit)
{
    if (!staged)
        return false;
    Staged *slot = slot_for(event);
    if (slot->event != event)
        return false;
    barrier();
    uintptr_t pattern = slot->pattern;
    uint8_t step = slot->step;
    if (track != slot->track || pattern + NOTE_TABLE != note_table ||
        pattern + track * TRACK_BYTES != track_data || (require_start && !slot->from_start) ||
        slot->event != event)
        return false;
    /* Only the due consumers clear entries. Timer interrupt reuse republishes
     * the slot, so a collision defaults to native dispatch.
     */
    slot->event = NULL;
    *hit = (TkSequenceHit){.pattern = pattern, .track = (uint8_t)track, .step = step};
    return true;
}

static int signed16(const uint8_t *addr)
{
    return (int16_t)((uint16_t)addr[0] << 8 | addr[1]);
}

static int inherited(int8_t value, uint8_t fallback)
{
    return value < 0 ? (int8_t)fallback : value;
}

static bool decode(TkSequenceHit *hit, const uint8_t *table, const uint8_t *track, int head,
                   const uint8_t *fallback, bool midi)
{
    if (head < -1 || head >= NOTE_COUNT)
        return false;
    const uint8_t *record = head < 0 ? fallback : table + head * 6;
    uint8_t group = record[5];
    int root = inherited((int8_t)record[2], track[DEFAULT_PITCH]);
    for (unsigned visited = 0; visited < (midi ? 4u : 1u); ++visited) {
        if (record[5] != group)
            return true;
        int pitch = inherited((int8_t)record[2], track[DEFAULT_PITCH + visited]);
        int velocity = inherited((int8_t)record[3], track[DEFAULT_VELOCITY]);
        int length = inherited((int8_t)record[4], track[DEFAULT_LENGTH]);
        bool enabled = !visited || pitch != 64;
        if (visited)
            pitch += root - 64;
        if (enabled && pitch >= 0 && pitch <= 127 && velocity > 0 && velocity <= 127 &&
            length >= 0 && length <= 127)
            hit->notes[hit->count++] =
                (TkSequenceNote){(uint8_t)pitch, (uint8_t)velocity, (uint8_t)length};
        if (!midi || head < 0)
            return true;
        head = signed16(table + NOTE_NEXT + 2 * head);
        if (head == -1)
            return true;
        if (head < 0 || head >= NOTE_COUNT)
            return false;
        record = table + head * 6;
    }
    return !midi || record[5] != group;
}

uint8_t tk_sequence_native_pitches(uint32_t pattern, uint8_t track, uint8_t step, bool midi,
                                   uint8_t pitches[4])
{
    if (!tk_pattern_fw_note_step(pattern, track, step))
        return 0;
    const uint8_t *base = (const uint8_t *)(uintptr_t)pattern;
    const uint8_t *table = base + NOTE_TABLE;
    const uint8_t *data = base + TRACK_BYTES * track;
    int head = signed16(table + 0xc000u + 2u * (TK_STEPS * track + step));
    const uint8_t fallback[6] = {track, step, 0xff, 0xff, 0xff, 0};
    TkSequenceHit hit = {0};
    if (!decode(&hit, table, data, head, fallback, midi))
        return 0;
    for (uint8_t i = 0; i < hit.count; ++i)
        pitches[i] = hit.notes[i].pitch;
    return hit.count;
}

static bool end_stock(uint8_t source, TkSequenceHit *hit)
{
    hit->count = 0;
    tk_sequence_offer(source, hit);
    return false;
}

int tk_fw_seq_audio_due(const uint32_t *event)
{
    const uint32_t flags = event[0xe];
    /* The audio builder uses type 0 for heap events and type 1 for a
     * cached record. Only a verified START copy can admit the latter. */
    if (event[0] > 1 || event[1] != 1 || event[5] != 1 || !(flags & 1u))
        return false;
    bool reserved = event[4] < TK_TRACKS && tk_pool_fw_reserved((uint8_t)event[4]);
    if (reserved && !event[0])
        return true;
    TkSequenceHit hit;
    if (!take(event, event[4], event[7], event[0xb], event[0] == 1, &hit))
        return false;
    if (reserved)
        return true;
    const uint8_t *track = (const uint8_t *)(uintptr_t)event[0xb];
    hit.parameters_only = !(flags & 0x80u);
    if (hit.parameters_only && tk_canopy_routed(hit.track))
        return false;
    if (!hit.parameters_only && !tk_sequence_wants_seed(hit.pattern, hit.track, hit.step))
        return end_stock(TK_SEQUENCE_AUDIO, &hit);
    hit.audio.sound = event[0x10] ? event[0x10] : event[0xf];
    if ((relay_tracks & (1u << hit.track)) && !tk_native_relay_sound(hit.audio.sound))
        return hit.parameters_only ? false : end_stock(TK_SEQUENCE_AUDIO, &hit);
    int8_t pool = (int8_t)track[SOUND_LOCKS + hit.step];
    if (pool >= 0)
        hit.audio.pool_sound = (uint8_t)pool + 1u;
    hit.audio.flags = flags & ~2u; /* The shared scheduler supplies retriggers. */
    if (!hit.parameters_only && !hit.audio.sound)
        return end_stock(TK_SEQUENCE_AUDIO, &hit);
    const uint32_t *locks = (const uint32_t *)(uintptr_t)event[0x15];
    if (locks) {
        uint32_t count = locks[2];
        if (count > TK_NATIVE_LOCKS_PER_HIT)
            return hit.parameters_only ? false : end_stock(TK_SEQUENCE_AUDIO, &hit);
        hit.audio.count = (uint8_t)count;
        const uint16_t *values = (const uint16_t *)(locks + 5);
        for (uint32_t i = 0; i < count; ++i, values += 4) {
            if (values[0] >= TK_NATIVE_LOCKS_PER_HIT)
                return hit.parameters_only ? false : end_stock(TK_SEQUENCE_AUDIO, &hit);
            hit.audio.locks[i] = (TkSequenceLock){.parameter = values[0], .value = values[1]};
        }
    }
    if (hit.parameters_only) {
        if (hit.audio.count)
            tk_sequence_offer(TK_SEQUENCE_AUDIO, &hit);
        return false; /* Stock still applies the automation at its due time. */
    }
    if (!decode(&hit, (const uint8_t *)(uintptr_t)event[7], track, (int32_t)event[8],
                (const uint8_t *)(event + 9), false) ||
        !hit.count || !tk_sequence_offer(TK_SEQUENCE_AUDIO, &hit))
        return end_stock(TK_SEQUENCE_AUDIO, &hit);
    ++tk_sequence_audio_hits;
    if (hit.audio.count)
        ++tk_sequence_audio_locked_hits;
    /* Native CANOPY dispatch still applies the stock AMP/filter/FX locks.
     * Its instrument event adapter discards stock key events while the
     * shared keyboard owns note generation. */
    return !tk_canopy_routed(hit.track);
}

int tk_fw_seq_midi_due(const uint32_t *event, int32_t channel)
{
    if (event[3] != 1 || event[0xd] != 1 || (event[4] & 0x81u) != 0x81u)
        return false;
    ++tk_sequence_midi_info[1];
    TkSequenceHit hit;
    if (!take(event, event[2], event[5], event[0xb], false, &hit))
        return false;
    ++tk_sequence_midi_info[2];
    if (channel < 0 || channel > 15 || !tk_sequence_wants_seed(hit.pattern, hit.track, hit.step))
        return end_stock(TK_SEQUENCE_MIDI, &hit);
    ++tk_sequence_midi_info[3];
    /* Controller locks are already applied. The downstream native worker
     * still applies the track and global MIDI transpositions to each pitch. */
    if (!decode(&hit, (const uint8_t *)(uintptr_t)event[5], (const uint8_t *)(uintptr_t)event[0xb],
                (int32_t)event[6], (const uint8_t *)(event + 9), true) ||
        !hit.count)
        return end_stock(TK_SEQUENCE_MIDI, &hit);
    ++tk_sequence_midi_info[4];
    if (!tk_sequence_offer(TK_SEQUENCE_MIDI, &hit))
        return end_stock(TK_SEQUENCE_MIDI, &hit);
    ++tk_sequence_midi_hits;
    return true;
}

void tk_sequence_poll(uint32_t pattern, bool running)
{
    uint32_t audio_solo = read32(tk_seq_audio_solo_lit);
    uint32_t midi_solo = read32(tk_seq_midi_solo_lit);
    uint16_t audio_muted = (uint16_t)(audio_solo ? ~audio_solo : read32(tk_seq_audio_mute_lit));
    uint16_t midi_muted = (uint16_t)(midi_solo ? ~midi_solo : read32(tk_seq_midi_mute_lit));
    uint16_t muted = (uint16_t)((audio_muted & ~midi_tracks) | (midi_muted & midi_tracks));
    uint32_t sr = dkrt_irq_mask();
    TkSequenceQueueEnds ends = tk_sequence_queue_ends();
    bool current = pp_transport_running() != 0;
    dkrt_irq_restore(sr);
    if (current == running)
        tk_sequence_drain_through(pattern, running, muted, tk_seq_length_table, ends);
}

void tk_sequence_set_midi(uint8_t track, bool midi)
{
    if (track >= TK_TRACKS)
        return;
    uint16_t bit = (uint16_t)(1u << track);
    if (midi)
        midi_tracks |= bit;
    else
        midi_tracks &= (uint16_t)~bit;
}

void tk_sequence_set_relay(uint8_t track, bool relay)
{
    if (track >= TK_TRACKS)
        return;
    uint16_t bit = (uint16_t)(1u << track);
    if (relay)
        relay_tracks |= bit;
    else
        relay_tracks &= (uint16_t)~bit;
}

/* optional: CANOPY's host (core/instrument-play) is not in every image */
extern int instrument_locked_sound(uint32_t index, uintptr_t expected, CpPreset *out,
                                   uint16_t *tune) __attribute__((weak));

bool tk_sequence_canopy_sound_fallback(uint32_t pattern, uint8_t track)
{
    if (!TK_HAVE(instrument_locked_sound) || !pattern || track >= TK_TRACKS)
        return false;
    const uint8_t *native = (const uint8_t *)(uintptr_t)pattern;
    const uint8_t *trig = native + TRACK_BYTES * track;
    for (uint8_t step = 0; step < TK_STEPS; ++step) {
        int8_t pool = (int8_t)trig[SOUND_LOCKS + step];
        if ((trig[2u * step + 1u] & 1u) && pool >= 0 &&
            !instrument_locked_sound((uint32_t)pool, 0, NULL, NULL))
            return true;
    }
    return false;
}
