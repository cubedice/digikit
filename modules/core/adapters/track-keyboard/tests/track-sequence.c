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
                         .notes = {{60, 90, 1}, {64, 80, 0}, {67, 70, 127}}};
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

    /* The timer delivers the first hit before the service observes PLAY. */
    assert(tk_sequence_offer(TK_SEQUENCE_AUDIO, &first));
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
    assert(!tk_capture_input(0, TK_DIN, 1, 16, 76, 90, false));
    assert(seed->count == TK_NOTES && seed->notes[15].velocity == 85);
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
    recorded_notes();
    return 0;
}
