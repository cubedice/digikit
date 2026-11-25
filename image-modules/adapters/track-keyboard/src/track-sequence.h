#ifndef DK_TRACK_SEQUENCE_H
#define DK_TRACK_SEQUENCE_H

#include "track-service.h"

typedef struct {
    uint32_t steps[TK_TRACKS][TK_STEPS / 32];
} TkSequencePolicy;

/* Snapshot track eligibility on the service task. */
void tk_service_sequence_policy(TkSequencePolicy *policy);
void tk_service_sequence_policy_for(TkSequencePolicy *policy, const TkPattern *pattern);
void tk_service_end_seed(uint8_t track);

enum { TK_SEQUENCE_TARGETS = 3 };

typedef struct {
    uintptr_t address;
    const TkPattern *pattern;
    uint16_t blocked_tracks;
} TkSequenceTarget;

typedef struct {
    uint32_t address, kit;
} TkSequenceNativeTarget;

enum { TK_SEQUENCE_AUDIO, TK_SEQUENCE_MIDI, TK_SEQUENCE_SOURCES };

typedef struct {
    uint8_t pitch, velocity, length;
} TkSequenceNote;

enum { TK_NATIVE_LOCKS_PER_HIT = 71 };

typedef struct {
    uint16_t parameter, value;
} TkSequenceLock;

typedef struct {
    uintptr_t sound;
    uint32_t flags;
    uint8_t count, pool_sound; /* Native Sound-lock index plus one. */
    TkSequenceLock locks[TK_NATIVE_LOCKS_PER_HIT];
} TkSequenceAudio;

typedef struct {
    uintptr_t pattern;
    uint8_t track, step, count;
    bool parameters_only;
    TkSequenceNote notes[TK_NOTES];
    TkSequenceAudio audio;
} TkSequenceHit;

typedef struct {
    uint8_t write[TK_SEQUENCE_SOURCES];
} TkSequenceQueueEnds;

/* These queues have one native producer each. The service task consumes them.
 * A zero-note hit ends the previous phrase while stock plays the new trig.
 */
bool tk_sequence_offer(uint8_t source, const TkSequenceHit *hit);
bool tk_sequence_wants_seed(uintptr_t pattern, uint8_t track, uint8_t step);
bool tk_sequence_init(void);
bool tk_sequence_fw_init(void);
void tk_sequence_reset(void);
void tk_sequence_prepare(const TkSequenceTarget *targets, unsigned count);
const TkSequenceAudio *tk_sequence_audio(uint8_t track);
void tk_sequence_drain(uintptr_t pattern, bool running, uint16_t muted,
                       const uint32_t lengths[128]);
/* Pair a queue boundary with the live transport before deciding to discard
 * stopped events. A START after this snapshot can publish the next hit. */
TkSequenceQueueEnds tk_sequence_queue_ends(void);
void tk_sequence_drain_through(uintptr_t pattern, bool running, uint16_t muted,
                               const uint32_t lengths[128], TkSequenceQueueEnds ends);

void tk_sequence_poll(uint32_t pattern, bool running);
void tk_sequence_native_targets(TkSequenceNativeTarget targets[TK_SEQUENCE_TARGETS]);
int tk_sequence_target_kind(uint32_t kit, uint8_t track);
void tk_sequence_set_midi(uint8_t track, bool midi);
void tk_sequence_set_relay(uint8_t track, bool relay);

/* Read the stock audio root or MIDI chord for the selected note trig. */
uint8_t tk_sequence_native_pitches(uint32_t pattern, uint8_t track, uint8_t step, bool midi,
                                   uint8_t pitches[4]);

#endif
