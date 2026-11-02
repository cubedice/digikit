/* DT2 1.17, admitted track-zero carrier. No cached audition Sound.
 * Layout follows stock track_type and its Sound getter (finding 78). */
#include <stdint.h>
#include "timing.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t ip_project(void);
extern uint32_t ip_kit(uint32_t project);
extern void ip_refresh(uint32_t source, uint32_t track);
extern uint32_t pp_current_pattern(void);
extern volatile uint32_t pp_position[16];
/* state, current source, refresh count, pattern, invalidations, machine type */
volatile uint32_t ip_admission[8];
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern void instrument_sound_suspend(void);
/* [6] active in-place loads; [7] observed loads of the tracked Sound. */
static IpStartTiming start_timing;
static uint32_t last_project, last_kit, last_source, last_pattern, last_type;

static int ram(uint32_t p)
{
    return p >= 0x42000000u && p < 0x48000000u && !(p & 3u);
}

static uint32_t sound(uint32_t *project, uint32_t *kit)
{
    uint32_t track, table, getter, source;
    *project=ip_project();
    if (!ram(*project)) return 0;
    *kit=ip_kit(*project);
    if (!ram(*kit)) return 0;
    track=*kit+0x6c; /* track 0; stock stride for other tracks is 0x3c0 */
    table=*(const uint32_t *)(uintptr_t)track;
    if (table < 0x40000400u || table >= 0x40400000u || (table & 3u)) return 0;
    getter=*(const uint32_t *)(uintptr_t)(table+0x28);
    if (getter < 0x40000400u || getter >= 0x40400000u || (getter & 1u)) return 0;
    source=((uint32_t (*)(uint32_t))(uintptr_t)getter)(track);
    return ram(source) ? source : 0;
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
    uint32_t project=0, kit=0;
    uint32_t source=sound(&project,&kit);
    uint32_t type=source ? *(const uint8_t *)(uintptr_t)(source+0xa2) : UINT32_MAX;
    uint32_t same=ip_admission[0] && project == last_project && kit == last_kit &&
        source == last_source && pattern == last_pattern && type == last_type &&
        type == dkrt_instrument_type();
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
        source != last_source || pattern != last_pattern || type != last_type;
    /* A committed switch within this project retains the instrument instance.
     * Refresh the destination stock carrier, but do not reset the MIDI ledger,
     * configuration, oscillators or controllers. Same-pattern Sound loads still
     * invalidate through the deserializer hook; project/machine changes reset.
     * Stage 1 has one fixed configuration and one admitted dynamic type. */
    uint32_t retain=ip_admission[0] && pattern && last_pattern &&
        pattern != last_pattern && project == last_project &&
        source && kit && type == dkrt_instrument_type() && type == last_type;
    last_project=project; last_kit=kit; last_source=source;
    last_pattern=pattern; last_type=type;
    ip_admission[1]=source;
    ip_admission[3]=pattern;
    ip_admission[5]=type;
    if (retain) {
        ip_refresh(source,0);
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
    ip_refresh(source,0);
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
