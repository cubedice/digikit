#include "../src/track-record.h"
#include "../src/track-keyboard.h"
#include "../src/track-config.h"
#include "../src/track-service.h"

#include <assert.h>
#include <string.h>

typedef struct {
    TkNoteEvent events[256];
    unsigned count;
} Events;

static bool sink(void *context, const TkNoteEvent *e)
{
    Events *captured = context;
    assert(captured->count < sizeof captured->events / sizeof captured->events[0]);
    captured->events[captured->count++] = *e;
    return true;
}

static void expect(const Events *e, unsigned i, bool on, unsigned pitch, unsigned lane)
{
    assert(i < e->count);
    assert(e->events[i].on == on);
    assert(e->events[i].pitch == pitch);
    assert(e->events[i].lane == lane);
}

static void direct_notes(void)
{
    Events e = {0};
    TkState s;
    tk_init(&s, sink, &e);
    tk_set_backend(&s, 3, false);
    TkParams p = tk_defaults();
    TkRecipe no_recipe = {0};
    p.poly = true;
    assert(tk_configure(&s, &p, &no_recipe));
    assert(tk_press(&s, TK_USB, 0, 60, 60, 105));
    assert(tk_press(&s, TK_DIN, 0, 60, 60, 97));
    assert(tk_press(&s, TK_PANEL, 0, 2, 64, 100));
    assert(tk_press(&s, TK_PANEL, 0, 3, 67, 91));
    assert(e.count == 3);
    expect(&e, 0, true, 60, TK_DIRECT);
    assert(e.events[0].velocity == 105);
    tk_release(&s, TK_PANEL, 0, 2);
    expect(&e, 3, false, 64, TK_DIRECT);
    tk_release(&s, TK_DIN, 0, 60);
    assert(e.count == 4);
    tk_pedal(&s, TK_USB, 0, true);
    tk_release(&s, TK_USB, 0, 60);
    assert(e.count == 4);
    tk_pedal(&s, TK_USB, 0, false);
    expect(&e, 4, false, 60, TK_DIRECT);
    assert(e.events[4].token == e.events[0].token);
    tk_panic(&s);
    expect(&e, 5, false, 67, TK_DIRECT);
    assert(e.count == 6);
}

static void steal_and_strum(void)
{
    Events e = {0};
    TkState s;
    tk_init(&s, sink, &e);
    tk_set_backend(&s, 3, false);
    TkParams p = tk_defaults();
    TkRecipe no_recipe = {0};
    p.poly = true;
    assert(tk_configure(&s, &p, &no_recipe));
    for (uint8_t i = 0; i < 4; ++i)
        assert(tk_press(&s, TK_USB, 0, i, 60 + i, 100));
    expect(&e, 3, false, 60, TK_DIRECT);
    expect(&e, 4, true, 63, TK_DIRECT);
    tk_release(&s, TK_USB, 0, 0);
    assert(e.count == 5);
    tk_panic(&s);
    e.count = 0;
    p.chord = TK_CHORD_MIN;
    p.strum_ms = 60;
    assert(tk_configure(&s, &p, &no_recipe));
    assert(tk_press(&s, TK_PANEL, 0, 0, 60, 100));
    expect(&e, 0, true, 60, TK_DIRECT);
    tk_tick(&s, 29999, 0);
    assert(e.count == 1);
    tk_tick(&s, 1, 0);
    expect(&e, 1, true, 63, TK_DIRECT);
    tk_release(&s, TK_PANEL, 0, 0);
    tk_tick(&s, 60000, 0);
    assert(e.count == 4 && !e.events[2].on && !e.events[3].on);
}

static void full_pitch_pool_from_two_ports(void)
{
    Events e = {0};
    TkState s;
    tk_init(&s, sink, &e);
    tk_set_backend(&s, TK_NOTES, false);
    TkParams p = tk_defaults();
    const TkRecipe recipe = {0};
    p.poly = true;
    p.voices = TK_NOTES;
    assert(tk_configure(&s, &p, &recipe));
    for (uint8_t i = 0; i < TK_NOTES; ++i) {
        assert(tk_press(&s, TK_USB, 0, i, 48 + i, 100));
        assert(tk_press(&s, TK_DIN, 0, i, 48 + i, 90));
    }
    assert(e.count == TK_NOTES && s.key_count == TK_INPUT_KEYS);
    assert(!tk_press(&s, TK_PANEL, 0, 0, 70, 100));
    for (uint8_t i = 0; i < TK_NOTES; ++i)
        tk_release(&s, TK_USB, 0, i);
    assert(e.count == TK_NOTES);
    for (uint8_t i = 0; i < TK_NOTES; ++i)
        tk_release(&s, TK_DIN, 0, i);
    assert(e.count == 2 * TK_NOTES);
}

