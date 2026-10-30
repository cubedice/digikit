/* Emulator-only DT2 1.17, track zero, WERP shadow. No cached audition source.
 * Layout follows stock track_type and its Sound getter (finding 78).
 * This is not a production V2 lifecycle or general Sound/pattern initializer. */
#include <stdint.h>
extern uint32_t cp_project(void);
extern uint32_t cp_kit(uint32_t project);
extern void cp_refresh(uint32_t source, uint32_t track);
volatile uint32_t cp_admission[3]; /* state, resolved source, refresh count */

static int ram(uint32_t p)
{
    return p >= 0x42000000u && p < 0x48000000u && !(p & 3u);
}

int canopy_carrier_prepare(void)
{
    uint32_t project, kit, track, table, getter, source;
    if (cp_admission[0] == 1) return 1;
    project=cp_project();
    if (!ram(project)) return 0;
    kit=cp_kit(project);
    if (!ram(kit)) return 0;
    track=kit+0x6c; /* track 0; stock stride for other tracks is 0x3c0 */
    table=*(const uint32_t *)(uintptr_t)track;
    if (table < 0x40000400u || table >= 0x40400000u || (table & 3u)) return 0;
    getter=*(const uint32_t *)(uintptr_t)(table+0x28);
    if (getter < 0x40000400u || getter >= 0x40400000u || (getter & 1u)) return 0;
    source=((uint32_t (*)(uint32_t))(uintptr_t)getter)(track);
    if (!ram(source) || *(const uint8_t *)(uintptr_t)(source+0xa2) != 1) return 0;
    cp_admission[1]=source;
    cp_refresh(source,0);
    ++cp_admission[2];
    cp_admission[0]=1;
    return 1;
}
