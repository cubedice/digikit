#include <stdio.h>
#include "../../../runtime/platform/instrument-play/src/cf/timing.h"
int main(void)
{
    IpStartTiming state = {0};
    unsigned run, valid, block, same;
    while (scanf("%u %u %u %u", &run, &valid, &block, &same) == 4)
        printf("%d\n", ip_start_hold(&state, run, valid, block, same));
    return 0;
}
