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

static void pattern_roundtrip(void)
{
    static TkPattern original, decoded;
    tk_pattern_init(&original);
    original.tracks[2].params.arp = 1;
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
    assert(decoded.tracks[2].params.arp == 1);
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

extern void track_native_checks(void);

int main(void)
{
    direct_notes();
    steal_and_strum();
    arp_timing();
    sequence_and_learned_shape();
    pattern_controls();
    service_ownership();
    pattern_roundtrip();
    track_native_checks();
}