static void arp_timing(void)
{
    Events e = {0};
    TkState s;
    tk_init(&s, sink, &e);
    TkParams p = tk_defaults();
    TkRecipe no_recipe = {0};
    p.arp = true;
    p.octaves = 1;
    p.gate = 100;
    assert(tk_configure(&s, &p, &no_recipe));
    assert(tk_press(&s, TK_PANEL, 0, 1, 64, 94));
    assert(tk_press(&s, TK_PANEL, 0, 2, 60, 84));
    assert(tk_press(&s, TK_PANEL, 0, 3, 67, 104));
    tk_tick(&s, 0, 0);
    assert(!e.count);
    tk_tick(&s, 0, 14400);
    tk_tick(&s, 124999, 0);
    assert(e.count == 1);
    for (unsigned i = 0; i < 6; ++i)
        tk_tick(&s, i == 0 ? 1 : 125000, 0);
    const uint8_t want[] = {64, 60, 67, 76, 72, 79, 64};
    for (unsigned i = 0; i < sizeof want; ++i) {
        expect(&e, 2 * i, true, want[i], TK_ARP);
        if (i > 0) {
            expect(&e, 2 * i - 1, false, want[i - 1], TK_ARP);
            assert(e.events[2 * i - 1].token == e.events[2 * i - 2].token);
        }
    }
    tk_tick(&s, 15 * 125000, 0);
    assert(e.count == 15);
    tk_clear_source(&s, TK_PANEL, 0);
    assert(e.count == 16);
    tk_tick(&s, 1200000, 0);
    assert(e.count == 16);
}

static void sequence_and_learned_shape(void)
{
    Events e = {0};
    TkState s;
    tk_init(&s, sink, &e);
    tk_set_backend(&s, 4, false);
    TkParams p = tk_defaults();
    TkRecipe shape = {.count = 3, .notes = {{0, 100}, {-4, 98}, {3, 96}}};
    p.poly = true;
    p.chord = TK_CHORD_USER;
    assert(tk_configure(&s, &p, &shape));
    TkSeed seed = {.count = 1, .length_ticks = TK_TICKS_PER_QUARTER, .notes = {{62, 80, 0, 0}}};
    assert(tk_seed(&s, &seed));
    expect(&e, 0, true, 62, TK_DIRECT);
    expect(&e, 1, true, 58, TK_DIRECT);
    expect(&e, 2, true, 65, TK_DIRECT);
    tk_tick(&s, 499999, 14400);
    assert(e.count == 3);
    tk_tick(&s, 1, 0);
    assert(e.count == 6);
    assert(tk_seed(&s, &seed));
    assert(tk_seed(&s, &seed));
    assert(e.count == 15);
    tk_stop_transport(&s);
    assert(e.count == 18);
}

