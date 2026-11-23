#ifndef DK_TRACK_SERVICE_H
#define DK_TRACK_SERVICE_H

#include "track-config.h"

/* Backend registration and event dispatch belong to one firmware task. MIDI and
 * sequencer interrupt adapters enqueue inputs for that task before calling it.
 */
typedef bool (*TkTrackOutput)(void *context, uint8_t track, const TkNoteEvent *event);

typedef bool (*TkTrackAvailable)(uint8_t track);
typedef bool (*TkTrackInherit)(uint8_t track, TkTrackConfig *config);
typedef bool (*TkTrackPrepare)(uint8_t track, TkParams *params, TkRecipe *recipe, TkSeed *seed,
                               bool recorded, uint16_t *pinned_controls);

typedef struct {
    TkTrackOutput output;
    void *context;
    uint8_t voices;
    bool blend;
    TkTrackInherit inherit;
    TkTrackAvailable available;
    TkTrackPrepare prepare_seed;
    bool (*seed_waiting)(uint8_t track); /* A direct sequenced onset is waiting for a helper. */
} TkTrackBackend;

/* The pattern adapter owns this storage and supplies a stable pointer for the
 * active edit pattern. Its save/copy hooks serialize before reusing it.
 */
#define TK_LOCKED_STEPS 128
#define TK_RECORDED_STEPS 32
#define TK_PATTERN_HEADER 16
#define TK_PATTERN_BYTES                                                                           \
    (TK_PATTERN_HEADER + TK_TRACKS * TK_CONFIG_BYTES + TK_LOCKED_STEPS * 64 +                      \
     TK_RECORDED_STEPS * (3 + TK_NOTES * 17))
typedef struct {
    uint16_t index; /* track * TK_STEPS + step + 1. Zero is unused. */
    TkStepLocks locks;
} TkPatternStep;

typedef struct {
    uint16_t index; /* Same step identity as TkPatternStep. */
    TkSeed seed;    /* Phrase length is supplied by the native trig. */
} TkPatternSeed;

typedef struct {
    TkStepLocks locks;
    TkSeed seed;
} TkStepAttachment;

enum TkAttachmentResult { TK_ATTACH_OK, TK_ATTACH_INVALID, TK_ATTACH_FULL };

typedef struct {
    uint8_t chord, voicing;
    uint16_t strum_ms;
    TkRecipe user;
} TkRecordedShape;

typedef struct {
    TkTrackConfig tracks[TK_TRACKS];
    TkPatternStep steps[TK_LOCKED_STEPS];
    TkPatternSeed seeds[TK_RECORDED_STEPS];
} TkPattern;

typedef struct {
    const TkPattern *pattern;
    uint32_t edit_generation;
    TkRecipe recipe;
    uint8_t track, step, first_pitch;
    bool open, learn, step_scope, clear;
} TkNotesDraft;

void tk_pattern_init(TkPattern *pattern);
bool tk_pattern_has_track(const TkPattern *pattern, uint8_t track);
bool tk_pattern_copy_track(TkPattern *destination, uint8_t to, const TkPattern *source,
                           uint8_t from);
bool tk_pattern_step_attachment(const TkPattern *pattern, uint8_t track, uint8_t step,
                                TkStepAttachment *out);
bool tk_pattern_set_attachment(TkPattern *pattern, uint8_t track, uint8_t step,
                               const TkStepAttachment *attachment);
enum TkAttachmentResult tk_pattern_attach_batch(TkPattern *pattern, uint8_t track,
                                                const int32_t *steps, uint8_t count,
                                                const TkStepAttachment *attachments, bool commit,
                                                bool *changed);
const TkSeed *tk_pattern_seed(const TkPattern *pattern, uint8_t track, uint8_t step);
bool tk_pattern_set_seed(TkPattern *pattern, uint8_t track, uint8_t step, const TkSeed *seed);
bool tk_service_store_edit_seed(uint8_t track, uint8_t step, const TkSeed *seed,
                                const TkRecordedShape *shape);
bool tk_service_edit_shape(uint8_t track, TkRecordedShape *shape);
/* Ordered settings and step locks for a pattern save transaction. The firmware
 * adapter supplies project/pattern identity and owns the durable write.
 */
size_t tk_pattern_encoded_size(const TkPattern *pattern);
bool tk_pattern_encode(const TkPattern *pattern, uint8_t *data, size_t size);
bool tk_pattern_decode(TkPattern *pattern, const uint8_t *data, size_t size);
bool tk_service_init(void);
void tk_service_edit_pattern(TkPattern *pattern);
void tk_service_play_pattern(const TkPattern *pattern);
void tk_service_set_voice_capacity(uint8_t track, uint8_t voices);
uint16_t tk_service_helpers(uint8_t track);
bool tk_service_assign_helpers(uint8_t track, uint16_t request, uint16_t selectable);
bool tk_service_set_backend(uint8_t track, const TkTrackBackend *backend);
const TkTrackConfig *tk_service_active_config(uint8_t track);
bool tk_service_enabled(uint8_t track);
bool tk_service_press(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                      uint8_t velocity);
void tk_service_release(uint8_t track, uint8_t source, uint8_t channel, uint32_t id);
void tk_service_pedal(uint8_t track, uint8_t source, uint8_t channel, bool down);
void tk_service_cancel_track(uint8_t track);
void tk_service_clear_source(uint8_t track, uint8_t source, uint8_t channel);
bool tk_service_selector(uint8_t track, bool active, int8_t *octave, int8_t *channel);
bool tk_service_write_selector(uint8_t track, int8_t octave, int8_t channel);
void tk_service_select_chord(uint8_t track, enum TkChord chord);
bool tk_service_seed(uint8_t track, uint8_t step, const TkSeed *seed);
bool tk_service_seed_for(const TkPattern *pattern, uint8_t track, uint8_t step, const TkSeed *seed);
void tk_service_tick(uint32_t elapsed_us, uint16_t bpm);
void tk_service_manual_retrig(uint8_t track, bool held);
void tk_service_boundary(void);
void tk_service_stop(void);

bool tk_trig_ready(uint8_t track);
uint8_t tk_trig_voice_capacity(uint8_t track);
int tk_trig_arp_pitch(uint8_t track, uint32_t *token);
uint8_t tk_trig_held_pitches(uint8_t track, uint8_t pitches[TK_NOTES]);
bool tk_trig_display(uint8_t track, TkParams *out);
bool tk_trig_write(uint8_t track, enum TkControl control, int value);
bool tk_trig_lock_display(uint8_t track, uint8_t step, TkParams *out, uint16_t *lock_mask);
bool tk_trig_lock_write(uint8_t track, uint8_t step, enum TkControl control, int value, bool clear);
bool tk_trig_lock_batch(uint8_t track, const uint32_t steps[4], enum TkControl control, int value,
                        bool clear);

bool tk_notes_open(uint8_t track, uint8_t step, bool step_scope, TkNotesDraft *draft);
void tk_notes_learn(TkNotesDraft *draft);
bool tk_notes_append(TkNotesDraft *draft, uint8_t pitch, uint8_t velocity);
bool tk_notes_commit(TkNotesDraft *draft);

#endif
