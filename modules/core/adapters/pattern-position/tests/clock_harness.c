#include <assert.h>
#define PP_QUEUE_TEST
#include "../src/cf/boundary.c"
int main(void)
{
    assert(pp_period(0, 0) == 2700000);
    assert(pp_period(0, 1) == 3600000);
    assert(pp_period(0, 2) == 5400000);
    assert(pp_period(0, 3) == 7200000);
    assert(pp_period(0, 4) == 10800000);
    assert(pp_period(0, 5) == 21600000);
    assert(pp_period(0, 6) == 43200000);
    assert(pp_period(0, 7) == 0);
    assert(pp_period(1, 0) == 5400000);
    assert(pp_period(1, 6) == 5400000);
    pp_observe_scaled(1, 100, 0x41000000, 0, 16, 10800000);
    assert(committed_period == 0);
    pp_commit(99, 7, 10);
    assert(committed_period == 0);
    pp_commit(100, 7, 11);
    assert(committed_period == 10800000);
    pp_observe_scaled(0, 200, 0x42000000, 0, 32, 2700000);
    assert(committed_period == 10800000);
    assert(pp_position[6] == 0x41000000);
    pp_commit(200, 5, 12);
    assert(committed_period == 2700000);
    assert(pp_position[6] == 0x42000000);
    pp_observe_scaled(0, 300, 0x42000000, 1, 32, 7200000);
    pp_commit(301, 5, 13);
    assert(!valid);
    assert(committed_period == 2700000);
    return 0;
}
