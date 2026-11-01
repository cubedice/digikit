/* Bounded musical ownership above the physical MIDI FIFO. No I/O or allocation.
 * Caller serializes all access. A generation boundary discards performance,
 * never the saved templates; the physical ledger owns held-key tombstones. */
#ifndef DK_INSTRUMENT_KEYBOARD_H
#define DK_INSTRUMENT_KEYBOARD_H
#include "../../../midi-events/src/cf/ownership.h"
enum { KB_PLAY, KB_ROOT, KB_LEARN };
enum { KB_OK, KB_FULL, KB_RANGE, KB_EMPTY, KB_STALE, KB_EXHAUSTED };
typedef struct { uint8_t count, intervals[4]; } KbShape;
typedef struct {
    uint32_t token;
    uint8_t note, velocity, port, channel, held, pedal, latched, stolen;
} KbKey;
typedef struct { uint32_t source, token; uint8_t note, velocity, port, channel; } KbVoice;
typedef struct {
    uint32_t epoch, generation, next_token, pedals, session, revision;
    uint8_t mode, previous_mode, latch, previous_latch, type, voicing, selector_base, selector_channel;
    uint8_t slot, capture_count, capture[4], status, dirty, update;
    KbShape shapes[8];
    KbKey keys[MI_KEYS];
    KbVoice voices[4];
} KbState;
void kb_init(KbState *s);
void kb_reset(KbState *s, uint32_t epoch, uint32_t generation);
/* Inputs: zero to eight note events, or one controller. At most eight output
 * changes. Negative return requests a priority reset; never send partial output. */
int kb_process(KbState *s, const MiEvent *in, uint32_t count, MiEvent out[8]);
void kb_latch(KbState *s, int enabled);
int kb_learn_begin(KbState *s);
int kb_learn_save(KbState *s, uint32_t session, uint32_t revision);
void kb_learn_cancel(KbState *s);
/* Stable byte format, independent of CPU endian/padding. 8 templates, 48 bytes. */
#define KB_RECORD_BYTES 48
void kb_encode(const KbState *s, uint8_t bytes[KB_RECORD_BYTES]);
int kb_decode(KbState *s, const uint8_t *bytes, uint32_t length);
#endif
