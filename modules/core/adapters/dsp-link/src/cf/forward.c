#include <stdint.h>

extern const uint16_t *rtm_gate_frame(const uint16_t *) __attribute__((weak));
extern void dl_stock_send(int, const uint16_t *, int, const volatile uint16_t *);
void dl_forward_frame(int len, const uint16_t *src, int reclen, const volatile uint16_t *rec)
{
    if (rtm_gate_frame && len == 0x802 && reclen == 0xabc)
        src = rtm_gate_frame(src);
    dl_stock_send(len, src, reclen, rec);
}
