/* Host test of the automatic recovery of the note transport (control.c recover_tick): the real control code with the
 * modeled adapter of canopy-settings.c. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#undef phase
#include <assert.h>

static void ticks(int n)
{
    for (int i = 0; i < n; ++i)
        instrument_play_tick();
}

int main(void)
{
    admitted = 1;
    /* A healthy transport is left alone. */
    ticks(3 * RECOVER_TICKS);
    assert(!recover_calls && !ip_recoveries);

    /* A fault is given about a second; then the recovery is tried. With an invalid reply guard the adapter refuses and
     * the fault stays: that case needs the restart the menu names. */
    mi_transport.faults = 1;
    ticks(RECOVER_TICKS - 1);
    assert(!recover_calls);
    ticks(1);
    assert(recover_calls == 1 && !ip_recoveries && mi_transport.faults);

    /* With a valid guard the next attempt recovers and counts it. */
    recover_result = 0;
    ticks(RECOVER_TICKS);
    assert(recover_calls == 2 && ip_recoveries == 1 && !mi_transport.faults);

    /* A fault that clears by itself restarts the wait. */
    mi_transport.faults = 1;
    ticks(RECOVER_TICKS - 1);
    mi_transport.faults = 0;
    ticks(1);
    mi_transport.faults = 1;
    ticks(RECOVER_TICKS - 1);
    assert(recover_calls == 2);
    ticks(1);
    assert(recover_calls == 3 && ip_recoveries == 2);

    /* A voice fault the DSP reported for this generation is not recovered here. */
    mi_transport.faults = 1;
    fault_generation = admitted;
    ticks(2 * RECOVER_TICKS);
    assert(recover_calls == 3 && mi_transport.faults);
    fault_generation = 0;

    /* The number of recoveries per boot is bounded. */
    for (int i = 0; i < 2 * RECOVER_LIMIT; ++i) {
        mi_transport.faults = 1;
        ticks(RECOVER_TICKS);
    }
    assert(ip_recoveries == RECOVER_LIMIT && mi_transport.faults);
    return 0;
}
