#ifndef DK_TRACK_POOL_H
#define DK_TRACK_POOL_H

#include "track-config.h"
bool tk_pool_link_init(void);

enum { TK_POOL_WORDS = 4, TK_POOL_UNBOUND = TK_TRACKS };

typedef struct {
    uint16_t assigned[TK_TRACKS];
    uint16_t draining[TK_TRACKS];
    uint32_t epoch;
    uint16_t relay_heads;
    /* Same-owner sample tails retired in the current context. */
    uint16_t reusable_tails;
} TkPool;

typedef struct {
    uint32_t epoch;
    uint16_t qualified_owners;
    uint16_t relay_owners;
    /* Automatic RELAY owners can play a pitched head with no helper. */
    uint16_t relay_heads;
    /* Empty audio destinations with native events excluded during a lease. */
    uint16_t eligible_helpers;
    /* Includes the worker queue, sample cursor and envelope release. */
    uint16_t quiescent_helpers;
    uint16_t reusable_helpers;
    bool keep_saved_only;
    /* Transient requests planned by the platform. A stored mask takes precedence. */
    const uint16_t *automatic_helpers;
} TkPoolSnapshot;

/* A retired route stays in the mixer map until its tail has drained.
 * An automatic sample owner can reuse an eligible tail from the current
 * context. Competing owners wait for quiet. Stored masks admit as a group.
 * The pool never writes either request back into the pattern.
 */
void tk_pool_update(TkPool *pool, const TkPoolSnapshot *snapshot,
                    const TkTrackConfig config[TK_TRACKS]);

uint8_t tk_pool_capacity(const TkPool *pool, uint8_t owner);
bool tk_pool_can_start(const TkPool *pool, uint8_t owner, uint8_t destination);

/* Encode one complete pre-send owner map. The DSP commits all four words
 * together. Active and draining helpers route to the same stereo owner bus.
 */
void tk_pool_owner_frame(const TkPool *pool, uint32_t words[TK_POOL_WORDS]);
bool tk_pool_decode_frame(const uint32_t words[TK_POOL_WORDS], uint8_t owner[TK_TRACKS]);

/* DTII 1.17 native frame. Resolve helpers before processing the stock
 * track gain, filter, AMP and pan. Call with a private copy of the frame.
 * Changing a helper's masked controls never mutates the stock Sound data.
 */
enum { TK_POOL_NATIVE_HALFWORDS = 0x802 / 2 };
bool tk_pool_project_frame(uint16_t frame[TK_POOL_NATIVE_HALFWORDS],
                           const uint32_t map[TK_POOL_WORDS]);

/* The frame region of the track keyboard (core/dsp-link DL_TRACK_POOL_WORDS): one stock-chord shape word, then the pool map. An entry
 * is one wire halfword. */
enum { TK_POOL_CHORD_ENTRIES = 2, TK_POOL_LINK_WORDS = 1 + TK_POOL_WORDS };
#define TK_CHORD_MAGIC 0xc5000000u
#define TK_HIDDEN_MAGIC 0xc6000000u

/* UI publication and interrupt-side consumers of the same snapshot. */
void tk_pool_publish_frame(const TkPool *pool);
/* The chord a track's voice plays as copies (finding 375): the 20 low bits of its shape word (copies in 18-19, three 6-bit signed intervals
 * in 12-17, 6-11 and 0-5), 0 for none. UI task. The frame sends one track's word per block: a changed one at once and twice more, the
 * others in turn. */
void tk_pool_link_chord(uint8_t track, uint32_t shape);
/* Hidden voice `voice` (1 .. 3; 0: the track's own note) of a track: the 18 low bits of its word (the gate in bit 17, a serial in 14-16, the
 * pitch in 7-13, the velocity in 0-6). It goes out in the same frame word as the shapes, changed ones first. */
void tk_pool_link_hidden(uint8_t track, uint8_t voice, uint32_t bits);
void tk_pool_link_write(uintptr_t tx, unsigned entry);
const uint16_t *tk_pool_link_project(const uint16_t *src);
void tk_pool_link_reply(const volatile uint16_t *reply);
uint16_t tk_pool_link_quiet(uint32_t *observed, uint32_t *now);
bool tk_pool_link_ready(void);

/* DTII 1.17 UI task. Saved groups admit at rest, AUTO follows note demand. */
void tk_pool_fw_update(uint32_t pattern, uint32_t selected, const TkTrackConfig config[TK_TRACKS],
                       bool stopped);
bool tk_pool_fw_relay_ready(void);
bool tk_pool_fw_automatic(uint8_t owner);
bool tk_pool_fw_start_ready(uint8_t owner, uint8_t destination);
bool tk_pool_fw_waiting_helpers(uint8_t owner);
uint16_t tk_pool_fw_eligible(uint8_t owner);
int tk_pool_fw_owner(uint8_t track);
uint8_t tk_pool_fw_voices(uint8_t owner);
uint8_t tk_pool_fw_limit(uint8_t owner);
uint16_t tk_pool_fw_helpers(uint8_t owner);
uint16_t tk_pool_fw_quiet(void);
bool tk_pool_fw_reserved(uint8_t track);
void tk_pool_fw_note_edge(uint8_t track);

#endif
