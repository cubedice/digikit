#include "../src/track-capture.h"
#include "../src/track-sequence.h"

#include <assert.h>

static uint32_t blocks;
static TkPattern bank_a, bank_b;
static TkNoteEvent events[64];
static unsigned count;

uint32_t dkrt_blocks(void)
{
    return blocks;
}

uint32_t dkrt_irq_mask(void)
{
    return 0;
}

void dkrt_irq_restore(uint32_t sr)
{
    assert(sr == 0);
}

static bool output(void *unused, uint8_t track, const TkNoteEvent *event)
{
    (void)unused;
    assert(track == 0 && count < sizeof events / sizeof events[0]);
    events[count++] = *event;
    return true;
}

static void native_due(void)
{
    const uint32_t lengths[128] = {[0] = 5400000, [1] = 10800000};
    uint32_t id = 0x41800000;
    TkSequenceHit hit = {.pattern = id,
                         .track = 0,
                         .step = 4,
                         .count = 3,
                         .notes = {{60, 90, 1}, {64, 80, 0}, {67, 70, 127}},
                         .audio = {.sound = 1, .count = 1, .locks = {{4, 8}}}};
    tk_service_tick(0, 14400);
    assert(!tk_sequence_offer(TK_SEQUENCE_MIDI, &hit));
    tk_sequence_drain(id, true, 0, lengths);
    assert(!tk_sequence_wants_seed(id, 0, 4));
    assert(tk_trig_write(0, TK_CTL_POLY, true));
    assert(tk_trig_lock_write(0, 5, TK_CTL_POLY, false, false));
    tk_sequence_drain(id, true, 0, lengths);
    assert(tk_sequence_wants_seed(id, 0, 4));
    assert(!tk_sequence_wants_seed(id, 0, 5));
    assert(!tk_sequence_wants_seed(id + 1u, 0, 4));
    assert(tk_sequence_offer(TK_SEQUENCE_MIDI, &hit));
    tk_sequence_drain(id, true, 0, lengths);
    assert(count == 3 && events[0].pitch == 60 && events[0].velocity == 90);
    assert(events[1].pitch == 64 && events[2].pitch == 67);
    const TkSequenceHit automation = {.pattern = id,
                                      .track = 0,
                                      .step = 5,
                                      .parameters_only = true,
                                      .audio = {.count = 2, .locks = {{4, 10}, {5, 12}}}};
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &automation));
    tk_sequence_drain(id, true, 0, lengths);
    const TkSequenceAudio *audio = tk_sequence_audio(0);
    assert(count == 3 && audio && audio->sound == 1 && audio->count == 2);
    assert(audio->locks[0].value == 10 && audio->locks[1].value == 12);
    tk_service_tick(249999, 14400);
    assert(count == 3);
    tk_service_tick(1, 14400);
    assert(count == 4 && !events[3].on && events[3].pitch == 64);
    tk_sequence_drain(id, true, 1, lengths);
    assert(count == 6 && !events[4].on && !events[5].on);

    hit.step = 8;
    for (unsigned i = 0; i < 31; ++i)
        assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &hit));
    assert(!tk_sequence_offer(TK_SEQUENCE_AUDIO, &hit));
    tk_sequence_drain(id + 1u, true, 0, lengths);
    assert(count == 6); /* Queued outgoing hits cannot play the new pattern. */

    hit.pattern = id + 1u;
    assert(tk_sequence_offer(TK_SEQUENCE_MIDI, &hit));
    tk_sequence_drain(hit.pattern, true, 0, lengths);
    assert(count == 9);
    hit.count = 0;
    hit.step = 5; /* Step lock turns POLY off. Stock owns the new hit. */
    assert(tk_sequence_offer(TK_SEQUENCE_MIDI, &hit));
    tk_sequence_drain(hit.pattern, true, 0, lengths);
    assert(count == 12);
    tk_sequence_drain(0, false, 0, lengths);
    assert(!tk_sequence_wants_seed(hit.pattern, 0, 4));
}

static void arm_while_stopped(void)
{
    const uint32_t lengths[128] = {0};
    const uintptr_t selected = 0x41800000;
    TkSequenceHit first = {
        .pattern = selected, .track = 0, .step = 0, .count = 1, .notes = {{71, 95, 127}}};
    const unsigned before = count;
    tk_sequence_drain(selected, false, 0, lengths);
    assert(tk_sequence_wants_seed(selected, 0, 0));
    assert(!tk_sequence_wants_seed(selected, 0, 5)); /* Step lock still applies. */
    assert(!tk_sequence_wants_seed(selected + 1, 0, 0));

    /* The timer starts after this poll snapshots its stopped queue. It must
     * not discard the new native hit when its STOP drain runs afterward. */
    TkSequenceQueueEnds stopped = tk_sequence_queue_ends();
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &first));
    assert(count == before);
    tk_sequence_drain_through(selected, false, 0, lengths, stopped);
    assert(count == before);
    tk_sequence_drain(selected, true, 0, lengths);
    assert(count == before + 1 && events[before].on && events[before].pitch == 71);

    first.notes[0].pitch = 75;
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &first));
    tk_sequence_drain(selected, false, 0, lengths);
    assert(count == before + 2 && !events[before + 1].on && events[before + 1].pitch == 71);
    assert(tk_sequence_wants_seed(selected, 0, 0)); /* STOP also rearms PLAY. */
    tk_sequence_drain(selected, true, 0, lengths);
    assert(count == before + 2); /* The outgoing hit cannot start at the next PLAY. */
    tk_sequence_reset();
}

