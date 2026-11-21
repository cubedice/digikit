#ifndef DK_TRACK_SEQUENCE_H
#define DK_TRACK_SEQUENCE_H

#include "track-service.h"

typedef struct {
    uint32_t steps[TK_TRACKS][TK_STEPS / 32];
} TkSequencePolicy;

/* Snapshot the active playing settings from the service task. */
void tk_service_sequence_policy(TkSequencePolicy *policy);
void tk_service_end_seed(uint8_t track);

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
    uint8_t count;
    TkSequenceLock locks[TK_NATIVE_LOCKS_PER_HIT];
} TkSequenceAudio;

typedef struct {
    uintptr_t pattern;
    uint8_t track, step, count;
    TkSequenceNote notes[TK_NOTES];
    TkSequenceAudio audio;
} TkSequenceHit;

/* These queues have one native producer each. The service task consumes them.
 * A zero-note hit ends the previous phrase while stock plays the new trig.
 */
bool tk_sequence_offer(uint8_t source, const TkSequenceHit *hit);
bool tk_sequence_wants_seed(uintptr_t pattern, uint8_t track, uint8_t step);
bool tk_sequence_init(void);
void tk_sequence_reset(void);
const TkSequenceAudio *tk_sequence_audio(uint8_t track);
void tk_sequence_drain(uintptr_t pattern, bool running, uint16_t muted,
                       const uint32_t lengths[128]);

void tk_sequence_poll(uint32_t pattern, bool running);
void tk_sequence_set_midi(uint8_t track, bool midi);
bool tk_sequence_canopy_legacy_locks(uint32_t pattern, uint8_t track);

#endif
