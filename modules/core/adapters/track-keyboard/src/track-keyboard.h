#ifndef DK_TRACK_KEYBOARD_H
#define DK_TRACK_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TK_NOTES 16
#define TK_INPUT_KEYS (2 * TK_NOTES)
#define TK_KEYS (TK_INPUT_KEYS + TK_NOTES)
#define TK_CANDIDATES (4 * TK_NOTES)
#define TK_TICKS_PER_QUARTER 21600000u

enum { TK_RATE_7Q = 17, TK_RATE_5Q };

enum TkOrder { TK_ORDER_PLAY, TK_ORDER_UP, TK_ORDER_DOWN, TK_ORDER_UPDN, TK_ORDER_RANDOM };
enum TkReset { TK_RESET_TRIG, TK_RESET_PATTERN, TK_RESET_CONTINUE };
enum TkChord {
    TK_CHORD_PLAY,
    TK_CHORD_MAJ,
    TK_CHORD_MAJ6,
    TK_CHORD_SUS2,
    TK_CHORD_MIN6,
    TK_CHORD_MIN,
    TK_CHORD_SUS4,
    TK_CHORD_ADD9,
    TK_CHORD_DOM7,
    TK_CHORD_DIM,
    TK_CHORD_MIN7,
    TK_CHORD_USER,
    TK_CHORD_MAJ7,
    TK_CHORD_AUG,
    TK_CHORD_COUNT
};
enum TkVoicing { TK_VOICE_CLOSE, TK_VOICE_OPEN, TK_VOICE_INV1, TK_VOICE_INV2 };
enum TkLane { TK_DIRECT, TK_ARP };
enum TkSource { TK_PANEL, TK_USB, TK_DIN, TK_SEQUENCER };

typedef struct {
    uint8_t arp, poly, order, gate, rate;
    int8_t octaves;
    uint8_t swing, reset, chord, voicing;
    uint8_t legacy_swing; /* CANOPY Sound's exact alternating interval deviation. */
    uint16_t strum_ms;
    uint8_t voices;
} TkParams;

typedef struct {
    int8_t interval;
    uint8_t velocity;
} TkRecipeNote;

typedef struct {
    uint8_t count;
    TkRecipeNote notes[TK_NOTES];
} TkRecipe;

typedef struct {
    uint8_t pitch, velocity;
    uint32_t delay_us;
    uint64_t gate_ticks;
} TkSeedNote;

typedef struct {
    uint8_t count;
    uint64_t length_ticks;
    TkSeedNote notes[TK_NOTES];
} TkSeed;

typedef struct {
    uint32_t token;
    uint8_t pitch, velocity, lane, on, lanes;
} TkNoteEvent;

/* Return false to decline an onset. Accept every release for any accepted onset.
 * Run all calls for one state in the same event task. Callbacks cannot re-enter it.
 */
typedef bool (*TkOutput)(void *context, const TkNoteEvent *event);

typedef struct {
    uint32_t id;
    uint8_t source, channel, pitch, velocity;
    bool down, stolen;
} TkKey;

typedef struct {
    uint8_t pitch, velocity;
} TkPitch;

typedef struct {
    TkNoteEvent note;
    uint64_t order;
    bool sent;
} TkVoice;

typedef struct {
    TkParams params;
    TkRecipe user;
    TkKey keys[TK_KEYS]; /* In physical press order, with pedal ownership. */
    uint16_t pedal[3];
    TkPitch direct[TK_NOTES], candidates[TK_CANDIDATES];
    TkVoice voices[TK_NOTES], arp_voice;
    uint64_t clock, time_us, next, arp_off, cycle, strum_start;
    uint64_t seed_start, seed_time_us;
    uint64_t voice_order;
    uint32_t serial, rng, remainder, interval_remainder;
    uint16_t bpm;
    uint8_t key_count, direct_count, candidate_count, cursor, last_pitch, strum_index;
    uint8_t voice_limit;
    bool playing, swung, have_last, have_cycle, blend, muted;
    TkSeed seed;
    uint16_t seed_started;
    TkOutput output;
    void *context;
} TkState;

TkParams tk_defaults(void);
bool tk_valid(const TkParams *params);
bool tk_valid_recipe(const TkRecipe *recipe);
bool tk_rate_division(uint8_t rate, uint8_t *numerator, uint8_t *denominator);
void tk_init(TkState *state, TkOutput output, void *context);
void tk_set_backend(TkState *state, uint8_t max_voices, bool mix_direct_and_arp);
bool tk_configure(TkState *state, const TkParams *params, const TkRecipe *user);
bool tk_press(TkState *state, uint8_t source, uint8_t channel, uint32_t id, int pitch,
              int velocity);
void tk_release(TkState *state, uint8_t source, uint8_t channel, uint32_t id);
void tk_pedal(TkState *state, uint8_t source, uint8_t channel, bool down);
void tk_clear_source(TkState *state, uint8_t source, uint8_t channel);
void tk_panic(TkState *state);
void tk_mute_output(TkState *state, bool muted);
/* One resolved trig. Length/gate zero means unlimited; delay is from its onset. */
bool tk_seed_valid(const TkSeed *seed);
bool tk_seed(TkState *state, const TkSeed *seed);
void tk_stop_seed(TkState *state);
void tk_stop_transport(TkState *state);
void tk_pattern_boundary(TkState *state);
/* bpm is in 1/120 BPM units. Zero or out of range retains the last valid value. */
bool tk_valid_tempo(uint16_t bpm);
void tk_tick(TkState *state, uint32_t elapsed_us, uint16_t bpm);

#endif