static void prepared_destination(void)
{
    const uint32_t lengths[128] = {0};
    const uintptr_t outgoing = 0x41800000, incoming = 0x4181db8c;
    static TkPattern destination;
    tk_pattern_init(&destination);
    destination.tracks[0].params.poly = true;
    const TkSeed recorded = {.count = 1, .notes = {{75, 94, 0, 0}}};
    assert(tk_pattern_set_seed(&destination, 0, 3, &recorded));

    TkSequenceHit a = {
        .pattern = outgoing, .track = 0, .step = 15, .count = 1, .notes = {{60, 90, 127}}};
    TkSequenceHit b = {
        .pattern = incoming, .track = 0, .step = 3, .count = 1, .notes = {{64, 91, 127}}};
    TkSequenceTarget target = {.address = incoming, .pattern = &destination, .blocked_tracks = 1};
    tk_service_play_pattern(&bank_a);
    tk_sequence_drain(outgoing, true, 0, lengths);
    tk_sequence_prepare(&target, 1);
    assert(!tk_sequence_wants_seed(incoming, 0, 3));
    target.blocked_tracks = 0;
    tk_sequence_prepare(&target, 1);
    assert(tk_sequence_wants_seed(outgoing, 0, 15));
    assert(tk_sequence_wants_seed(incoming, 0, 3));

    const unsigned before = count;
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &a));
    tk_sequence_drain(outgoing, true, 0, lengths);
    assert(count == before + 1 && events[before].on && events[before].pitch == 60);
    /* Native negative microtiming can put the destination note before the
     * master boundary. Its own recorded notes must already be effective. */
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &b));
    tk_sequence_drain(outgoing, true, 0, lengths);
    assert(count == before + 3 && !events[before + 1].on);
    assert(events[before + 2].on && events[before + 2].pitch == 75);
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &a));
    tk_service_play_pattern(&destination);
    tk_sequence_drain(incoming, true, 0, lengths);
    assert(count == before + 3); /* Neither a new note nor a cut at the boundary. */

    tk_sequence_reset();
    tk_service_play_pattern(&bank_a);
    tk_sequence_drain(outgoing, true, 0, lengths);
    tk_sequence_prepare(&target, 1);
    const unsigned before_direct = count;
    assert(tk_sequence_offer(TK_SEQUENCE_MIDI, &a));
    assert(tk_sequence_offer(TK_SEQUENCE_MIDI, &b));
    tk_service_play_pattern(&destination);
    tk_sequence_drain(incoming, true, 0, lengths);
    assert(count == before_direct + 1 && events[before_direct].pitch == 75);
    tk_sequence_reset();
    tk_service_play_pattern(&bank_a);
    destination.tracks[0].params.poly = 0;
    destination.tracks[0].params.arp = 1;
    destination.tracks[0].params.reset = TK_RESET_PATTERN;
    const TkSeed chord = {.count = 2, .notes = {{75, 94, 0, 0}, {79, 90, 0, 0}}};
    assert(tk_pattern_set_seed(&destination, 0, 3, &chord));
    tk_sequence_drain(outgoing, true, 0, lengths);
    tk_sequence_prepare(&target, 1);
    const unsigned before_arp = count;
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &b));
    tk_sequence_drain(outgoing, true, 0, lengths);
    assert(count == before_arp + 1 && events[before_arp].pitch == 75);
    tk_service_play_pattern(&destination);
    tk_service_boundary();
    tk_service_tick(125000, 14400);
    assert(count == before_arp + 3 && events[before_arp + 2].on &&
           events[before_arp + 2].pitch == 79);
    tk_sequence_reset();
    tk_service_play_pattern(NULL);
}

