/* DT2 1.17, admitted carriers on up to IP_INSTANCES kit tracks whose Sound
 * carries the instrument type. No cached audition Sound. Layout follows stock
 * track_type and its Sound getter (finding 135): kit track objects at +0x6c,
 * stride 0x3c0. Every instance keeps its own admission identity (finding 145). */
#include <stdint.h>
#include "timing.h"
#include "src-mod.h"
#include "instances.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t ip_project(void);
extern uint32_t ip_kit(uint32_t project);
extern void ip_refresh(uint32_t source, uint32_t track);
extern uint32_t pp_current_pattern(void);
extern volatile uint32_t pp_position[16];
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
volatile uint32_t ip_admission[IP_INSTANCES * 8];
volatile uint32_t ip_tracks[IP_INSTANCES] = {IP_NONE, IP_NONE, IP_NONE, IP_NONE};
volatile uint32_t ip_track = IP_NONE;
typedef struct {
    IpStartTiming timing;
    uint32_t project, kit, source, pattern, type, track;
} IpSeen;
static IpSeen seen[IP_INSTANCES];
#ifdef DK_CANOPY_SOUND_PROOF
/* Disposable emulator experiment only. The callback can allocate and is not
 * safe for production: this observes only the UI scan, after stock may have
 * replaced or copied a Sound, and has no failure or concurrency protocol. */
static void (*proof_leave)(uint32_t);
void instrument_sound_proof_set_leave(void (*callback)(uint32_t))
{
    proof_leave = callback;
}
/* Emulator-only hooks around the real in-place-load bracket. A separate test
 * wrapper understands the high-bit refusal cookie; stock's wrapper does not.
 * These callbacks are not a production failure or lifecycle protocol. */
static int (*proof_load_before)(uint32_t, uint32_t, uint32_t, uint32_t);
static int (*proof_load_after)(uint32_t, uint32_t);
void instrument_sound_proof_set_load(int (*before)(uint32_t, uint32_t, uint32_t, uint32_t),
                                     int (*after)(uint32_t, uint32_t))
{
    uint32_t sr = dkrt_irq_mask();
    proof_load_before = before;
    proof_load_after = after;
    dkrt_irq_restore(sr);
}
#endif

static int ram(uint32_t p)
{
    return p >= 0x42000000u && p < 0x48000000u && !(p & 3u);
}

static uint32_t kit_now(uint32_t *project)
{
    *project = ip_project();
    if (!ram(*project))
        return 0;
    uint32_t kit = ip_kit(*project);
    return ram(kit) ? kit : 0;
}

/* The Sound of kit track t, or zero. */
static uint32_t track_sound(uint32_t kit, uint32_t t)
{
    if (!kit || t >= 16)
        return 0;
    uint32_t track = kit + 0x6c + 0x3c0u * t;
    uint32_t table = *(const uint32_t *)(uintptr_t)track;
    if (table < 0x40000400u || table >= 0x40400000u || (table & 3u))
        return 0;
    uint32_t getter = *(const uint32_t *)(uintptr_t)(table + 0x28);
    if (getter < 0x40000400u || getter >= 0x40400000u || (getter & 1u))
        return 0;
    uint32_t source = ((uint32_t (*)(uint32_t))(uintptr_t)getter)(track);
    return ram(source) ? source : 0;
}

/* A scheduled destination pattern is not yet the sounding identity: do not
 * call its getters until the committed boundary catches up. */
static int switching(void)
{
    uint32_t sr = dkrt_irq_mask();
    uint32_t pattern = pp_current_pattern(), running = pp_position[14] == 1;
    uint32_t flags = pp_position[5], committed = pp_position[6];
    dkrt_irq_restore(sr);
    return running && (flags & 3) == 3 && committed && pattern != committed;
}

/* Once per UI tick. An instance keeps its track while that track still
 * carries the instrument among the lowest IP_INSTANCES such tracks; freed
 * instances take newly qualifying tracks, lowest track first, keeping their
 * sound settings. In-place loads and pattern hand-overs keep the old set. */
