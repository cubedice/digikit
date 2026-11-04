/* Bounded CANOPY movement policy. No I/O, allocation, firmware addresses or
 * floating point. Called once per 32-sample / 48 kHz block by a future adapter.
 * This is not yet connected to the instrument's SHARC process callback. */
#ifndef CANOPY_MOVEMENT_H
#define CANOPY_MOVEMENT_H
#include <stdint.h>
enum { CM_UP, CM_DOWN, CM_UP_DOWN, CM_AS_PLAYED, CM_RANDOM };
enum { CM_TRIG, CM_PATTERN, CM_CONTINUE };
enum { CM_MASTER, CM_LOCAL };
enum { CM_STOPPED, CM_WAIT_CLOCK, CM_RUNNING, CM_CLOCK_LOST, CM_EXHAUSTED };
enum { CM_OFF, CM_ON };
enum { CM_PAD, CM_ARP };
#define CM_MAX_EVENTS 10
typedef struct {
    uint32_t token;
    uint8_t note, velocity, port, channel;
} CmNote;
typedef struct {
    /* Local rate is notes/quarter: 1,2,3,4,6,8. Master rate is steps: 1..16.
     * Swing applies only to LOCAL; MASTER never estimates subdivisions.
     * Strum is total chord spread in blocks (0..750 = 500 ms).
     * Gate is 1..100 percent; mix is 0..127, zero disables arp onsets. */
    uint32_t clock, rate, order, octaves, gate, swing, restart, bpm, strum, mix;
} CmConfig;
typedef struct {
    uint32_t block, generation, boundary, pattern;
    uint32_t valid, running;
    /* Full admitted membership, ordered by admission, excluding audio tails.
     * phrase is an explicit new-phrase serial from the ownership mapper. */
    uint32_t revision, phrase, generated, count;
    CmNote notes[4];
} CmInput;
typedef struct { uint32_t kind, lane; CmNote note; } CmEvent;
typedef struct {
    CmConfig config;
    CmNote pads[4], pending[4], chord[4], arp;
    uint32_t pending_at[4];
    uint32_t count, revision, phrase, ordinal, random, next_token;
    uint32_t initialized, block, generation, pattern, boundary, boundary_block;
    uint32_t step_count, period, phase, parity, gate_left, status;
    uint32_t started, reset_pending, running, clock_healthy, swing;
} CmState;
void cm_init(CmState *s, uint32_t seed);
/* Invalid updates leave the complete configuration unchanged. Clock/rate
 * changes explicitly restart timing at the next selected-source boundary. */
int cm_configure(CmState *s, const CmConfig *c);
/* Invalid input is rejected before any mutation. Returns event count or -1.
 * The caller must reserve all CM_MAX_EVENTS outputs before calling. Events
 * are ordered releases before replacements; PAD and ARP token domains differ.
 * Output tokens must never be passed to the physical keyboard ledger. */
int cm_tick(CmState *s, const CmInput *in, CmEvent out[CM_MAX_EVENTS]);
/* PANIC emits all active releases, cancels pending work, and forgets candidates.
 * Caller then resets the input ownership generation before accepting new notes. */
uint32_t cm_panic(CmState *s, CmEvent out[CM_MAX_EVENTS]);
#endif
