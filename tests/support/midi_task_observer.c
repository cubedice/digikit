/* Test-only observation before stock routing; never consumes a message. */
#include <stdint.h>
uint32_t mi_observed_count, mi_observed_overflow;
uint32_t mi_observed[32][3];
void mi_observe(const uint8_t *msg, uint32_t len, uint32_t source)
{
    uint32_t packed=0, i, n=mi_observed_count;
    if (n >= 32) { mi_observed_overflow=1; return; }
    for(i=0;i<len && i<3;++i) packed=(packed<<8)|msg[i];
    mi_observed[n][0]=source;
    mi_observed[n][1]=len;
    mi_observed[n][2]=packed;
    mi_observed_count=n+1;
}
