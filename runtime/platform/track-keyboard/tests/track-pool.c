#include "../src/track-pool.h"

#include <assert.h>
#include <string.h>

static void check_projected_controls(const uint32_t map[TK_POOL_WORDS])
{
    uint16_t original[TK_POOL_NATIVE_HALFWORDS];
    uint16_t actual[TK_POOL_NATIVE_HALFWORDS];
    uint16_t expected[TK_POOL_NATIVE_HALFWORDS];
    for (unsigned i = 0; i < TK_POOL_NATIVE_HALFWORDS; ++i)
        original[i] = (uint16_t)(i * 17 + 1);
    memcpy(actual, original, sizeof original);
    memcpy(expected, original, sizeof original);
    for (unsigned helper = 8; helper <= 10; ++helper) {
        expected[0x74 / 2 + helper] = original[0x74 / 2];
        expected[0xb4 / 2 + helper] = original[0xb4 / 2];
        expected[(0xec + 0x60 * helper) / 2] = original[0xec / 2];
        memcpy(&expected[(0xfa + 0x60 * helper) / 2], &original[0xfa / 2], 0x40);
    }
    assert(tk_pool_project_frame(actual, map));
    assert(memcmp(actual, expected, sizeof expected) == 0);

    /* A rejected routing frame cannot partially update the native frame. */
    uint32_t corrupt[TK_POOL_WORDS];
    memcpy(corrupt, map, sizeof corrupt);
    corrupt[2] ^= 1;
    assert(!tk_pool_project_frame(actual, corrupt));
    assert(memcmp(actual, expected, sizeof expected) == 0);
}

static void pool_ack(uint16_t *packet, uint8_t epoch, uint16_t quiet, uint16_t sequence)
{
    packet[0] = (uint16_t)(0x4b51u ^ epoch);
    packet[1] = quiet;
    packet[2] = sequence;
    packet[3] = (uint16_t)(sequence ^ quiet ^ 0x32f7u ^ epoch);
}

static void check_publication(const TkPool *pool, const uint32_t expected[TK_POOL_WORDS])
{
    uint16_t original[TK_POOL_NATIVE_HALFWORDS] = {0};
    uint16_t queue[2 * (7 + 2 * TK_POOL_LINK_WORDS)] = {0};
    original[0x74 / 2] = 0x7400;
    original[0x74 / 2 + 9] = 0x6000;
    original[0x34 / 2 + 9] = 0x7f00;
    assert(tk_pool_link_project(original) == original);
    tk_pool_publish_frame(pool);
    tk_pool_link_write((uintptr_t)queue, 7);
    const unsigned map = 2 * (7 + TK_POOL_CHORD_ENTRIES); /* the map follows the chord word */
    uint8_t epoch = (uint8_t)(queue[map + 3] ^ 0x544bu);
    assert(epoch != 0);
    for (unsigned word = 0; word < TK_POOL_WORDS; ++word) {
        unsigned half = map + 4 * word;
        assert(queue[half] == 0 && queue[half + 2] == 0);
        assert(queue[half + 1] == (uint16_t)expected[word]);
        uint16_t tag = word == 0 || word == 3 ? epoch : 0;
        assert(queue[half + 3] == ((uint16_t)(expected[word] >> 16) ^ tag));
    }
    const uint16_t *projected = tk_pool_link_project(original);
    assert(projected != original);
    assert(projected[0x74 / 2 + 9] == 0x7400 && original[0x74 / 2 + 9] == 0x6000);
    assert(projected[0x34 / 2 + 9] == 0x7f00);

    uint16_t reply[0x60 / 2 + 4] = {0};
    uint16_t *packet = reply + 0x60 / 2;
    assert(!tk_pool_link_ready());
    pool_ack(packet, (uint8_t)(epoch - 1), UINT16_MAX, 4);
    tk_pool_link_reply(reply);
    assert(!tk_pool_link_ready());
    pool_ack(packet, epoch, UINT16_MAX, 5);
    packet[3] ^= 1;
    tk_pool_link_reply(reply);
    assert(!tk_pool_link_ready());
    pool_ack(packet, epoch, UINT16_MAX, 6);
    tk_pool_link_reply(reply);
    assert(tk_pool_link_ready());

    tk_pool_publish_frame(pool); /* Republishing an unchanged route leaves the ACK valid. */
    assert(tk_pool_link_ready());
    const TkPool empty = {0};
    tk_pool_publish_frame(&empty);
    assert(!tk_pool_link_ready());
    assert(tk_pool_link_project(original) == original);
}

