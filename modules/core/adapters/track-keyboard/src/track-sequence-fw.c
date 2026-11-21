#include "track-sequence.h"
#include "track-capture.h"
#include "track-pool.h"

extern const uint32_t tk_seq_length_table[128];
extern const uint8_t tk_seq_audio_mute_lit[], tk_seq_audio_solo_lit[];
extern const uint8_t tk_seq_midi_mute_lit[], tk_seq_midi_solo_lit[];
extern const uint8_t tk_seq_record_ui_lit[];
extern volatile uint32_t pp_position[16];

enum {
    TRACK_BYTES = 0x6a5,
    NOTE_TABLE = 0x6a50,
    NOTE_COUNT = 8192,
    NOTE_NEXT = 0xd000,
    DEFAULT_VELOCITY = 0x480,
    DEFAULT_LENGTH = 0x481,
    DEFAULT_PITCH = 0x485,
    SOUND_LOCKS = 0x400,
    PARAMETER_LOCK_COUNTS = 0x1d360,
    STAGED_EVENTS = 257,
};

typedef struct {
    const uint32_t *volatile event;
    uintptr_t pattern;
    uint8_t track, step;
} Staged;

static Staged staged[STAGED_EVENTS];
static uint16_t midi_tracks;

volatile uint32_t tk_sequence_audio_hits, tk_sequence_audio_locked_hits, tk_sequence_midi_hits;
volatile uint32_t tk_sequence_midi_info[5]; /* scheduled, due, matched, enabled, decoded */

static void barrier(void)
{
    __asm__ volatile("" ::: "memory");
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

void tk_fw_seq_stage(const uint32_t *event, uint32_t track, uint32_t step, uintptr_t note_table)
{
    if (!event || track >= TK_TRACKS || step >= TK_STEPS || note_table < NOTE_TABLE)
        return;
    Staged *slot = slot_for(event);
    slot->event = NULL;
    barrier();
    slot->pattern = note_table - NOTE_TABLE;
    slot->track = (uint8_t)track;
    slot->step = (uint8_t)step;
    barrier();
    slot->event = event;
}

void tk_fw_seq_stage_midi(const uint32_t *event, uint32_t track, uint32_t step,
                          uintptr_t note_table)
{
    ++tk_sequence_midi_info[0];
    tk_fw_seq_stage(event, track, step, note_table);
}

static bool take(const uint32_t *event, uint32_t track, uintptr_t note_table, uintptr_t track_data,
                 TkSequenceHit *hit)
{
    Staged *slot = slot_for(event);
    if (slot->event != event)
        return false;
    barrier();
    uintptr_t pattern = slot->pattern;
    uint8_t step = slot->step;
    if (track != slot->track || pattern + NOTE_TABLE != note_table ||
        pattern + track * TRACK_BYTES != track_data || slot->event != event)
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

static bool end_stock(uint8_t source, TkSequenceHit *hit)
{
    hit->count = 0;
    tk_sequence_offer(source, hit);
    return false;
}

int tk_fw_seq_audio_due(const uint32_t *event)
{
    const uint32_t flags = event[0xe];
    if (event[0] || event[1] != 1 || event[5] != 1 || (flags & 0x81u) != 0x81u)
        return false;
    if (event[4] < TK_TRACKS && tk_pool_fw_reserved((uint8_t)event[4]))
        return true;
    TkSequenceHit hit;
    if (!take(event, event[4], event[7], event[0xb], &hit))
        return false;
    const uint8_t *track = (const uint8_t *)(uintptr_t)event[0xb];
    if (!tk_sequence_wants_seed(hit.pattern, hit.track, hit.step))
        return end_stock(TK_SEQUENCE_AUDIO, &hit);
    hit.audio.sound = event[0x10] ? event[0x10] : event[0xf];
    hit.audio.flags = flags & ~2u; /* The shared scheduler supplies retriggers. */
    if (!hit.audio.sound)
        return end_stock(TK_SEQUENCE_AUDIO, &hit);
    const uint32_t *locks = (const uint32_t *)(uintptr_t)event[0x15];
    if (locks) {
        uint32_t count = locks[2];
        if (count > TK_NATIVE_LOCKS_PER_HIT)
            return end_stock(TK_SEQUENCE_AUDIO, &hit);
        hit.audio.count = (uint8_t)count;
        const uint16_t *values = (const uint16_t *)(locks + 5);
        for (uint32_t i = 0; i < count; ++i, values += 4) {
            if (values[0] >= TK_NATIVE_LOCKS_PER_HIT)
                return end_stock(TK_SEQUENCE_AUDIO, &hit);
            hit.audio.locks[i] = (TkSequenceLock){.parameter = values[0], .value = values[1]};
        }
    }
    if (!decode(&hit, (const uint8_t *)(uintptr_t)event[7], track, (int32_t)event[8],
                (const uint8_t *)(event + 9), false) ||
        !hit.count || !tk_sequence_offer(TK_SEQUENCE_AUDIO, &hit))
        return end_stock(TK_SEQUENCE_AUDIO, &hit);
    ++tk_sequence_audio_hits;
    if (hit.audio.count)
        ++tk_sequence_audio_locked_hits;
    return true;
}

int tk_fw_seq_midi_due(const uint32_t *event, int32_t channel)
{
    if (event[3] != 1 || event[0xd] != 1 || (event[4] & 0x81u) != 0x81u)
        return false;
    ++tk_sequence_midi_info[1];
    TkSequenceHit hit;
    if (!take(event, event[2], event[5], event[0xb], &hit))
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
    tk_sequence_drain(pattern, running, muted, tk_seq_length_table);
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

bool tk_sequence_canopy_legacy_locks(uint32_t pattern, uint8_t track)
{
    if (!pattern || track >= TK_TRACKS)
        return false;
    const uint8_t *native = (const uint8_t *)(uintptr_t)pattern;
    const uint8_t *trig = native + TRACK_BYTES * track;
    const uint8_t *locks = native + PARAMETER_LOCK_COUNTS + TK_STEPS * track;
    for (uint8_t step = 0; step < TK_STEPS; ++step)
        if ((trig[2u * step + 1u] & 1u) && ((int8_t)trig[SOUND_LOCKS + step] >= 0 || locks[step]))
            return true;
    return false;
}
