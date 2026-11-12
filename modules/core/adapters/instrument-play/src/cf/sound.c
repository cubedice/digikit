/* DT2 1.17, admitted carrier on the lowest kit track whose Sound carries the
 * instrument type. No cached audition Sound. Layout follows stock track_type
 * and its Sound getter (finding 78): kit track objects at +0x6c, stride 0x3c0. */
#include <stdint.h>
#include "timing.h"
#include "src-mod.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t ip_project(void);
extern uint32_t ip_kit(uint32_t project);
extern void ip_refresh(uint32_t source, uint32_t track);
extern uint32_t pp_current_pattern(void);
extern volatile uint32_t pp_position[16];
/* state, current source, refresh count, pattern, invalidations, machine type */
volatile uint32_t ip_admission[8];
/* The track holding the instrument Sound (0..15), or 0xffffffff for none.
 * Refreshed by every prepare scan; UI ownership follows it before admission. */
volatile uint32_t ip_track=0xffffffffu;
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern void instrument_sound_suspend(void);
/* [6] active in-place loads; [7] observed loads of the tracked Sound. */
static IpStartTiming start_timing;
static uint32_t last_project, last_kit, last_source, last_pattern, last_type, last_track;

static int ram(uint32_t p)
{
    return p >= 0x42000000u && p < 0x48000000u && !(p & 3u);
}

/* The first track whose Sound byte +0xa2 is the instrument type. Returns that
 * Sound and its track; zero when no track carries the instrument. */
static uint32_t sound(uint32_t *project, uint32_t *kit, uint32_t *track_out)
{
    uint32_t t, track, table, getter, source, type=dkrt_instrument_type();
    *track_out=0xffffffffu;
    *project=ip_project();
    if (!ram(*project)) return 0;
    *kit=ip_kit(*project);
    if (!ram(*kit)) return 0;
    if (!type) return 0;
    for (t=0; t<16; ++t) {
        track=*kit+0x6c+0x3c0u*t;
        table=*(const uint32_t *)(uintptr_t)track;
        if (table < 0x40000400u || table >= 0x40400000u || (table & 3u)) continue;
        getter=*(const uint32_t *)(uintptr_t)(table+0x28);
        if (getter < 0x40000400u || getter >= 0x40400000u || (getter & 1u)) continue;
        source=((uint32_t (*)(uint32_t))(uintptr_t)getter)(track);
        if (!ram(source) || *(const uint8_t *)(uintptr_t)(source+0xa2)!=type) continue;
        *track_out=t;
        return source;
    }
    return 0;
}

uint32_t instrument_sound_tune(void)
{
    uint32_t project=0,kit=0,track=0,source=sound(&project,&kit,&track);
    if(!source) return 0x4000;
    /* DT2 1.17 parameter 0xca: native Sound slot25, signed 8.8 semitones
     * biased by 0x4000. Read the current Sound, never the selected UI track. */
    uint32_t raw=*(const uint16_t *)(uintptr_t)(source+0x14+25*2);
    return raw>=0x400 && raw<=0x7c00 ? raw : 0x4000;
}

/* -1: close routes/reset before another refresh; 0: wait; 1: prepared.
 * Poll even while configuration is in flight. Never refresh in an old generation.
 * The deserializer hook brackets in-place loads of the tracked Sound. */
int instrument_sound_prepare(int allow_refresh)
{
    if (ip_admission[6]) return 0;
    uint32_t sr=dkrt_irq_mask();
    uint32_t pattern=pp_current_pattern(), running=pp_position[14] == 1;
    uint32_t flags=pp_position[5], committed=pp_position[6], block=pp_position[12];
    dkrt_irq_restore(sr);
    /* A scheduled destination is not yet the sounding identity. Do not even
     * call its getter until the committed boundary catches up. */
    if (running && (flags & 3) == 3 && committed && pattern != committed) {
        (void)ip_start_hold(&start_timing,running,1,block,0);
        return ip_admission[0] ? 1 : 0;
    }
    uint32_t project=0, kit=0, track=0;
    uint32_t source=sound(&project,&kit,&track);
    uint32_t type=source ? *(const uint8_t *)(uintptr_t)(source+0xa2) : UINT32_MAX;
    ip_track=track;
    uint32_t same=ip_admission[0] && project == last_project && kit == last_kit &&
        source == last_source && pattern == last_pattern && type == last_type &&
        track == last_track && type == dkrt_instrument_type();
    int hold=ip_start_hold(&start_timing,running,(flags & 3) == 3 && committed,block,same);
    if (running) {
        if ((flags & 3) != 3 || !committed) {
            /* START can precede its first committed boundary. Preserve only
             * the already verified identity, for a bounded observation window. */
            if (hold) return 1;
            /* Unknown timing cannot authorize a new generation. */
            if (ip_admission[0]) {
                ip_admission[0]=0;
                ++ip_admission[4];
                return -1;
            }
            return 0;
        }
    }
    uint32_t changed=project != last_project || kit != last_kit ||
        source != last_source || pattern != last_pattern || type != last_type ||
        track != last_track;
    /* A committed switch within this project retains the instrument instance
     * when the destination keeps the instrument on the same track. Refresh the
     * destination stock carrier, but do not reset the MIDI ledger,
     * configuration, oscillators or controllers. Same-pattern Sound loads still
     * invalidate through the deserializer hook; project/machine/track changes
     * reset. One fixed configuration and one admitted dynamic type. */
    uint32_t retain=ip_admission[0] && pattern && last_pattern &&
        pattern != last_pattern && project == last_project &&
        source && kit && type == dkrt_instrument_type() && type == last_type &&
        track == last_track;
    last_project=project; last_kit=kit; last_source=source;
    last_pattern=pattern; last_type=type; last_track=track;
    ip_admission[1]=source;
    ip_admission[3]=pattern;
    ip_admission[5]=type;
    if (retain) {
        instrument_mod_defaults(source);
        ip_refresh(source,track);
        ++ip_admission[2];
        return 1;
    }
    if (changed && ip_admission[0]) {
        ip_admission[0]=0;
        ++ip_admission[4];
        return -1;
    }
    if (ip_admission[0]) return 1;
    if (!allow_refresh || !source || (type == 0 || type != dkrt_instrument_type()) || !pattern ||
        pp_current_pattern() != pattern) return 0;
    instrument_mod_defaults(source);
    ip_refresh(source,track);
    ++ip_admission[2];
    ip_admission[0]=1;
    return 1;
}

/* Called before the stock deserializer writes the destination. No Sound data is
 * copied here. An unrelated destination cannot reset this track. The cookie
 * pairs end with begin even if selected identity changes during the load. */
uint32_t ip_sound_begin(uint32_t destination, uint32_t record)
{
    uint32_t sr=dkrt_irq_mask(), cookie=0;
    if (destination && record && destination == ip_admission[1] &&
        *(const uint32_t *)(uintptr_t)(record+4) == 3) {
        ++ip_admission[6];
        ++ip_admission[7];
        cookie=1;
        if (ip_admission[0]) {
            ip_admission[0]=0;
            ++ip_admission[4];
            instrument_sound_suspend();
        }
    }
    dkrt_irq_restore(sr);
    return cookie;
}

void ip_sound_end(uint32_t cookie)
{
    uint32_t sr=dkrt_irq_mask();
    if (cookie && ip_admission[6]) --ip_admission[6];
    dkrt_irq_restore(sr);
}