static void pattern_controls(void)
{
    TkTrackConfig track = {.params = tk_defaults()};
    assert(tk_set_control(&track.params, TK_CTL_OCTAVES, -3));
    assert(!tk_set_control(&track.params, TK_CTL_OCTAVES, -4));
    assert(tk_set_control(&track.params, TK_CTL_STRUM, 500));
    assert(!tk_set_control(&track.params, TK_CTL_STRUM, 501));
    assert(tk_assign_helpers(&track, 0, 0x600, 0x601, 0x600, 0));
    assert(!tk_assign_helpers(&track, 0, 0x601, 0x601, 0x601, 0));
    assert(!tk_assign_helpers(&track, 0, 0x200, 0x601, 0x600, 0x200));
    assert(track.helpers == 0x600);
    track.user.count = TK_NOTES;
    track.user.notes[0] = (TkRecipeNote){-127, 127};
    track.user.notes[TK_NOTES - 1] = (TkRecipeNote){127, 90};
    uint8_t bytes[TK_CONFIG_BYTES];
    assert(tk_config_encode(&track, bytes, sizeof bytes));
    TkTrackConfig restored = {0};
    assert(tk_config_decode(&restored, bytes, sizeof bytes));
    assert(restored.params.octaves == -3 && restored.params.strum_ms == 500);
    assert(restored.helpers == 0x600);
    assert(restored.user.notes[0].interval == -127 && restored.user.notes[0].velocity == 127);
    assert(restored.user.notes[TK_NOTES - 1].interval == 127);
    bytes[4] = 255;
    assert(!tk_config_decode(&restored, bytes, sizeof bytes));
    assert(restored.params.strum_ms == 500);
    assert(!tk_config_decode(&restored, bytes, sizeof bytes - 1));
    TkStepLocks locks = {0};
    assert(tk_set_lock(&locks, TK_CTL_CHORD, TK_CHORD_USER));
    assert(!tk_set_lock(&locks, TK_CTL_VOICES, 3));
    locks.mask |= TK_RECIPE_OVERRIDE;
    locks.user = (TkRecipe){.count = 1, .notes = {{7, 95}}};
    TkParams effective;
    TkRecipe recipe;
    assert(tk_resolve_step(&track, &locks, &effective, &recipe));
    assert(effective.chord == TK_CHORD_USER && recipe.count == 1 && recipe.notes[0].interval == 7);
    tk_clear_lock(&locks, TK_CTL_CHORD);
    assert(tk_resolve_step(&track, &locks, &effective, &recipe));
    assert(effective.chord == TK_CHORD_PLAY && track.user.count == TK_NOTES);
}

static bool track_sink(void *context, uint8_t track, const TkNoteEvent *e)
{
    assert(track == 0);
    return sink(context, e);
}

static void service_ownership(void)
{
    TkPattern pattern;
    Events e = {0};
    tk_pattern_init(&pattern);
    tk_service_edit_pattern(&pattern);
    assert(!tk_trig_ready(0));
    TkTrackBackend backend = {.output = track_sink, .context = &e, .voices = 4};
    assert(tk_service_set_backend(0, &backend));
    assert(tk_trig_ready(0));
    assert(tk_trig_write(0, TK_CTL_ARP, 1));
    assert(tk_trig_lock_write(0, 5, TK_CTL_GATE, 100, false));
    TkParams p;
    uint16_t locks;
    assert(tk_trig_lock_display(0, 5, &p, &locks));
    assert(p.gate == 100 && locks == (1u << TK_CTL_GATE));
    assert(tk_trig_display(0, &p) && p.gate == 50);
    TkSeed seed = {.count = 3, .notes = {{60, 100, 0, 0}, {64, 90, 0, 0}, {67, 95, 0, 0}}};
    tk_service_play_pattern(&pattern);
    assert(tk_service_seed(0, 5, &seed));
    tk_service_tick(0, 14400);
    expect(&e, 0, true, 60, TK_ARP);
    assert(tk_service_press(0, TK_PANEL, 0, 1, 71, 100));
    tk_service_tick(0, 0);
    expect(&e, 1, false, 60, TK_ARP);
    expect(&e, 2, true, 71, TK_ARP);
    tk_service_tick(125000, 0);
    tk_service_release(0, TK_PANEL, 0, 1);
    unsigned resume = e.count;
    tk_service_tick(125000, 0);
    expect(&e, resume, true, 67, TK_ARP); /* The overridden E still advanced the arp. */
    assert(e.events[0].token & 0x80000000u);
    assert(!(e.events[2].token & 0x80000000u));

    TkNotesDraft draft;
    assert(tk_notes_open(0, 5, true, &draft));
    tk_notes_learn(&draft);
    assert(tk_notes_append(&draft, 64, 90));
    assert(tk_notes_append(&draft, 60, 84));
    assert(tk_notes_commit(&draft));
    assert(tk_trig_lock_display(0, 5, &p, &locks));
    assert(p.chord == TK_CHORD_USER && (locks & TK_RECIPE_OVERRIDE));
    assert(tk_notes_open(0, 5, true, &draft) && draft.recipe.notes[1].interval == -4);
    tk_service_edit_pattern(&pattern);
    assert(!tk_notes_commit(&draft));

    tk_service_stop();
    tk_service_edit_pattern(NULL);
    assert(tk_service_set_backend(0, NULL));
}

