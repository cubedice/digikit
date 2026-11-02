#include "src-clock.h"
uint32_t cc_milliseconds(uint32_t high,uint32_t low,uint32_t previous,uint32_t counter)
{
    uint32_t delta=counter-previous, next=low+delta;
    high+=next<low;
    /* Bounded unsigned long division, avoiding a freestanding 64-bit runtime
     * import. Firmware itself divides this timer by 132000000 for seconds.
     * Keep the low 32 quotient bits for wrap-safe UI duration subtraction. */
    uint32_t remainder=0,quotient=0;
    for(uint32_t i=0;i<64;++i) {
        uint32_t bit=i<32 ? (high>>(31-i))&1u : (next>>(63-i))&1u;
        remainder=(remainder<<1)|bit;
        quotient<<=1;
        if(remainder>=132000) { remainder-=132000; quotient|=1; }
    }
    return quotient;
}
#if defined(__m68k__)
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
uint32_t instrument_src_now(void)
{
    /* DT2 OS1.17: 400cc8b2 extends DTCN0 into these two words; 400cc970
     * divides them by 132MHz. Read atomically, include unaccumulated time,
     * and never alter the firmware's accumulator or timer configuration. */
    uint32_t sr=dkrt_irq_mask();
    uint32_t high=*(volatile uint32_t *)0x42940a4c;
    uint32_t low=*(volatile uint32_t *)0x42940a50;
    uint32_t previous=*(volatile uint32_t *)0x42940a54;
    uint32_t counter=*(volatile uint32_t *)0xfc07000c;
    dkrt_irq_restore(sr);
    return cc_milliseconds(high,low,previous,counter);
}
#endif