static void check_quiet_witness(void)
{
    uint16_t reply[0x60 / 2 + 4] = {0};
    uint16_t *packet = reply + 0x60 / 2;
    uint16_t queue[4 * TK_POOL_LINK_WORDS] = {0};
    tk_pool_link_write((uintptr_t)queue, 0);
    uint8_t epoch = (uint8_t)(queue[2 * TK_POOL_CHORD_ENTRIES + 3] ^ 0x544bu);
    pool_ack(packet, epoch, 0x01ff, 7);
    tk_pool_link_reply(reply);
    uint32_t observed, now;
    assert(tk_pool_link_quiet(&observed, &now) == packet[1] && observed == now);
    assert(tk_pool_link_ready());

    for (unsigned i = 0; i < 45u; ++i)
        tk_pool_link_reply(reply); /* Repeated reply must eventually become stale. */
    assert(tk_pool_link_quiet(&observed, &now) == 0 && now - observed == 45);
    assert(!tk_pool_link_ready());
    ++packet[2];
    tk_pool_link_reply(reply); /* Invalid checksum cannot restore the witness. */
    assert(tk_pool_link_quiet(&observed, &now) == 0);
    pool_ack(packet, epoch, packet[1], packet[2]);
    tk_pool_link_reply(reply);
    assert(tk_pool_link_quiet(&observed, &now) == packet[1] && observed == now);
    assert(tk_pool_link_ready());
}

static void check_automatic_ownership(void)
{
    enum { OWNER = 0, COMPETITOR = 1, H1 = 8, H2 = 9, H3 = 10, H4 = 11 };
    const uint16_t first = 1u << H1;
    const uint16_t middle = 1u << H2 | 1u << H3;
    const uint16_t last = 1u << H4;
    TkTrackConfig config[TK_TRACKS] = {0};
    uint16_t automatic[TK_TRACKS] = {0};
    TkPool pool = {0};
    TkPoolSnapshot snapshot = {.epoch = 1,
                               .qualified_owners = 3,
                               .eligible_helpers = first | middle | last,
                               .quiescent_helpers = first,
                               .automatic_helpers = automatic};
    uint32_t frame[TK_POOL_WORDS];
    uint8_t route[TK_TRACKS];

    automatic[OWNER] = first | middle;
    tk_pool_update(&pool, &snapshot, config);
    assert(pool.assigned[OWNER] == first); /* A busy candidate need not stall a quiet one. */
    snapshot.quiescent_helpers = middle | last;
    tk_pool_update(&pool, &snapshot, config);
    assert(pool.assigned[OWNER] == (first | middle) && !pool.draining[OWNER]);

    automatic[OWNER] = middle | last;
    automatic[COMPETITOR] = first;
    snapshot.quiescent_helpers = last;
    tk_pool_update(&pool, &snapshot, config);
    assert(pool.assigned[OWNER] == middle && pool.draining[OWNER] == first);
    assert(!pool.assigned[COMPETITOR]); /* Keep the old tail and the routing frame's limit. */
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route) && route[H1] == OWNER &&
           route[H4] == TK_POOL_UNBOUND);

    snapshot.quiescent_helpers |= first;
    tk_pool_update(&pool, &snapshot, config);
    assert(pool.assigned[OWNER] == (middle | last) && !pool.draining[OWNER]);
    assert(pool.assigned[COMPETITOR] == first);
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route) && route[H1] == COMPETITOR && route[H4] == OWNER);

    automatic[OWNER] = automatic[COMPETITOR] = 0;
    snapshot.quiescent_helpers = 0;
    tk_pool_update(&pool, &snapshot, config);
    assert(!pool.assigned[OWNER] && pool.draining[OWNER] == (middle | last));
    assert(!pool.assigned[COMPETITOR] && pool.draining[COMPETITOR] == first);
    snapshot.quiescent_helpers = snapshot.eligible_helpers;
    tk_pool_update(&pool, &snapshot, config);
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route) && !(frame[0] & 0xffffu));
    assert(!config[OWNER].helpers && !config[COMPETITOR].helpers);

    /* A standalone RELAY marker leaves the native mixer route untouched. */
    snapshot.relay_owners = snapshot.relay_heads = 1u << OWNER;
    tk_pool_update(&pool, &snapshot, config);
    tk_pool_owner_frame(&pool, frame);
    assert((frame[1] & 0xf) == 0xa && !(frame[0] & 0xffffu));
    assert(tk_pool_decode_frame(frame, route) && route[OWNER] == TK_POOL_UNBOUND);
    automatic[OWNER] = first | middle;
    tk_pool_update(&pool, &snapshot, config);
    assert(pool.assigned[OWNER] == first); /* RELAY is capped at one helper. */
    snapshot.quiescent_helpers = middle;
    automatic[OWNER] = middle;
    tk_pool_update(&pool, &snapshot, config);
    assert(!pool.assigned[OWNER] && pool.draining[OWNER] == first);
    tk_pool_owner_frame(&pool, frame);
    assert((frame[1] & 0xf) == 0xa);
    assert(tk_pool_decode_frame(frame, route) && route[H1] == OWNER &&
           route[H2] == TK_POOL_UNBOUND);
    snapshot.relay_heads = 0;
    tk_pool_update(&pool, &snapshot, config);
    tk_pool_owner_frame(&pool, frame);
    assert((frame[1] & 0xf) == 0);
    assert(tk_pool_decode_frame(frame, route) && route[H1] == OWNER);
}

