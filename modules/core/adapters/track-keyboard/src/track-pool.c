#include "track-pool.h"

#define TK_POOL_MAGIC 0x544b0000u
#define TK_POOL_SALT 0x22dca71bu

void tk_pool_update(TkPool *pool, const TkPoolSnapshot *snapshot,
                    const TkTrackConfig config[TK_TRACKS])
{
    uint16_t claimed = 0;
    uint16_t owners = 0;
    bool changed = pool->epoch != snapshot->epoch;
    pool->epoch = snapshot->epoch;

    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
        uint16_t assigned = pool->assigned[owner];
        uint16_t draining = pool->draining[owner] & ~snapshot->quiescent_helpers;
        bool enabled = config && snapshot->epoch && (snapshot->qualified_owners & (1u << owner)) &&
                       config[owner].params.poly;
        if (assigned && (changed || !enabled || config[owner].helpers != assigned ||
                         (assigned & ~snapshot->eligible_helpers))) {
            draining |= assigned;
            assigned = 0;
        }
        pool->assigned[owner] = assigned;
        pool->draining[owner] = draining;
        claimed |= assigned | draining;
        if (assigned || draining)
            owners |= (uint16_t)(1u << owner);
    }

    if (!config)
        return;
    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
        const TkTrackConfig *request = &config[owner];
        if (!request->helpers || !request->params.poly || !snapshot->epoch ||
            !(snapshot->qualified_owners & (1u << owner)) || (claimed & (1u << owner)) ||
            pool->assigned[owner] || pool->draining[owner])
            continue;
        TkTrackConfig trial = {0};
        if (!tk_assign_helpers(&trial, owner, request->helpers, snapshot->eligible_helpers,
                               snapshot->quiescent_helpers, claimed | owners))
            continue;
        pool->assigned[owner] = trial.helpers;
        claimed |= trial.helpers;
        owners |= (uint16_t)(1u << owner);
    }
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
