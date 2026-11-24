#include "tk-seam.h"
#include "track-pool.h"

#include "track-capture.h"
#include "track-canopy.h"
#include "track-memory.h"
#include "track-midi.h"
#include "track-native.h"
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
static uint32_t rescanned, scan_patterns[4], scan_kits[2];
static uint16_t relay_owners, automatic_owners;
static uint16_t planned_chords, wanted[TK_TRACKS];
static uint16_t eligible, owners, empty_tracks;
static uint8_t machine[TK_TRACKS];
static bool relay_head_package;
static volatile uint16_t preparing, reserved;
static volatile uint32_t panel_record_audition;

static bool relay_head_module(void)
{
    static const char module_id[] = "digikit/relay-machine";
    DkrtModule module;
    if (!TK_HAVE(rtm_is_type))
        return false;
    for (unsigned i = 0; dkrt_module(i, &module) == DKRT_OK; ++i) {
        unsigned n = 0;
        while (n < sizeof(module_id) && module.name[n] == (uint8_t)module_id[n])
            ++n;
        if (n == sizeof(module_id))
            return module.state == DKRT_MOD_LOADED && module.version >= 0x000501u;
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

bool tk_pool_fw_automatic(uint8_t owner)
{
    return owner < TK_TRACKS && (automatic_owners & (1u << owner));
}

uint8_t tk_pool_fw_limit(uint8_t owner)
{
    if (tk_pool_fw_automatic(owner) && !tk_pool_fw_reserved(owner))
        return relay_owners & (1u << owner) ? 2 : 4;
    return tk_pool_fw_voices(owner);
}

bool tk_pool_fw_start_ready(uint8_t owner, uint8_t destination)
{
    if (owner != destination)
        return tk_pool_can_start(&pool, owner, destination) && tk_pool_link_ready();
    uint16_t bit = (uint16_t)(1u << owner);
    if (tk_native_audio_pending(owner) > 1 &&
        (!(planned_chords & bit) || (wanted[owner] & ~pool.assigned[owner]) ||
         (wanted[owner] && !tk_pool_link_ready())))
        return false;
    return !(relay_owners & bit) || ((pool.relay_heads & bit) && tk_pool_link_ready());
}

bool tk_pool_fw_waiting_helpers(uint8_t owner)
{
    return tk_pool_fw_automatic(owner) &&
           ((wanted[owner] | pool.assigned[owner]) & ~tk_native_audio_active);
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
    if (track < TK_TRACKS) {
        guard_at[track] = now + 24u;
        planned_chords &= (uint16_t)~(1u << track);
    }
}

void tk_panel_record_audition(uint32_t track, uint32_t pitch)
{
    panel_record_audition = track < TK_TRACKS && pitch < 128 ? ((track + 1u) << 8) | pitch : 0;
}

int tk_pool_fw_block_note(const int32_t *event)
{
    if (!event || (uint32_t)event[0] >= TK_TRACKS)
        return 0;
    /* The KeyboardView's stock note action still owns the recorder and its
     * held keys. Suppress only its synchronous audition while the shared
     * keyboard owns this exact pitch. Generated dispatch runs outside it. */
    uint32_t audition = panel_record_audition;
    if (audition && (event[3] == 1 || event[3] == 2) && event[8] == 0x40 &&
        (uint32_t)event[1] < 128 &&
        audition == (((uint32_t)event[0] + 1u) << 8 | (uint32_t)event[1]))
        return 1;
    uint16_t bit = (uint16_t)(1u << event[0]);
    return (reserved & bit) || (event[3] == 1 && (preparing & bit));
}

static void scan_empty(uint32_t pattern, uint32_t selected, bool watch_targets, bool force,
                       uint32_t now)
{
    TkSequenceNativeTarget targets[TK_SEQUENCE_TARGETS] = {{0}};
    if (watch_targets)
        tk_sequence_native_targets(targets);
    uint32_t current[] = {selected, pp_current_pattern(), targets[1].address, targets[2].address};
    uint32_t kits[] = {targets[1].kit, targets[2].kit};
    bool changed = force || now - rescanned >= 120u;
    for (unsigned i = 0; i < 4; ++i) {
        changed |= scan_patterns[i] != current[i];
        scan_patterns[i] = current[i];
    }
    for (unsigned i = 0; i < 2; ++i) {
        changed |= scan_kits[i] != kits[i];
        scan_kits[i] = kits[i];
    }
    if (!changed)
        return;

    relay_head_package = relay_head_module();
    empty_tracks = 0;
    for (uint8_t track = 0; track < TK_TRACKS; ++track) {
        bool empty = empty_track(pattern, track);
        for (unsigned i = 0; empty && i < 4; ++i)
            if (current[i] && current[i] != pattern)
                empty = empty_track(current[i], track);
        for (unsigned i = 0; empty && i < 2; ++i)
            if (current[i + 2] && current[i + 2] != pattern)
                empty = tk_sequence_target_kind(kits[i], track) == TK_NATIVE_AUDIO;
        if (empty)
            empty_tracks |= (uint16_t)(1u << track);
    }
    rescanned = now;
}

static uint16_t plan_automatic(uint16_t waiting, uint16_t candidates, uint16_t quiet,
                               uint16_t reusable_helpers)
{
    const uint16_t active = (uint16_t)tk_native_audio_active;
    uint16_t requested = 0;
    planned_chords = 0;
    memset(wanted, 0, sizeof wanted);
    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
        uint16_t bit = (uint16_t)(1u << owner);
        if (!(automatic_owners & bit) || (reserved & bit))
            continue;
        uint16_t assigned = pool.assigned[owner] & eligible;
        uint16_t keep = assigned & active;
        unsigned need = waiting & bit ? tk_native_audio_pending(owner) : 0;
        if (need > 1)
            planned_chords |= bit;
        if (need && !(active & bit))
            --need; /* The first pending note uses the owner. */
        if (!(relay_owners & bit) && pool.epoch == generation) {
            /* A native sample lane can retrigger through its own tail. The
             * mix route is already on the DSP, so no quiet admission is due. */
            uint16_t warm = (assigned & ~active) | (pool.draining[owner] & pool.reusable_tails);
            warm &= reusable_helpers & ~quiet;
            while (warm && need) {
                uint16_t first = warm & (uint16_t)-warm;
                keep |= first;
                warm ^= first;
                --need;
            }
        }
        unsigned room = relay_owners & bit ? 1 : 3;
        for (uint16_t occupied = (assigned & ~quiet) | pool.draining[owner]; occupied && room;
             occupied &= occupied - 1u)
            --room;
        uint16_t idle = assigned & quiet;
        while (room && need && (idle || candidates)) {
            uint16_t available = idle ? idle : candidates;
            uint16_t candidate = available & (uint16_t)-available;
            keep |= candidate;
            idle &= (uint16_t)~candidate;
            candidates &= (uint16_t)~candidate;
            --room;
            --need;
        }
        wanted[owner] = keep;
        requested |= keep;
    }
    return requested;
}

void tk_pool_fw_update(uint32_t pattern, uint32_t selected, const TkTrackConfig config[TK_TRACKS],
                       bool stopped)
{
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
    uint16_t waiting = tk_native_audio_waiting();
    uint16_t active = (uint16_t)tk_native_audio_active;
    uint16_t dsp_quiet = tk_pool_link_quiet(&observed, &now) & ~active;
    owners = eligible = relay_owners = automatic_owners = 0;
    uint32_t capture = tk_capture_midi_mode();
    bool may_save_group = stopped && capture == TK_CAPTURE_OFF;
    if (pattern && kit) {
        scan_empty(pattern, selected, !stopped && (waiting || reserved), context_changed, now);
        uint16_t sample_tracks = 0;
        for (uint8_t track = 0; track < TK_TRACKS; ++track) {
            int type = tk_fw_track_type(kit, track);
            bool changed = machine[track] != (uint8_t)type;
            machine[track] = (uint8_t)type;
            if (type < 0 || changed || tk_fw_midi_mode(track) || tk_canopy_is_owner(track, type))
                continue;
            if (type <= 6 && type != 5)
                sample_tracks |= (uint16_t)(1u << track);
            else if (relay_head_package && TK_HAVE(rtm_is_type) && rtm_is_type((unsigned)type))
                relay_owners |= (uint16_t)(1u << track);
        }
        owners = sample_tracks | relay_owners;
        eligible = sample_tracks & empty_tracks;
    }

    uint16_t requested = 0, musical = 0;
    if (config)
        for (uint8_t owner = 0; owner < TK_TRACKS; ++owner) {
            uint16_t bit = (uint16_t)(1u << owner);
            if (!(owners & bit))
                continue;
            const TkTrackConfig *track = &config[owner];
            uint16_t helpers = track->helpers;
            if (track->params.arp || track->params.poly || helpers)
                musical |= bit;
            if (!helpers)
                automatic_owners |= bit;
            else if (track->params.poly && (may_save_group || pool.assigned[owner]) &&
                     !(helpers & ~tk_pool_fw_eligible(owner)) &&
                     (!(relay_owners & bit) || !(helpers & (helpers - 1u))))
                requested |= helpers;
        }
    for (uint8_t track = 0; track < TK_TRACKS; ++track)
        if ((reserved & (1u << track)) && (int32_t)(observed - guard_at[track]) < 0)
            dsp_quiet &= (uint16_t)~(1u << track);
    uint16_t quiet = dsp_quiet;
    if (waiting || reserved || requested)
        quiet &= (uint16_t)~queued_tracks();
    uint8_t focused = tk_midi_selected_track();
    uint16_t focus_mask = focused < TK_TRACKS ? (uint16_t)(1u << focused) : 0;
    uint16_t unavailable = requested | reserved | waiting | musical | focus_mask;
    /* Stock queue traffic can delay assignment and retirement. Keep quiet
     * preparations and leases stable while their onsets wait for the map. */
    uint16_t candidates = eligible & ~unavailable;
    if (capture == TK_CAPTURE_LIVE)
        candidates &= quiet; /* Leave tracks with stock record traffic available to the recorder. */
    else if (capture == TK_CAPTURE_OFF)
        candidates &= dsp_quiet;
    else
        candidates = 0;
    uint16_t reusable_helpers =
        capture == TK_CAPTURE_OFF || capture == TK_CAPTURE_LIVE ? eligible & ~focus_mask : 0;
    requested |= plan_automatic(waiting, candidates, dsp_quiet, reusable_helpers);

    uint16_t changed = requested & ~preparing;
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
                               .relay_heads =
                                   automatic_owners & relay_owners & (musical | waiting | active),
                               .eligible_helpers = eligible,
                               .quiescent_helpers = quiet,
                               .reusable_helpers = reusable_helpers,
                               .automatic_helpers = wanted,
                               .keep_saved_only = !may_save_group};
    tk_pool_update(&pool, &snapshot, config);
    uint16_t leased = 0;
    for (uint8_t owner = 0; owner < TK_TRACKS; ++owner)
        leased |= pool.assigned[owner] | pool.draining[owner];
    reserved = leased;
    preparing = requested;
    tk_pool_publish_frame(&pool);
}
