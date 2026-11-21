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

static void check_publication(const TkPool *pool, const uint32_t expected[TK_POOL_WORDS])
{
    uint16_t original[TK_POOL_NATIVE_HALFWORDS] = {0};
    uint16_t queue[2 * (7 + 2 * TK_POOL_WORDS)] = {0};
    original[0x74 / 2] = 0x7400;
    original[0x74 / 2 + 9] = 0x6000;
    original[0x34 / 2 + 9] = 0x7f00;
    assert(tk_pool_link_project(original) == original);
    tk_pool_publish_frame(pool);
    tk_pool_link_write((uintptr_t)queue, 7);
    for (unsigned word = 0; word < TK_POOL_WORDS; ++word) {
        unsigned half = 7 * 2 + 4 * word;
        assert(queue[half] == 0 && queue[half + 2] == 0);
        assert(queue[half + 1] == (uint16_t)expected[word]);
        assert(queue[half + 3] == (uint16_t)(expected[word] >> 16));
    }
    const uint16_t *projected = tk_pool_link_project(original);
    assert(projected != original);
    assert(projected[0x74 / 2 + 9] == 0x7400 && original[0x74 / 2 + 9] == 0x6000);
    assert(projected[0x34 / 2 + 9] == 0x7f00);
    const TkPool empty = {0};
    tk_pool_publish_frame(&empty);
    assert(tk_pool_link_project(original) == original);
}

static void check_quiet_witness(void)
{
    uint16_t reply[0x60 / 2 + 4] = {0};
    uint16_t *packet = reply + 0x60 / 2;
    packet[0] = 0x4b51;
    packet[1] = 0x01ff;
    packet[2] = 7;
    packet[3] = (uint16_t)(packet[1] ^ packet[2] ^ 0x32f7u);
    tk_pool_link_reply(reply);
    uint32_t observed, now;
    assert(tk_pool_link_quiet(&observed, &now) == packet[1] && observed == now);

    for (unsigned i = 0; i < 45u; ++i)
        tk_pool_link_reply(reply); /* Repeated reply must eventually become stale. */
    assert(tk_pool_link_quiet(&observed, &now) == 0 && now - observed == 45);
    ++packet[2];
    tk_pool_link_reply(reply); /* Invalid checksum cannot restore the witness. */
    assert(tk_pool_link_quiet(&observed, &now) == 0);
    packet[3] = (uint16_t)(packet[1] ^ packet[2] ^ 0x32f7u);
    tk_pool_link_reply(reply);
    assert(tk_pool_link_quiet(&observed, &now) == packet[1] && observed == now);
}

void track_pool_checks(void)
{
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
    tk_pool_update(&pool, &snapshot, config);
    assert(tk_pool_capacity(&pool, OWNER) == 4);
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
}
