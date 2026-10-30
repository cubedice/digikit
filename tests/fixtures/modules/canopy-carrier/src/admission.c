/* Emulator-only DT2 1.17, track zero, WERP shadow. No cached audition source.
 * Layout follows stock track_type and its Sound getter (finding 78).
 * This is not a production V2 lifecycle or general Sound/pattern initializer. */
#include <stdint.h>
extern uint32_t cp_project(void);
extern uint32_t cp_kit(uint32_t project);
extern void cp_refresh(uint32_t source, uint32_t track);
extern uint32_t pp_current_pattern(void);
/* state, current source, refresh count, pattern, invalidations, machine type */
volatile uint32_t cp_admission[6];
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
 * Same-pointer, same-type Sound replacement needs a separate firmware event. */
int canopy_carrier_prepare(int allow_refresh)
{
    uint32_t project=0, kit=0, pattern=pp_current_pattern();
    uint32_t source=sound(&project,&kit);
    uint32_t type=source ? *(const uint8_t *)(uintptr_t)(source+0xa2) : UINT32_MAX;
    uint32_t changed=project != last_project || kit != last_kit ||
        source != last_source || pattern != last_pattern || type != last_type;
    last_project=project; last_kit=kit; last_source=source;
    last_pattern=pattern; last_type=type;
    cp_admission[1]=source;
    cp_admission[3]=pattern;
    cp_admission[5]=type;
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
