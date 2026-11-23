#include "track-pool.h"

#define TK_POOL_MAGIC 0x544b0000u
#define TK_POOL_SALT 0x22dca71bu
#define TK_POOL_RELAY_OWNER 0xau

static bool valid_size(const TkPoolSnapshot *snapshot, uint8_t owner, uint16_t mask)
{
    return !(snapshot->relay_owners & (1u << owner)) || !(mask & (mask - 1u));
}

static uint16_t request_for(const TkPoolSnapshot *snapshot, const TkTrackConfig *config,
                            uint8_t owner)
{
    if (!config || !snapshot->epoch || !(snapshot->qualified_owners & (1u << owner)))
        return 0;
    if (config[owner].helpers)
        return config[owner].params.poly ? config[owner].helpers : 0;
    return snapshot->automatic_helpers ? snapshot->automatic_helpers[owner] : 0;
}

void tk_pool_update(TkPool *pool, const TkPoolSnapshot *snapshot,
                    const TkTrackConfig config[TK_TRACKS])
{
    uint16_t claimed = 0, owners = 0, tails = 0;
    bool changed = pool->epoch != snapshot->epoch;
    uint16_t reusable = changed ? 0 : pool->reusable_tails;
    uint16_t next_reusable = 0;
    pool->epoch = snapshot->epoch;

    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
        uint16_t bit = (uint16_t)(1u << owner);
        uint16_t assigned = pool->assigned[owner];
        uint16_t draining = pool->draining[owner] & ~snapshot->quiescent_helpers;
        uint16_t request = request_for(snapshot, config, owner);
        uint16_t keep = assigned & request & snapshot->eligible_helpers;
        bool reuse = !changed && config && !config[owner].helpers &&
                     (snapshot->qualified_owners & bit) && !(snapshot->relay_owners & bit);
        if (changed || !valid_size(snapshot, owner, assigned) ||
            (request && config[owner].helpers && (assigned != request || keep != assigned)))
            keep = 0;
        if (reuse) {
            uint16_t warm = draining & reusable & snapshot->reusable_helpers;
            uint16_t restarted = warm & request;
            draining &= (uint16_t)~restarted;
            keep |= restarted;
            next_reusable |= (warm & ~restarted) | (assigned & ~keep & snapshot->reusable_helpers);
        }
        draining |= assigned & ~keep;
        pool->assigned[owner] = keep;
        pool->draining[owner] = draining;
        claimed |= keep | draining;
        if (keep || draining)
            owners |= (uint16_t)(1u << owner);
        if (draining)
            tails |= (uint16_t)(1u << owner);
    }

    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
        uint16_t bit = (uint16_t)(1u << owner);
        uint16_t request = request_for(snapshot, config, owner);
        if (!request || (claimed & bit))
            continue;
        uint16_t available = request & snapshot->eligible_helpers & snapshot->quiescent_helpers &
                             ~(claimed | owners | bit);
        uint16_t occupied = pool->assigned[owner] | pool->draining[owner];
        uint16_t added = 0;
        if (config[owner].helpers) {
            TkTrackConfig trial;
            if (!occupied && !snapshot->keep_saved_only &&
                tk_assign_helpers(&trial, owner, request, available, UINT16_MAX, 0) &&
                valid_size(snapshot, owner, request))
                added = request;
        } else {
            uint8_t room = snapshot->relay_owners & bit ? 1 : 3;
            for (; occupied && room; occupied &= occupied - 1u)
                --room;
            for (; available && room; --room) {
                uint16_t first = available & (uint16_t)-available;
                added |= first;
                available ^= first;
            }
        }
        pool->assigned[owner] |= added;
        claimed |= added;
        if (added)
            owners |= bit;
    }
    pool->relay_heads = ((snapshot->relay_heads | (owners & ~tails)) & snapshot->relay_owners &
                         snapshot->qualified_owners & ~claimed);
    pool->reusable_tails = next_reusable;
}