void instrument_sound_scan(void)
{
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (ip_admission[i * 8 + 6])
            return;
    if (switching())
        return;
    uint32_t project = 0, kit = kit_now(&project), type = dkrt_instrument_type();
    uint32_t want = 0, count = 0;
    for (uint32_t t = 0; type && kit && t < 16 && count < IP_INSTANCES; ++t) {
        uint32_t source = track_sound(kit, t);
        if (source && *(const uint8_t *)(uintptr_t)(source + 0xa2) == type) {
            want |= 1u << t;
            ++count;
        }
    }
#ifdef DK_CANOPY_SOUND_PROOF
    if (proof_leave)
        for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
            uint32_t t = ip_tracks[i], old = ip_admission[i * 8 + 1];
            if (ip_admission[i * 8] && old && t < 16 &&
                (!(want & (1u << t)) || track_sound(kit, t) != old))
                proof_leave(old);
        }
#endif
    uint32_t sr = dkrt_irq_mask();
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        uint32_t t = ip_tracks[i];
        if (t < 16 && (want & (1u << t)))
            want &= ~(1u << t);
        else
            ip_tracks[i] = IP_NONE;
    }
    for (uint32_t t = 0; t < 16 && want; ++t) {
        if (!(want & (1u << t)))
            continue;
        for (uint32_t i = 0; i < IP_INSTANCES; ++i)
            if (ip_tracks[i] == IP_NONE) {
                ip_tracks[i] = t;
                break;
            }
        want &= ~(1u << t);
    }
    uint32_t lowest = IP_NONE;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (ip_tracks[i] < lowest)
            lowest = ip_tracks[i];
    ip_track = lowest;
    dkrt_irq_restore(sr);
}

uint32_t instrument_sound_tune(uint32_t instance)
{
    uint32_t project = 0;
    if (instance >= IP_INSTANCES)
        return 0x4000;
    uint32_t source = track_sound(kit_now(&project), ip_tracks[instance]);
    uint32_t type = dkrt_instrument_type();
    if (!source || !type || *(const uint8_t *)(uintptr_t)(source + 0xa2) != type)
        return 0x4000;
    /* DT2 1.17 parameter 0xca: native Sound slot25, signed 8.8 semitones
     * biased by 0x4000. Read this instance's Sound, never the selected UI track. */
    uint32_t raw = *(const uint16_t *)(uintptr_t)(source + 0x14 + 25 * 2);
    return raw >= 0x400 && raw <= 0x7c00 ? raw : 0x4000;
}

int instrument_sound_admitted(uint32_t track)
{
    uint32_t type = dkrt_instrument_type();
    if (track >= 16 || !type)
        return 0;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (ip_tracks[i] == track && ip_admission[i * 8] && ip_admission[i * 8 + 5] == type)
            return 1;
    return 0;
}

/* -1: close routes/reset before another refresh; 0: wait; 1: prepared.
 * Poll even while configuration is in flight. Never refresh in an old generation.
 * The deserializer hook brackets in-place loads of the tracked Sound. */