static void recorded_notes(void)
{
    const uint32_t id = 0x41800000;
    const uint32_t note_table = id + 0x6a50u;
    tk_capture_poll(id, 14400);
    blocks = 100;
    tk_capture_panel_press(0, 0, 128, 90, 1);
    tk_capture_note_commit(note_table, 0, 13, 67, 91);
    blocks = 115;
    assert(tk_capture_input_at(0, TK_USB, 2, 30, 60, 75, true, 115));
    tk_capture_note_commit(note_table, 0, 13, 60, 75);
    tk_capture_poll(id, 14400);
    const TkSeed *seed = tk_pattern_seed(&bank_a, 0, 13);
    assert(seed && seed->count == 2 && seed->notes[0].pitch == 67 && seed->notes[1].pitch == 60);
    assert(seed->notes[0].velocity == 91 && seed->notes[1].velocity == 75);
    assert(seed->notes[0].delay_us == 0 && seed->notes[1].delay_us == 10000);
    blocks = 175;
    tk_capture_panel_release(0);
    assert(tk_capture_input_at(0, TK_USB, 2, 30, 60, 0, true, 250));
    assert(seed->notes[0].gate_ticks == 2160000u && seed->notes[1].gate_ticks == 4320000u);
    TkParams settings;
    uint16_t locks;
    assert(tk_trig_write(0, TK_CTL_CHORD, TK_CHORD_MIN));
    assert(tk_trig_lock_display(0, 13, &settings, &locks));
    assert(settings.chord == TK_CHORD_PLAY);

    blocks = 300;
    assert(tk_capture_input(0, TK_DIN, 1, 18, 63, 89, false));
    tk_capture_note_commit(note_table, 0, 14, 63, 89);
    blocks = 350;
    assert(tk_capture_input(0, TK_DIN, 1, 19, 66, 90, false));
    tk_capture_note_commit(note_table, 0, 15, 66, 90);
    tk_capture_poll(id, 14400);
    assert(tk_pattern_seed(&bank_a, 0, 14)->notes[0].pitch == 63);
    assert(tk_pattern_seed(&bank_a, 0, 15)->notes[0].pitch == 66);
    assert(tk_trig_write(0, TK_CTL_CHORD, TK_CHORD_MAJ));
    tk_capture_shape_changed(0);
    assert(tk_trig_lock_display(0, 14, &settings, &locks));
    assert(settings.chord == TK_CHORD_MAJ);

    blocks = 400;
    assert(tk_capture_input(0, TK_DIN, 1, 20, 69, 93, false));
    tk_capture_note_commit(note_table, 0, 16, 69, 93);
    tk_service_edit_pattern(&bank_b);
    tk_capture_poll(id + 1u, 14400);
    assert(!tk_pattern_seed(&bank_a, 0, 16) && !tk_pattern_seed(&bank_b, 0, 16));

    assert(tk_trig_write(0, TK_CTL_POLY, true));
    for (uint8_t i = 0; i < TK_NOTES; ++i) {
        ++blocks;
        assert(tk_capture_input(0, TK_DIN, 1, i, 60 + i, 70 + i, false));
        tk_capture_note_commit(note_table + 1u, 0, 17, 60 + i, 70 + i);
    }
    tk_capture_poll(id + 1u, 14400);
    seed = tk_pattern_seed(&bank_b, 0, 17);
    assert(seed && seed->count == TK_NOTES && seed->notes[15].pitch == 75);
    uint32_t drops = tk_capture_dropped;
    assert(!tk_capture_input(0, TK_DIN, 1, 16, 76, 90, false));
    assert(tk_capture_dropped == drops + 1);
    assert(seed->count == TK_NOTES && seed->notes[15].velocity == 85);
}

static void full_recording_pool(void)
{
    tk_pattern_init(&bank_b);
    tk_service_edit_pattern(&bank_b);
    TkSeed note = {.count = 1, .notes = {{.pitch = 64, .velocity = 100}}};
    TkRecordedShape shape = {.chord = TK_CHORD_MAJ, .voicing = TK_VOICE_OPEN};
    for (uint8_t step = 0; step < TK_RECORDED_STEPS; ++step)
        assert(tk_service_store_edit_seed(0, step, &note, &shape));

    const uint8_t spare = TK_RECORDED_STEPS;
    assert(tk_trig_lock_write(0, spare, TK_CTL_RATE, 14, false));
    assert(!tk_service_store_edit_seed(0, spare, &note, &shape));
    TkParams params;
    uint16_t locks;
    assert(tk_trig_lock_display(0, spare, &params, &locks));
    assert(locks == (1u << TK_CTL_RATE) && params.rate == 14);
    assert(!tk_pattern_seed(&bank_b, 0, spare));

    const TkSeed *saved = tk_pattern_seed(&bank_b, 0, 0);
    assert(saved && saved->count == 1 && saved->notes[0].pitch == 64);
    note.count = TK_NOTES + 1;
    shape.chord = TK_CHORD_MIN;
    assert(!tk_service_store_edit_seed(0, 0, &note, &shape));
    assert(tk_trig_lock_display(0, 0, &params, &locks));
    assert(params.chord == TK_CHORD_MAJ);
    assert(saved->count == 1 && saved->notes[0].pitch == 64);

    note.count = 1;
    note.notes[0].pitch = 67;
    assert(tk_service_store_edit_seed(0, 0, &note, &shape));
    assert(saved->notes[0].pitch == 67);
}

int main(void)
{
    assert(tk_service_init());
    assert(tk_sequence_init());
    tk_pattern_init(&bank_a);
    tk_pattern_init(&bank_b);
    tk_service_edit_pattern(&bank_a);
    TkTrackBackend backend = {.output = output, .voices = 4};
    assert(tk_service_set_backend(0, &backend));
    native_due();
    arm_while_stopped();
    prepared_destination();
    recorded_notes();
    full_recording_pool();
    return 0;
}