static void selector_retains_common_pitches(void)
{
    static TkPattern pattern;
    Events e = {0};
    tk_pattern_init(&pattern);
    tk_service_edit_pattern(&pattern);
    TkTrackBackend backend = {.output = track_sink, .context = &e, .voices = 4};
    assert(tk_service_set_backend(0, &backend));
    assert(tk_trig_write(0, TK_CTL_POLY, 1));
    assert(tk_trig_write(0, TK_CTL_CHORD, TK_CHORD_MAJ));
    assert(tk_service_write_selector(0, 3, -1));
    int8_t octave, channel;
    assert(tk_service_selector(0, true, &octave, &channel) && octave == 3 && channel == -1);
    assert(tk_service_press(0, TK_USB, 0, 60, 60, 96));
    expect(&e, 0, true, 60, TK_DIRECT);
    expect(&e, 1, true, 64, TK_DIRECT);
    expect(&e, 2, true, 67, TK_DIRECT);
    tk_service_select_chord(0, TK_CHORD_MIN);
    expect(&e, 3, false, 64, TK_DIRECT);
    expect(&e, 4, true, 63, TK_DIRECT);
    assert(e.count == 5 && pattern.tracks[0].params.chord == TK_CHORD_MIN);
    tk_service_release(0, TK_USB, 0, 60);
    assert(e.count == 8);
    assert(tk_trig_write(0, TK_CTL_CHORD, TK_CHORD_PLAY));
    assert(tk_service_selector(0, true, &octave, &channel) && octave == -1 && channel == -1);
    assert(tk_service_selector(0, false, &octave, &channel) && octave == 3 && channel == -1);
    tk_service_edit_pattern(NULL);
    assert(tk_service_set_backend(0, NULL));
}

static void pattern_switch_keeps_held_keys(void)
{
    static TkPattern first, second, bypass;
    tk_pattern_init(&first);
    tk_pattern_init(&second);
    tk_pattern_init(&bypass);
    first.tracks[0].params.poly = 1;
    second.tracks[0].params.poly = 1;
    Events e = {0};
    const TkTrackBackend backend = {.output = track_sink, .context = &e, .voices = 4};
    tk_service_edit_pattern(&first);
    assert(tk_service_set_backend(0, &backend));
    tk_service_play_pattern(&first);
    assert(tk_service_press(0, TK_USB, 0, 60, 60, 100));
    expect(&e, 0, true, 60, TK_DIRECT);
    tk_service_play_pattern(&second);
    assert(e.count == 1);
    tk_service_play_pattern(&bypass);
    expect(&e, 1, false, 60, TK_DIRECT);
    tk_service_play_pattern(&second);
    expect(&e, 2, true, 60, TK_DIRECT);
    tk_service_release(0, TK_USB, 0, 60);
    expect(&e, 3, false, 60, TK_DIRECT);
    tk_service_stop();
    tk_service_edit_pattern(NULL);
    assert(tk_service_set_backend(0, NULL));
}

static void pattern_roundtrip(void)
{
    static TkPattern original, decoded;
    tk_pattern_init(&original);
    original.tracks[2].params.arp = 1;
    original.tracks[2].params.rate = TK_RATE_5Q;
    original.tracks[2].selector_channel = 15;
    original.tracks[2].canopy_mix = 76;
    original.tracks[2].override = true;
    original.seeds[8] = (TkPatternSeed){.index = 300,
                                        .seed = {.count = 3,
                                                 .notes = {{60, 102, 0, UINT64_C(32098765000)},
                                                           {67, 64, 231, 0},
                                                           {64, 111, 26043, UINT64_C(123456789)}}}};
    original.steps[0].index = 300;
    assert(tk_set_lock(&original.steps[0].locks, TK_CTL_CHORD, TK_CHORD_USER));
    original.steps[0].locks.mask |= TK_RECIPE_OVERRIDE;
    original.steps[0].locks.user = (TkRecipe){.count = 2, .notes = {{0, 71}, {-8, 111}}};
    original.steps[59].index = TK_TRACKS * TK_STEPS;
    assert(tk_set_lock(&original.steps[59].locks, TK_CTL_OCTAVES, -2));
    uint8_t data[16 + TK_TRACKS * TK_CONFIG_BYTES + 2 * 64];
    size_t size = tk_pattern_encoded_size(&original);
    assert(size <= sizeof data && tk_pattern_encode(&original, data, size));
    assert(tk_pattern_decode(&decoded, data, size));
    assert(decoded.tracks[2].params.arp == 1 && decoded.tracks[2].params.rate == TK_RATE_5Q);
    assert(decoded.tracks[2].selector_channel == 15 && decoded.tracks[2].override);
    assert(decoded.tracks[2].canopy_mix == 76 && decoded.tracks[0].selector_octave == -1);
    assert(decoded.seeds[0].seed.notes[2].pitch == 64 &&
           decoded.seeds[0].seed.notes[2].delay_us == 26043 &&
           decoded.seeds[0].seed.notes[0].gate_ticks == UINT64_C(32098765000));
    assert(decoded.steps[1].index == original.steps[59].index);
    assert(decoded.steps[1].locks.values[TK_CTL_OCTAVES] == -2);
    assert(decoded.steps[0].locks.user.notes[1].interval == -8);
    data[size - 14] ^= 1;
    assert(!tk_pattern_decode(&decoded, data, size));
    assert(decoded.steps[1].locks.values[TK_CTL_OCTAVES] == -2);
    original.steps[59].index = 300;
    assert(!tk_pattern_encode(&original, data, size));
    assert(!tk_pattern_decode(&decoded, data, size));
}