int instrument_sound_prepare(uint32_t instance, int allow_refresh)
{
    if (instance >= IP_INSTANCES)
        return 0;
    volatile uint32_t *a = &ip_admission[instance * 8];
    IpSeen *last = &seen[instance];
    if (a[6])
        return 0;
    uint32_t sr = dkrt_irq_mask();
    uint32_t pattern = pp_current_pattern(), running = pp_position[14] == 1;
    uint32_t flags = pp_position[5], committed = pp_position[6], block = pp_position[12];
    dkrt_irq_restore(sr);
    /* A scheduled destination is not yet the sounding identity. Do not even
     * call its getter until the committed boundary catches up. */
    if (running && (flags & 3) == 3 && committed && pattern != committed) {
        (void)ip_start_hold(&last->timing, running, 1, block, 0);
        return a[0] ? 1 : 0;
    }
    uint32_t project = 0, kit = kit_now(&project), track = ip_tracks[instance];
    uint32_t source = track_sound(kit, track);
    uint32_t type = source ? *(const uint8_t *)(uintptr_t)(source + 0xa2) : UINT32_MAX;
    uint32_t same = a[0] && project == last->project && kit == last->kit &&
                    source == last->source && pattern == last->pattern && type == last->type &&
                    track == last->track && type == dkrt_instrument_type();
    int hold = ip_start_hold(&last->timing, running, (flags & 3) == 3 && committed, block, same);
    if (running) {
        if ((flags & 3) != 3 || !committed) {
            /* START can precede its first committed boundary. Preserve only
             * the already verified identity, for a bounded observation window. */
            if (hold)
                return 1;
            /* Unknown timing cannot authorize a new generation. */
            if (a[0]) {
                a[0] = 0;
                ++a[4];
                return -1;
            }
            return 0;
        }
    }
    uint32_t changed = project != last->project || kit != last->kit || source != last->source ||
                       pattern != last->pattern || type != last->type || track != last->track;
    /* A committed switch within this project retains the instance when the
     * destination keeps the instrument on the same track. Refresh the
     * destination stock carrier, but do not reset the MIDI ledger,
     * configuration, oscillators or controllers. Same-pattern Sound loads still
     * invalidate through the deserializer hook; project/machine/track changes
     * reset. One fixed configuration and one admitted dynamic type. */
    uint32_t retain = a[0] && pattern && last->pattern && pattern != last->pattern &&
                      project == last->project && source && kit && type == dkrt_instrument_type() &&
                      type == last->type && track == last->track;
    last->project = project;
    last->kit = kit;
    last->source = source;
    last->pattern = pattern;
    last->type = type;
    last->track = track;
    a[1] = source;
    a[3] = pattern;
    a[5] = type;
    if (retain) {
        instrument_mod_defaults(source);
        ip_refresh(source, track);
        ++a[2];
        return 1;
    }
    if (changed && a[0]) {
        a[0] = 0;
        ++a[4];
        return -1;
    }
    if (a[0])
        return 1;
    if (!allow_refresh || !source || (type == 0 || type != dkrt_instrument_type()) || !pattern ||
        pp_current_pattern() != pattern)
        return 0;
    instrument_mod_defaults(source);
    ip_refresh(source, track);
    ++a[2];
    a[0] = 1;
    return 1;
}

/* Called before the stock deserializer writes the destination. No Sound data is
 * copied here. An unrelated destination cannot reset any instance. The cookie
 * (a mask of matched instances) pairs end with begin even if identities change. */
uint32_t ip_sound_begin(uint32_t destination, uint32_t record)
{
    uint32_t sr = dkrt_irq_mask(), cookie = 0;
    if (destination && record && *(const uint32_t *)(uintptr_t)(record + 4) == 3) {
#ifdef DK_CANOPY_SOUND_PROOF
        if (proof_load_before) {
            uint32_t match = IP_INSTANCES;
            for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
                volatile uint32_t *a = &ip_admission[i * 8];
                if (destination != a[1])
                    continue;
                if (a[6] || match != IP_INSTANCES) {
                    dkrt_irq_restore(sr);
                    return 0x80000000u;
                }
                if (a[0])
                    match = i;
            }
            if (match != IP_INSTANCES &&
                proof_load_before(match, destination, record, ip_admission[match * 8 + 4])) {
                dkrt_irq_restore(sr);
                return 0x80000000u;
            }
        }
#endif
        for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
            volatile uint32_t *a = &ip_admission[i * 8];
            if (destination != a[1])
                continue;
            ++a[6];
            ++a[7];
            cookie |= 1u << i;
            if (a[0]) {
                a[0] = 0;
                ++a[4];
                instrument_sound_suspend(i);
            }
        }
    }
    dkrt_irq_restore(sr);
    return cookie;
}

void ip_sound_end(uint32_t cookie)
{
    uint32_t sr = dkrt_irq_mask();
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if ((cookie & (1u << i)) && ip_admission[i * 8 + 6])
            --ip_admission[i * 8 + 6];
    dkrt_irq_restore(sr);
}

#ifdef DK_CANOPY_SOUND_PROOF
/* Used only by the proof wrapper which supplies the actual stock result.
 * A failed after-callback leaves the existing loading guard in place. */
void ip_sound_proof_end(uint32_t cookie, uint32_t result)
{
    uint32_t sr = dkrt_irq_mask();
    if (proof_load_after && cookie && proof_load_after(cookie, result)) {
        dkrt_irq_restore(sr);
        return;
    }
    ip_sound_end(cookie);
    dkrt_irq_restore(sr);
}
#endif
