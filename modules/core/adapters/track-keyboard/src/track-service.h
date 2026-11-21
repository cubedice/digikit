#ifndef DK_TRACK_SERVICE_H
#define DK_TRACK_SERVICE_H

#include "track-config.h"

/* Backend registration and event dispatch belong to one firmware task. MIDI and
 * sequencer interrupt adapters enqueue inputs for that task before calling it.
 */
typedef bool (*TkTrackOutput)(void *context, uint8_t track, const TkNoteEvent *event);

typedef struct {
    TkTrackOutput output;
    void *context;
    uint8_t voices;
    bool blend;
} TkTrackBackend;

/* The pattern adapter owns this storage and supplies a stable pointer for the
 * active edit pattern. Its save/copy hooks serialize before reusing it.
 */
#define TK_LOCKED_STEPS 128
typedef struct {
    uint16_t index; /* track * TK_STEPS + step + 1. Zero is unused. */
    TkStepLocks locks;
} TkPatternStep;

typedef struct {
    TkTrackConfig tracks[TK_TRACKS];
    TkPatternStep steps[TK_LOCKED_STEPS];
} TkPattern;

typedef struct {
    const TkPattern *pattern;
    uint32_t edit_generation;
    TkRecipe recipe;
    uint8_t track, step, first_pitch;
    bool open, learn, step_scope;
} TkNotesDraft;

void tk_pattern_init(TkPattern *pattern);
/* Ordered settings and step locks for a pattern save transaction. The firmware
 * adapter supplies project/pattern identity and owns the durable write.
 */
size_t tk_pattern_encoded_size(const TkPattern *pattern);
bool tk_pattern_encode(const TkPattern *pattern, uint8_t *data, size_t size);
bool tk_pattern_decode(TkPattern *pattern, const uint8_t *data, size_t size);
void tk_service_edit_pattern(TkPattern *pattern);
void tk_service_play_pattern(const TkPattern *pattern);
bool tk_service_set_backend(uint8_t track, const TkTrackBackend *backend);
bool tk_service_enabled(uint8_t track);
bool tk_service_press(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                      uint8_t velocity);
void tk_service_release(uint8_t track, uint8_t source, uint8_t channel, uint32_t id);
void tk_service_pedal(uint8_t track, uint8_t source, uint8_t channel, bool down);
bool tk_service_seed(uint8_t track, uint8_t step, const TkSeed *seed);
void tk_service_tick(uint32_t elapsed_us, uint16_t bpm);
void tk_service_boundary(void);
void tk_service_stop(void);

bool tk_trig_ready(uint8_t track);
uint8_t tk_trig_voice_capacity(uint8_t track);
bool tk_trig_display(uint8_t track, TkParams *out);
bool tk_trig_write(uint8_t track, enum TkControl control, int value);
bool tk_trig_lock_display(uint8_t track, uint8_t step, TkParams *out, uint16_t *lock_mask);
bool tk_trig_lock_write(uint8_t track, uint8_t step, enum TkControl control, int value, bool clear);

bool tk_notes_open(uint8_t track, uint8_t step, bool step_scope, TkNotesDraft *draft);
void tk_notes_learn(TkNotesDraft *draft);
bool tk_notes_append(TkNotesDraft *draft, uint8_t pitch, uint8_t velocity);
bool tk_notes_commit(TkNotesDraft *draft);

#endif
