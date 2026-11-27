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

/* The words of the frame word before the map: the stock-chord shapes (TK_CHORD_MAGIC, the track, the shape) of the 16 tracks, then four
 * hidden-voice words per track (TK_HIDDEN_MAGIC; 0 until one is set). The sends a changed one still has, and the word whose turn it is. */
enum { LINK_SENDS = 3, LINK_COUNT = TK_TRACKS + 4 * TK_TRACKS };
static volatile uint32_t link_word[LINK_COUNT];
static volatile uint8_t link_sends[LINK_COUNT];
static uint8_t link_turn;

static void link_publish(unsigned index, uint32_t word)
{
    if (link_word[index] == word)
        return;
    link_word[index] = word;
    link_sends[index] = LINK_SENDS;
}

void tk_pool_link_chord(uint8_t track, uint32_t shape)
{
    if (track < TK_TRACKS)
        link_publish(track, TK_CHORD_MAGIC | (uint32_t)track << 20 | (shape & 0xfffffu));
}

void tk_pool_link_hidden(uint8_t track, uint8_t voice, uint32_t bits)
{
    if (track < TK_TRACKS && voice < 4)
        link_publish(TK_TRACKS + 4u * track + voice, TK_HIDDEN_MAGIC | (uint32_t)track << 20 |
                                                         (uint32_t)voice << 18 | (bits & 0x3ffffu));
}

/* A track that never had a chord sends its empty shape; a hidden voice never set sends nothing. */
static uint32_t link_at(unsigned index)
{
    uint32_t word = link_word[index];
    return word || index >= TK_TRACKS ? word : TK_CHORD_MAGIC | (uint32_t)index << 20;
}

/* Frame interrupt: the word of this block. A changed one goes at once and twice more, the others in turn. */
static uint32_t chord_next(void)
{
    for (unsigned i = 0; i < LINK_COUNT; ++i)
        if (link_sends[i]) {
            link_sends[i] = (uint8_t)(link_sends[i] - 1u);
            return link_at(i);
        }
    for (unsigned k = 0; k < LINK_COUNT; ++k) {
        unsigned i = link_turn;
        link_turn = (uint8_t)((link_turn + 1u) % LINK_COUNT);
        uint32_t word = link_at(i);
        if (word)
            return word;
    }
    return 0;
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
