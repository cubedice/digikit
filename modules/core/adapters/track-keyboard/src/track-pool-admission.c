#include "track-pool.h"

#include "track-capture.h"
#include "track-canopy.h"
#include "track-pattern-fw.h"
#include "dkrt.h"

extern uintptr_t tk_fw_project(void);
extern uintptr_t tk_fw_active_kit(uintptr_t project);
extern int32_t tk_fw_track_type(uintptr_t kit, uint32_t track);
extern uint32_t tk_fw_midi_mode(uint32_t track);
extern uintptr_t tk_pool_fw_audio_queue(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t sr);
extern uint32_t pp_current_pattern(void);
extern volatile uint32_t tk_native_audio_active;
extern int rtm_is_type(unsigned type) __attribute__((weak));

static TkPool pool;
static uint32_t last_pattern, last_revision, generation;
static uintptr_t last_kit;
static uint32_t guard_at[TK_TRACKS];
static uint32_t rescanned;
static uint16_t relay_owners;
static uint16_t eligible, owners, empty_tracks;
static uint8_t machine[TK_TRACKS];
static bool previously_admitting, relay_head_package;
static volatile uint16_t preparing, reserved;

static bool relay_head_module(void)
{
    static const char module_id[] = "digikit/relay-machine";
    DkrtModule module;
    if (!rtm_is_type)
        return false;
    for (unsigned i = 0; dkrt_module(i, &module) == DKRT_OK; ++i) {
        unsigned n = 0;
        while (n < sizeof(module_id) && module.name[n] == (uint8_t)module_id[n])
            ++n;
        if (n == sizeof(module_id))
            return module.state == DKRT_MOD_LOADED && module.version >= 0x000500u;
    }
    return false;
}

bool tk_pool_fw_relay_ready(void)
{
    return relay_head_package;
}

static bool ram_record(uintptr_t address, unsigned size)
{
    return address >= 0x40000000u && address <= 0x48000000u - size && !(address & 1u);
}

static uint16_t queued_tracks(void)
{
    /* A nonempty bucket can contain only clock/control work. Check the
     * destination of queued note records before testing helper quiescence. */
    uint16_t tracks = 0;
    unsigned budgets = 32, records = 128;
    uint32_t sr = dkrt_irq_mask();
    uintptr_t bucket = tk_pool_fw_audio_queue();
    while (bucket && budgets-- && ram_record(bucket, 20)) {
        const volatile uint32_t *group = (const volatile uint32_t *)bucket;
        uintptr_t record = group[2];
        while (record && records && ram_record(record, 0x6c)) {
            --records;
            const volatile uint32_t *event = (const volatile uint32_t *)record;
            if (event[0] <= 1 && event[4] < TK_TRACKS)
                tracks |= (uint16_t)(1u << event[4]);
            if (event[0] == 2 || event[0] > 8) {
                tracks = UINT16_MAX; /* A kit transition or unknown event. */
                break;
            }
            record = event[0x1a];
        }
        if (record) {
            tracks = UINT16_MAX;
            break;
        }
        bucket = group[4];
    }
    if (bucket)
        tracks = UINT16_MAX;
    dkrt_irq_restore(sr);
    return tracks;
}

static bool empty_track(uint32_t pattern, uint8_t track)
{
    enum { PATTERN_SIZE = 0x1db8c, TRACK_SIZE = 0x6a5, NOTE_HEAD = 0x12a50, LOCK_COUNTS = 0x1d360 };
    if (!ram_record(pattern, PATTERN_SIZE))
        return false;
    const volatile uint8_t *trk =
        (const volatile uint8_t *)(uintptr_t)(pattern + track * TRACK_SIZE);
    const volatile uint8_t *head =
        (const volatile uint8_t *)(uintptr_t)(pattern + NOTE_HEAD + 2u * TK_STEPS * track);
    const volatile uint8_t *locks =
        (const volatile uint8_t *)(uintptr_t)(pattern + LOCK_COUNTS + TK_STEPS * track);
    for (unsigned step = 0; step < TK_STEPS; ++step) {
        if ((trk[step * 2 + 1] & 1u) || !(head[step * 2] & 0x80u) || locks[step])
            return false;
    }
    for (unsigned offset = 0x100; offset < 0x480; ++offset)
        if (trk[offset] != 0xffu)
            return false;
    return true;
}

uint16_t tk_pool_fw_eligible(uint8_t owner)
{
    if (owner >= TK_TRACKS || !(owners & (1u << owner)) || tk_pool_fw_owner(owner) >= 0)
        return 0;
    uint16_t excluded = (uint16_t)(1u << owner);
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if (track != owner && (pool.assigned[track] || pool.draining[track]))
            excluded |= (uint16_t)((1u << track) | pool.assigned[track] | pool.draining[track]);
    return eligible & ~excluded;
}

int tk_pool_fw_owner(uint8_t track)
{
    if (track >= TK_TRACKS)
        return -1;
    uint16_t bit = (uint16_t)(1u << track);
    if (reserved & bit)
        for (uint8_t owner = 0; owner < TK_TRACKS; ++owner)
            if ((pool.assigned[owner] | pool.draining[owner]) & bit)
                return owner;
    return -1;
}

