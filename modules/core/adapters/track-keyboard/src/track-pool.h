#ifndef DK_TRACK_POOL_H
#define DK_TRACK_POOL_H

#include "track-config.h"

enum { TK_POOL_WORDS = 4, TK_POOL_UNBOUND = TK_TRACKS };

typedef struct {
    uint16_t assigned[TK_TRACKS];
    uint16_t draining[TK_TRACKS];
    uint32_t epoch;
} TkPool;

typedef struct {
    uint32_t epoch;
    uint16_t qualified_owners;
    /* Empty audio destinations with native events excluded during a lease. */
    uint16_t eligible_helpers;
    /* Includes the worker queue, sample cursor and envelope release. */
    uint16_t quiescent_helpers;
} TkPoolSnapshot;

/* A retired route stays in the mixer map until the platform confirms its
 * tail has drained. It is unavailable for starts and to competing owners.
 * The active pattern owns requested masks; the pool never rewrites them.
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

/* UI publication and interrupt-side consumers of the same snapshot. */
void tk_pool_publish_frame(const TkPool *pool);
void tk_pool_link_write(uintptr_t tx, unsigned entry);
const uint16_t *tk_pool_link_project(const uint16_t *src);
void tk_pool_link_reply(const volatile uint16_t *reply);
uint16_t tk_pool_link_quiet(uint32_t *observed, uint32_t *now);

/* DTII 1.17 UI task. Only stopped ONESHOT groups may claim helpers. */
void tk_pool_fw_update(uint32_t pattern, const TkTrackConfig config[TK_TRACKS], bool can_admit);
uint16_t tk_pool_fw_eligible(uint8_t owner);
int tk_pool_fw_owner(uint8_t track);
uint8_t tk_pool_fw_voices(uint8_t owner);
uint16_t tk_pool_fw_helpers(uint8_t owner);
uint16_t tk_pool_fw_quiet(void);
bool tk_pool_fw_reserved(uint8_t track);
void tk_pool_fw_note_edge(uint8_t track);

#endif