static void copy_track_attachment(void)
{
    static TkPattern source, target;
    tk_pattern_init(&source);
    tk_pattern_init(&target);
    source.tracks[4].params.arp = 1;
    source.tracks[4].selector_octave = 3;
    source.steps[1].index = 4 * TK_STEPS + 128;
    assert(tk_set_lock(&source.steps[1].locks, TK_CTL_CHORD, TK_CHORD_MIN));
    const TkSeed recipe = {.count = 2, .notes = {{60, 99, 0, 1000}, {63, 91, 100, 2000}}};
    assert(tk_pattern_set_seed(&source, 4, 0, &recipe));
    target.tracks[0].params.poly = 1;
    target.steps[7].index = 2 * TK_STEPS + 1;
    assert(tk_set_lock(&target.steps[7].locks, TK_CTL_GATE, 62));
    assert(tk_pattern_has_track(&source, 4));
    assert(tk_pattern_copy_track(&target, 2, &source, 4));
    assert(target.tracks[0].params.poly == 1 && target.tracks[2].params.arp == 1);
    assert(target.tracks[2].selector_octave == 3);
    assert(target.steps[0].index == 2 * TK_STEPS + 128);
    assert(target.steps[0].locks.values[TK_CTL_CHORD] == TK_CHORD_MIN);
    assert(target.steps[7].index == 0);
    assert(tk_pattern_seed(&target, 2, 0)->notes[0].velocity == 99);
    assert(tk_pattern_copy_track(&target, 1, &target, 2));
    assert(tk_pattern_has_track(&target, 1) && tk_pattern_has_track(&target, 2));
    assert(tk_pattern_seed(&target, 1, 0)->notes[1].delay_us == 100);
    assert(tk_pattern_copy_track(&target, 2, NULL, 0));
    assert(!tk_pattern_has_track(&target, 2) && tk_pattern_has_track(&target, 1));

    /* An unavailable lock slot refuses the transfer before altering tracks. */
    for (unsigned i = 0; i < TK_LOCKED_STEPS; ++i)
        target.steps[i].index = (uint16_t)(i + 1);
    assert(!tk_pattern_copy_track(&target, 2, &source, 4));
    assert(!tk_pattern_has_track(&target, 2) && target.tracks[0].params.poly == 1);
}

