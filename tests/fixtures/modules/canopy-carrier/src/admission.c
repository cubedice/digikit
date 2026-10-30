/* Emulator-only DT2 1.17, track zero, WERP shadow. No cached audition source.
 * Layout follows stock track_type and its Sound getter (finding 78).
 * This is not a production V2 lifecycle or general Sound/pattern initializer. */
#include <stdint.h>
extern uint32_t cp_project(void);
extern uint32_t cp_kit(uint32_t project);
extern void cp_refresh(uint32_t source, uint32_t track);
extern uint32_t pp_current_pattern(void);
extern volatile uint32_t pp_position[16];
/* state, current source, refresh count, pattern, invalidations, machine type */
volatile uint32_t cp_admission[8];
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern void canopy_carrier_suspend(void);
/* [6] active in-place loads; [7] observed loads of the tracked Sound. */
static uint32_t last_project, last_kit, last_source, last_pattern, last_type;

static int ram(uint32_t p)
{
    return p >= 0x42000000u && p < 0x48000000u && !(p & 3u);
}

static uint32_t sound(uint32_t *project, uint32_t *kit)
{
    uint32_t track, table, getter, source;
    *project=cp_project();
    if (!ram(*project)) return 0;
    *kit=cp_kit(*project);
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
int canopy_carrier_prepare(int allow_refresh)
{
    if (cp_admission[6]) return 0;
    uint32_t sr=dkrt_irq_mask();
    uint32_t pattern=pp_current_pattern(), running=pp_position[14] == 1;
    uint32_t flags=pp_position[5], committed=pp_position[6];
    dkrt_irq_restore(sr);
    if (running) {
        if ((flags & 3) != 3 || !committed) {
            /* Unknown timing cannot authorize a new generation. */
            if (cp_admission[0]) {
                cp_admission[0]=0;
                ++cp_admission[4];
                return -1;
            }
            return 0;
        }
        /* The scheduler can expose destination Sound/kit data before playback.
         * Keep the source ownership; never refresh from those early pointers.
         * An in-place loader can still close admission first for safety. */
        if (pattern != committed) return cp_admission[0] ? 1 : 0;
    }
    uint32_t project=0, kit=0;
    uint32_t source=sound(&project,&kit);
    uint32_t type=source ? *(const uint8_t *)(uintptr_t)(source+0xa2) : UINT32_MAX;
    uint32_t changed=project != last_project || kit != last_kit ||
        source != last_source || pattern != last_pattern || type != last_type;
    /* A committed switch within this project retains the instrument instance.
     * Refresh the destination stock carrier, but do not reset the MIDI ledger,
     * configuration, oscillators or controllers. Same-pattern Sound loads still
     * invalidate through the deserializer hook; project/machine changes reset.
     * This fixture has one fixed configuration and shadows type 1 only. */
    uint32_t retain=cp_admission[0] && pattern && last_pattern &&
        pattern != last_pattern && project == last_project &&
        source && kit && type == 1 && last_type == 1;
    last_project=project; last_kit=kit; last_source=source;
    last_pattern=pattern; last_type=type;
    cp_admission[1]=source;
    cp_admission[3]=pattern;
    cp_admission[5]=type;
    if (retain) {
        cp_refresh(source,0);
        ++cp_admission[2];
        return 1;
    }
    if (changed && cp_admission[0]) {
        cp_admission[0]=0;
        ++cp_admission[4];
        return -1;
    }
    if (cp_admission[0]) return 1;
    if (!allow_refresh || !source || type != 1 || !pattern ||
        pp_current_pattern() != pattern) return 0;
    cp_refresh(source,0);
    ++cp_admission[2];
    cp_admission[0]=1;
    return 1;
}

/* Called before the stock deserializer writes the destination. No Sound data is
 * copied here. An unrelated destination cannot reset this track. The cookie
 * pairs end with begin even if selected identity changes during the load. */
uint32_t cp_sound_begin(uint32_t destination, uint32_t record)
{
    uint32_t sr=dkrt_irq_mask(), cookie=0;
    if (destination && record && destination == cp_admission[1] &&
        *(const uint32_t *)(uintptr_t)(record+4) == 3) {
        ++cp_admission[6];
        ++cp_admission[7];
        cookie=1;
        if (cp_admission[0]) {
            cp_admission[0]=0;
            ++cp_admission[4];
            canopy_carrier_suspend();
        }
    }
    dkrt_irq_restore(sr);
    return cookie;
}

void cp_sound_end(uint32_t cookie)
{
    uint32_t sr=dkrt_irq_mask();
    if (cookie && cp_admission[6]) --cp_admission[6];
    dkrt_irq_restore(sr);
}