uint8_t tk_pool_fw_voices(uint8_t owner)
{
    return tk_pool_capacity(&pool, owner);
}

uint16_t tk_pool_fw_helpers(uint8_t owner)
{
    return owner < TK_TRACKS ? pool.assigned[owner] : 0;
}

bool tk_pool_fw_reserved(uint8_t track)
{
    return track < TK_TRACKS && ((preparing | reserved) & (1u << track));
}

uint16_t tk_pool_fw_quiet(void)
{
    uint32_t observed, now;
    return tk_pool_link_quiet(&observed, &now);
}

void tk_pool_fw_note_edge(uint8_t track)
{
    uint32_t observed, now;
    (void)tk_pool_link_quiet(&observed, &now);
    if (track < TK_TRACKS)
        guard_at[track] = now + 24u;
}

int tk_pool_fw_block_note(const int32_t *event)
{
    if (!event || (uint32_t)event[0] >= TK_TRACKS)
        return 0;
    uint16_t bit = (uint16_t)(1u << event[0]);
    return (reserved & bit) || (event[3] == 1 && (preparing & bit));
}

void tk_pool_fw_update(uint32_t pattern, const TkTrackConfig config[TK_TRACKS], bool can_admit)
{
    /* Start a new lease while stopped. Keep an eligible existing lease
     * through PLAY on the same pattern and drain a lost lease in place. */
    uintptr_t project = tk_fw_project();
    uintptr_t kit = project ? tk_fw_active_kit(project) : 0;
    uint32_t revision = tk_pattern_fw_revision();
    bool context_changed = pattern != last_pattern || revision != last_revision || kit != last_kit;
    if (context_changed) {
        last_pattern = pattern;
        last_revision = revision;
        last_kit = kit;
        if (!++generation)
            ++generation;
    }
    uint32_t observed, now;
    uint16_t quiet = tk_pool_link_quiet(&observed, &now) & ~tk_native_audio_active;
    owners = eligible = relay_owners = 0;
    bool may_create = can_admit && tk_capture_midi_mode() == TK_CAPTURE_OFF;
    bool observing = (may_create || reserved) && pattern && kit;
    if (observing) {
        if (context_changed || !previously_admitting || now - rescanned >= 120u) {
            uint32_t live = pp_current_pattern();
            relay_head_package = relay_head_module();
            empty_tracks = 0;
            for (uint8_t track = 0; track < TK_TRACKS; ++track)
                if (empty_track(pattern, track) &&
                    (!live || live == pattern || empty_track(live, track)))
                    empty_tracks |= (uint16_t)(1u << track);
            rescanned = now;
        }
        uint16_t sample_tracks = 0;
        for (uint8_t track = 0; track < TK_TRACKS; ++track) {
            int type = tk_fw_track_type(kit, track);
            bool changed = machine[track] != (uint8_t)type;
            machine[track] = (uint8_t)type;
            if (type < 0 || changed || tk_fw_midi_mode(track) || tk_canopy_is_owner(track, type))
                continue;
            /* The six stock sample machines share the stock stereo cursor
             * bank. Retire a lease when either machine changes, since its
             * live state and loaded sample belonged to the earlier voice. */
            if (type <= 6 && type != 5)
                sample_tracks |= (uint16_t)(1u << track);
            else if (relay_head_package && rtm_is_type && rtm_is_type((unsigned)type))
                relay_owners |= (uint16_t)(1u << track);
        }
        owners = sample_tracks | relay_owners;
        eligible = sample_tracks & empty_tracks;
        if (!may_create) {
            uint16_t assigned = 0;
            for (uint8_t track = 0; track < TK_TRACKS; ++track)
                assigned |= pool.assigned[track];
            eligible &= assigned;
        }
    }
    previously_admitting = observing;
    uint16_t requested = 0;
    if (config)
        for (uint8_t owner = 0; owner < TK_TRACKS; ++owner)
            if (config[owner].params.poly && config[owner].helpers &&
                !(config[owner].helpers & ~tk_pool_fw_eligible(owner))) {
                uint16_t helpers = config[owner].helpers;
                if (!(relay_owners & (1u << owner)) || !(helpers & (helpers - 1u)))
                    requested |= helpers;
            }

    if (requested || reserved)
        quiet &= (uint16_t)~queued_tracks();
    uint16_t changed = (requested ^ preparing) & requested;
    preparing = requested | reserved;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        uint16_t bit = (uint16_t)(1u << track);
        if (changed & bit)
            guard_at[track] = now + 24u;
        if (((requested | reserved) & bit) && (int32_t)(observed - guard_at[track]) < 0)
            quiet &= (uint16_t)~bit;
    }
    TkPoolSnapshot snapshot = {.epoch = generation,
                               .qualified_owners = owners,
                               .relay_owners = relay_owners,
                               .eligible_helpers = eligible,
                               .quiescent_helpers = quiet};
    tk_pool_update(&pool, &snapshot, config);
    uint16_t leased = 0;
    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner)
        leased |= pool.assigned[owner] | pool.draining[owner];
    reserved = leased;
    preparing = requested;
    tk_pool_publish_frame(&pool);
}