static void copy_step_attachment(void)
{
    static TkPattern pattern;
    tk_pattern_init(&pattern);
    TkStepAttachment source = {
        .seed = {.count = 2, .notes = {{60, 99, 0, 1000}, {65, 83, 27, 3000}}}};
    assert(tk_set_lock(&source.locks, TK_CTL_CHORD, TK_CHORD_MIN));
    assert(tk_pattern_set_attachment(&pattern, 1, 127, &source));
    TkStepAttachment copy;
    assert(tk_pattern_step_attachment(&pattern, 1, 127, &copy));
    assert(tk_pattern_set_attachment(&pattern, 4, 0, &copy));
    assert(tk_pattern_set_attachment(&pattern, 1, 127, NULL));
    assert(!tk_pattern_step_attachment(&pattern, 1, 127, &copy));
    assert(tk_pattern_step_attachment(&pattern, 4, 0, &copy));
    assert(copy.locks.values[TK_CTL_CHORD] == TK_CHORD_MIN);
    assert(copy.seed.notes[1].pitch == 65 && copy.seed.notes[1].gate_ticks == 3000);

    /* The lock and seed move together, so a full seed array keeps the old lock. */
    TkStepAttachment previous = {0};
    assert(tk_set_lock(&previous.locks, TK_CTL_GATE, 70));
    assert(tk_pattern_set_attachment(&pattern, 5, 6, &previous));
    for (unsigned i = 0; i < TK_RECORDED_STEPS; ++i)
        pattern.seeds[i].index = (uint16_t)(i + 1);
    assert(!tk_pattern_set_attachment(&pattern, 5, 6, &source));
    assert(tk_pattern_step_attachment(&pattern, 5, 6, &copy));
    assert(!copy.seed.count && copy.locks.values[TK_CTL_GATE] == 70);

    /* A later target can free the slot an earlier destination needs. */
    const int32_t moving[] = {50, 0};
    const TkStepAttachment empty = {0};
    const TkStepAttachment values[] = {source, empty};
    bool changed;
    assert(tk_pattern_attach_batch(&pattern, 0, moving, 2, values, false, &changed) ==
           TK_ATTACH_OK);
    assert(changed && !tk_pattern_seed(&pattern, 0, 50));
    assert(tk_pattern_attach_batch(&pattern, 0, moving, 2, values, true, &changed) == TK_ATTACH_OK);
    assert(!tk_pattern_seed(&pattern, 0, 0));
    assert(tk_pattern_seed(&pattern, 0, 50)->notes[1].gate_ticks == 3000);

    const int32_t overlapping[] = {50, 50};
    assert(tk_pattern_attach_batch(&pattern, 0, overlapping, 2, values, true, &changed) ==
           TK_ATTACH_INVALID);
    assert(tk_pattern_seed(&pattern, 0, 50)->notes[1].gate_ticks == 3000);
}

static void native_record_roundtrip(void)
{
    static uint8_t record[TK_NATIVE_RECORD_BYTES];
    enum { SIZE = TK_NATIVE_TAIL_PAYLOAD + 329 };
    uint8_t bytes[SIZE], decoded[SIZE];
    for (unsigned i = 0; i < SIZE; ++i)
        bytes[i] = (uint8_t)(i * 11u);
    uint8_t *native_locks = record + TK_NATIVE_LOCKS_OFFSET;
    for (unsigned slot = 0; slot < TK_NATIVE_LOCKS; ++slot)
        memset(native_locks + slot * TK_NATIVE_LOCK_STRIDE, 0x11, TK_NATIVE_LOCK_STRIDE);
    const unsigned free_slots[] = {10, 74};
    native_locks[free_slots[0] * TK_NATIVE_LOCK_STRIDE] = 0xff;
    assert(!tk_record_write(record, bytes, SIZE));
    assert(tk_record_read(record, decoded, sizeof decoded) == 0);
    for (unsigned f = 0; f < 2; ++f)
        memset(native_locks + free_slots[f] * TK_NATIVE_LOCK_STRIDE, 0xff, 2);
    assert(tk_record_write(record, bytes, SIZE));
    assert(tk_record_read(record, decoded, sizeof decoded) == SIZE);
    assert(memcmp(bytes, decoded, SIZE) == 0);
    assert(tk_record_read(record, decoded, sizeof decoded - 1) == 0);
    for (unsigned slot = 0; slot < TK_NATIVE_LOCKS; ++slot)
        if (slot != free_slots[0] && slot != free_slots[1])
            for (unsigned offset = 0; offset < TK_NATIVE_LOCK_STRIDE; ++offset)
                assert(native_locks[slot * TK_NATIVE_LOCK_STRIDE + offset] == 0x11);
    assert(tk_record_write(record, NULL, 0));
    assert(tk_record_read(record, decoded, sizeof decoded) == 0);
    record[TK_NATIVE_TAIL_OFFSET] = 0x55;
    assert(!tk_record_write(record, bytes, SIZE));
    assert(record[TK_NATIVE_TAIL_OFFSET] == 0x55);
}

extern void track_native_checks(void);
extern void track_pool_checks(void);

int main(void)
{
    direct_notes();
    full_pitch_pool_from_two_ports();
    steal_and_strum();
    arp_timing();
    sequence_and_learned_shape();
    pattern_controls();
    service_ownership();
    selector_retains_common_pitches();
    pattern_switch_keeps_held_keys();
    pattern_roundtrip();
    copy_track_attachment();
    copy_step_attachment();
    native_record_roundtrip();
    track_native_checks();
    track_pool_checks();
}
