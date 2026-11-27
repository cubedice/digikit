#include "track-pool.h"
#include "track-memory.h"

typedef struct {
    uint32_t words[TK_POOL_WORDS];
    uint8_t tag;
} TkPoolFrame;

static volatile TkPoolFrame maps[2];
static volatile unsigned published;
#ifdef __m68k__
extern void *tk_fw_alloc(uint32_t size);
static uint16_t *projected;

bool tk_pool_link_init(void)
{
    if (!projected)
        projected = tk_fw_alloc(TK_POOL_NATIVE_HALFWORDS * sizeof(*projected));
    return projected != NULL;
}
#else
static uint16_t projected[TK_POOL_NATIVE_HALFWORDS];
#endif
static volatile uint32_t reply_time, reply_clock, reply_word;
static volatile uint8_t reply_tag;

void tk_pool_publish_frame(const TkPool *pool)
{
    uint32_t packet[TK_POOL_WORDS];
    tk_pool_owner_frame(pool, packet);
    /* Only the UI task publishes. The frame interrupt can read the previous
     * slot until this single index store and completes before the UI resumes. */
    unsigned current = published;
    bool changed = false;
    for (unsigned word = 0; word < TK_POOL_WORDS; ++word)
        changed |= maps[current].words[word] != packet[word];
    if (!changed)
        return;
    unsigned next = current ^ 1u;
    for (unsigned word = 0; word < TK_POOL_WORDS; ++word)
        maps[next].words[word] = packet[word];
    maps[next].tag = (uint8_t)(maps[current].tag + 1u);
    published = next;
}

/* The stock-chord shape words (TK_CHORD_MAGIC, the track, the shape), the sends a changed one still has, and the track whose turn it is. */
enum { CHORD_SENDS = 3 };
static volatile uint32_t chord_word[TK_TRACKS];
static volatile uint8_t chord_sends[TK_TRACKS];
static uint8_t chord_turn;

void tk_pool_link_chord(uint8_t track, uint32_t shape)
{
    if (track >= TK_TRACKS)
        return;
    uint32_t word = TK_CHORD_MAGIC | (uint32_t)track << 20 | (shape & 0xfffffu);
    if (chord_word[track] == word)
        return;
    chord_word[track] = word;
    chord_sends[track] = CHORD_SENDS;
}

/* Frame interrupt: the word of this block. A track that never had a chord sends its empty one. */
static uint32_t chord_next(void)
{
    uint8_t track = TK_TRACKS;
    for (uint8_t t = 0; t < TK_TRACKS && track == TK_TRACKS; ++t)
        if (chord_sends[t])
            track = t;
    if (track < TK_TRACKS) {
        chord_sends[track] = (uint8_t)(chord_sends[track] - 1u);
    } else {
        track = chord_turn;
        chord_turn = (uint8_t)((chord_turn + 1u) % TK_TRACKS);
    }
    uint32_t word = chord_word[track];
    return word ? word : TK_CHORD_MAGIC | (uint32_t)track << 20;
}

void tk_pool_link_write(uintptr_t tx, unsigned entry)
{
    uint32_t chord = chord_next();
    *(volatile uint16_t *)(tx + 4u * entry + 2u) = (uint16_t)chord;
    *(volatile uint16_t *)(tx + 4u * entry + 6u) = (uint16_t)(chord >> 16);
    entry += TK_POOL_CHORD_ENTRIES;
    const volatile TkPoolFrame *packet = &maps[published];
    /* Stamp the header and its checksum together. The native frame copy
     * still uses the untagged map for projecting owner controls. */
    uint32_t tag = (uint32_t)packet->tag << 16;
    for (unsigned word = 0; word < TK_POOL_WORDS; ++word) {
        uint32_t value = packet->words[word];
        if (word == 0 || word == TK_POOL_WORDS - 1)
            value ^= tag;
        *(volatile uint16_t *)(tx + 4u * entry + 2u) = (uint16_t)value;
        *(volatile uint16_t *)(tx + 4u * entry + 6u) = (uint16_t)(value >> 16);
        entry += 2;
    }
}

const uint16_t *tk_pool_link_project(const uint16_t *src)
{
    const volatile uint32_t *packet = maps[published].words;
    if (!src || !(packet[0] & 0xffffu))
        return src;
    uint32_t snapshot[TK_POOL_WORDS];
    for (unsigned word = 0; word < TK_POOL_WORDS; ++word)
        snapshot[word] = packet[word];
    memcpy(projected, src, TK_POOL_NATIVE_HALFWORDS * sizeof(*projected));
    return tk_pool_project_frame(projected, snapshot) ? projected : src;
}

void tk_pool_link_reply(const volatile uint16_t *reply)
{
    enum { ACK = 0x60 / 2, MAGIC = 0x4b51, SALT = 0x32f7 };
    uint32_t now = ++reply_clock;
    if (!reply || (reply[ACK] & 0xff00u) != (MAGIC & 0xff00u))
        return;
    uint8_t tag = (uint8_t)(reply[ACK] ^ MAGIC);
    uint32_t sequence = reply[ACK + 2];
    uint16_t quiet = reply[ACK + 1];
    uint16_t delta = (uint16_t)(sequence - (reply_word >> 16));
    if (!sequence || !delta || (delta >= 0x8000u && now - reply_time <= 40u) ||
        (sequence ^ quiet ^ SALT ^ tag) != reply[ACK + 3])
        return;
    reply_word = (sequence << 16) | quiet;
    reply_tag = tag;
    reply_time = now;
}

uint16_t tk_pool_link_quiet(uint32_t *observed, uint32_t *now)
{
    uint32_t before = reply_time;
    uint32_t word = reply_word;
    *observed = reply_time;
    *now = reply_clock;
    return before && before == *observed && *now - before <= 40 ? (uint16_t)word : 0;
}

bool tk_pool_link_ready(void)
{
    const volatile TkPoolFrame *packet = &maps[published];
    uint32_t before = reply_time;
    uint8_t tag = reply_tag;
    return packet->words[0] && before && before == reply_time && reply_clock - before <= 40 &&
           tag == packet->tag;
}