uint8_t tk_pool_capacity(const TkPool *pool, uint8_t owner)
{
    if (owner >= TK_TRACKS)
        return 0;
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if ((pool->assigned[track] | pool->draining[track]) & (1u << owner))
            return 0;
    uint8_t voices = 1;
    for (uint16_t mask = pool->assigned[owner]; mask; mask &= mask - 1u)
        ++voices;
    return voices;
}

bool tk_pool_can_start(const TkPool *pool, uint8_t owner, uint8_t destination)
{
    return owner < TK_TRACKS && destination < TK_TRACKS &&
           (pool->assigned[owner] & (1u << destination));
}

void tk_pool_owner_frame(const TkPool *pool, uint32_t words[TK_POOL_WORDS])
{
    words[0] = TK_POOL_MAGIC;
    words[1] = words[2] = 0;
    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
        uint16_t mask = pool->assigned[owner] | pool->draining[owner];
        words[0] |= mask;
        /* A qualified automatic RELAY owner may use its own pitched head. */
        if (pool->relay_heads & (1u << owner))
            words[1 + owner / 8] |= TK_POOL_RELAY_OWNER << (4u * (owner % 8));
        for (uint8_t helper = 0; mask; ++helper, mask >>= 1)
            if (mask & 1u)
                words[1 + helper / 8] |= (uint32_t)owner << (4u * (helper % 8u));
    }
    words[3] = TK_POOL_SALT ^ words[0] ^ words[1] ^ words[2];
}

bool tk_pool_decode_frame(const uint32_t words[TK_POOL_WORDS], uint8_t owner[TK_TRACKS])
{
    uint8_t route[TK_TRACKS];
    uint8_t count[TK_TRACKS] = {0};
    if ((words[0] & 0xffff0000u) != TK_POOL_MAGIC ||
        (words[0] ^ words[1] ^ words[2] ^ words[3]) != TK_POOL_SALT)
        return false;
    for (uint8_t helper = 0; helper < TK_TRACKS; ++helper) {
        route[helper] = TK_POOL_UNBOUND;
        if (words[0] & (1u << helper)) {
            uint8_t owner_track = (uint8_t)(words[1 + helper / 8] >> (4u * (helper % 8u))) & 15u;
            if ((words[0] & (1u << owner_track)) || ++count[owner_track] > 3)
                return false;
            route[helper] = owner_track;
        }
    }
    for (uint8_t helper = 0; helper < TK_TRACKS; ++helper)
        owner[helper] = route[helper];
    return true;
}

bool tk_pool_project_frame(uint16_t frame[TK_POOL_NATIVE_HALFWORDS],
                           const uint32_t map[TK_POOL_WORDS])
{
    enum {
        TRACK_LEVEL = 0x74 / 2,
        FILTER_MACHINE = 0xb4 / 2,
        SOURCE_LEVEL = 0xec / 2,
        FILTER_PAGE = 0xfa / 2,
        FX_PAGE_END = 0x13a / 2,
        TRACK_STRIDE = 0x60 / 2,
    };
    uint8_t route[TK_TRACKS];
    if (!tk_pool_decode_frame(map, route))
        return false;
    for (uint8_t helper = 0; helper < TK_TRACKS; ++helper) {
        uint8_t owner = route[helper];
        if (owner == TK_POOL_UNBOUND)
            continue;
        unsigned source = TRACK_STRIDE * owner;
        unsigned dest = TRACK_STRIDE * helper;
        frame[TRACK_LEVEL + helper] = frame[TRACK_LEVEL + owner];
        frame[FILTER_MACHINE + helper] = frame[FILTER_MACHINE + owner];
        frame[SOURCE_LEVEL + dest] = frame[SOURCE_LEVEL + source];
        for (unsigned word = FILTER_PAGE; word < FX_PAGE_END; ++word)
            frame[word + dest] = frame[word + source];
    }
    return true;
}