static uint32_t chord_word_written(void)
{
    uint16_t queue[4 * TK_POOL_LINK_WORDS] = {0};
    tk_pool_link_write((uintptr_t)queue, 0);
    return queue[1] | (uint32_t)queue[3] << 16;
}

static void check_chord_words(void)
{
    /* The stock-chord shape words before the map (finding 375): a changed one at once and twice more, then every track in turn. */
    for (unsigned i = 0; i < 4 * TK_TRACKS; ++i)
        (void)chord_word_written(); /* what earlier checks left to send */
    const uint32_t shape = 2u << 18 | 4u << 12 | 7u << 6;
    tk_pool_link_chord(5, shape);
    for (unsigned i = 0; i < 3; ++i)
        assert(chord_word_written() == (TK_CHORD_MAGIC | 5u << 20 | shape));
    uint16_t seen = 0;
    for (unsigned i = 0; i < TK_TRACKS; ++i) {
        uint32_t word = chord_word_written();
        assert((word & 0xff000000u) == TK_CHORD_MAGIC);
        uint8_t track = (uint8_t)(word >> 20 & 15u);
        seen |= (uint16_t)(1u << track);
        assert((word & 0xfffffu) == (track == 5 ? shape : 0u));
    }
    assert(seen == 0xffffu);
    tk_pool_link_chord(5, shape); /* unchanged: nothing extra to send */
    tk_pool_link_chord(5, (uint32_t)(-3 & 63) << 12 | 1u << 18);
    assert(chord_word_written() == (TK_CHORD_MAGIC | 5u << 20 | 1u << 18 | 61u << 12));
    tk_pool_link_chord(5, 0);
}

