/* Stock USB byte-parser component. Only allocation, queue push and wakeup are
 * replaced; no task scheduling or USB hardware delivery is claimed. */
#include <stdint.h>
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
extern void mi_stock_usb_receive(const uint8_t *, uint32_t);
static uint8_t blocks[32][4], messages[32][4];
static uint32_t allocated, queued, wakes, fault;
void *memcpy(void *dst, const void *src, unsigned n)
{
    volatile uint8_t *p=dst;
    const volatile uint8_t *q=src;
    while(n--) *p++=*q++;
    return dst;
}

void *mi_parser_alloc(void)
{
    if (allocated >= 32) { fault=1; return blocks[31]; }
    return blocks[allocated++];
}
void mi_parser_push(uint32_t queue, const uint8_t *message)
{
    uint32_t i;
    if (queue != 0x12345678 || queued >= 32) { fault=1; return; }
    for (i=0; i<4; ++i) messages[queued][i]=message[i];
    ++queued;
}
void mi_parser_wake(uint32_t semaphore)
{
    if (semaphore != 0x23456789) fault=1;
    ++wakes;
}

uint32_t mi_parser_case(uint32_t which)
{
    uint32_t i,j;
    uint8_t split[] = {0x90,60,100};
    uint8_t running[] = {0x90,60,100,64,80,0x80,60,0};
    uint8_t realtime[] = {0x90,60,0xf8,100,64,0};
    uint8_t incomplete[] = {0x90,60};
    uint8_t replacement[] = {0x80,60,0};
    uint8_t channel[] = {0x9f,127,127,0xbf,64,127,0xef,0,64};
    allocated=queued=wakes=fault=0;
    for (i=0;i<32;++i) for (j=0;j<4;++j) blocks[i][j]=messages[i][j]=0;
    if (which == 0) {
        mi_stock_usb_receive(split,1); CHECK(queued == 0);
        mi_stock_usb_receive(split+1,1); CHECK(queued == 0);
        mi_stock_usb_receive(split+2,1); CHECK(queued == 1);
        CHECK(messages[0][0] == 0x90 && messages[0][1] == 60 && messages[0][2] == 100);
    } else if (which == 1) {
        mi_stock_usb_receive(running,sizeof(running)); CHECK(queued == 3);
        CHECK(messages[0][0] == 0x90 && messages[0][1] == 60 && messages[0][2] == 100);
        CHECK(messages[1][0] == 0x90 && messages[1][1] == 64 && messages[1][2] == 80);
        CHECK(messages[2][0] == 0x80 && messages[2][1] == 60 && messages[2][2] == 0);
    } else if (which == 2) {
        mi_stock_usb_receive(realtime,sizeof(realtime)); CHECK(queued == 3);
        CHECK(messages[0][0] == 0xf8 && messages[0][3] == 1);
        CHECK(messages[1][0] == 0x90 && messages[1][1] == 60 && messages[1][2] == 100);
        CHECK(messages[2][0] == 0x90 && messages[2][1] == 64 && messages[2][2] == 0);
    } else if (which == 3) {
        mi_stock_usb_receive(incomplete,sizeof(incomplete)); CHECK(queued == 0);
        mi_stock_usb_receive(replacement,sizeof(replacement)); CHECK(queued == 1);
        CHECK(messages[0][0] == 0x80 && messages[0][1] == 60 && messages[0][2] == 0);
    } else {
        mi_stock_usb_receive(channel,sizeof(channel)); CHECK(queued == 3);
        for(i=0;i<3;++i) for(j=0;j<3;++j) CHECK(messages[i][j] == channel[3*i+j]);
    }
    for (i=0;i<queued;++i) CHECK(messages[i][3] == ((which == 2 && i == 0) ? 1 : 3));
    CHECK(!fault && wakes == queued);
    return 0;
}