void track_pool_checks(void)
{
    check_chord_words();
    enum { OWNER = 0, COMPETITOR = 1, H1 = 8, H2 = 9, H3 = 10 };
    const uint16_t group = 1u << H1 | 1u << H2 | 1u << H3;
    const uint16_t eligible = group | (1u << COMPETITOR);
    TkTrackConfig config[TK_TRACKS] = {0};
    config[OWNER].params.poly = config[COMPETITOR].params.poly = 1;
    config[OWNER].helpers = config[COMPETITOR].helpers = group;
    TkPool pool = {0};
    TkPoolSnapshot snapshot = {.epoch = 17,
                               .qualified_owners = 3,
                               .eligible_helpers = eligible,
                               .quiescent_helpers = eligible & ~(1u << H2)};
    uint32_t frame[TK_POOL_WORDS];
    uint8_t route[TK_TRACKS];

    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 1);
    assert(!tk_pool_can_start(&pool, OWNER, H1)); /* Whole request or no assignment. */
    snapshot.quiescent_helpers = eligible;
    snapshot.keep_saved_only = true;
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 1); /* No new saved lease during playback. */
    snapshot.keep_saved_only = false;
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 4);
    snapshot.keep_saved_only = true;
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 4);
    snapshot.keep_saved_only = false;
    assert(tk_pool_capacity(&pool, COMPETITOR) == 1);
    assert(tk_pool_capacity(&pool, H1) == 0);
    assert(tk_pool_can_start(&pool, OWNER, H2));
    assert(!tk_pool_can_start(&pool, COMPETITOR, H2));
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route));
    assert(route[H1] == OWNER && route[H2] == OWNER && route[H3] == OWNER);
    assert(route[OWNER] == TK_POOL_UNBOUND && route[COMPETITOR] == TK_POOL_UNBOUND);
    check_projected_controls(frame);
    check_publication(&pool, frame);
    check_quiet_witness();

    /* Beginning retirement closes starts, retaining the route while native
     * releases travel through the worker and the envelopes run down. */
    snapshot.quiescent_helpers = 0;
    config[OWNER].params.poly = 0;
    tk_pool_update(&pool, &snapshot, config);
    assert(config[OWNER].helpers == group && config[COMPETITOR].helpers == group);
    assert(!tk_pool_can_start(&pool, OWNER, H2) && !tk_pool_can_start(&pool, COMPETITOR, H2));
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route) && route[H2] == OWNER);

    /* Even a checksummed frame cannot route a source through another helper. */
    frame[2] ^= H2;
    assert(!tk_pool_decode_frame(frame, route) && route[H1] == OWNER);
    frame[3] ^= H2;
    assert(!tk_pool_decode_frame(frame, route) && route[H1] == OWNER);

    snapshot.quiescent_helpers = eligible;
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, COMPETITOR) == 4);
    assert(tk_pool_can_start(&pool, COMPETITOR, H3));
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route) && route[H2] == COMPETITOR);

    /* Pattern changes drain even when the next request is identical. A
     * qualifier loss prevents admission even after the old setup is clear. */
    ++snapshot.epoch;
    tk_pool_update(&pool, &snapshot, config);
    assert(!tk_pool_can_start(&pool, COMPETITOR, H3));
    assert(tk_pool_capacity(&pool, H3) == 0);
    snapshot.qualified_owners &= ~(1u << COMPETITOR);
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, H3) == 1);
    tk_pool_owner_frame(&pool, frame);
    assert(tk_pool_decode_frame(frame, route) && route[H2] == TK_POOL_UNBOUND);

    /* An active owner's mixer cannot be a helper for another group. */
    snapshot.qualified_owners = 3;
    tk_pool_update(&pool, &snapshot, config);
    config[OWNER].params.poly = 1;
    config[OWNER].helpers = 1u << COMPETITOR;
    tk_pool_update(&pool, &snapshot, config);
    assert(!tk_pool_can_start(&pool, OWNER, COMPETITOR));
    assert(tk_pool_capacity(&pool, COMPETITOR) == 4);

    /* A RELAY owner can lease one empty sample track, even if an older
     * pattern has stored a larger request from a stock sample machine. */
    pool = (TkPool){0};
    snapshot.relay_owners = 1u << OWNER;
    config[OWNER].helpers = group;
    config[COMPETITOR].params.poly = 0;
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 1);
    config[OWNER].helpers = 1u << H1;
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 2);
    assert(tk_pool_can_start(&pool, OWNER, H1));
    tk_pool_owner_frame(&pool, frame);
    assert((frame[1] & 0xf) == 0xa);
    assert(tk_pool_decode_frame(frame, route) && route[H1] == OWNER);
    snapshot.quiescent_helpers = 0;
    config[OWNER].params.poly = 0;
    tk_pool_update(&pool, &snapshot, config);
    tk_pool_owner_frame(&pool, frame);
    assert((frame[1] & 0xf) == 0); /* Draining is a mixer route, never read permission. */
    assert(tk_pool_decode_frame(frame, route) && route[H1] == OWNER);
    check_automatic_ownership();
}
