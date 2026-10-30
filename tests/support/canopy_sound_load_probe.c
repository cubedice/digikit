/* Synthetic RAM fixtures around the real DT2 deserializer and CANOPY wrapper. */
#include <stdint.h>
extern volatile uint32_t cp_admission[8];
extern uint32_t cp_sound_entry(uint32_t, uint32_t);
extern uint32_t cp_sound_begin(uint32_t, uint32_t);
extern void cp_sound_end(uint32_t);
extern int canopy_carrier_prepare(int);
static uint32_t sound[1024], other[1024], record[280];
static uint32_t resets, old_marker, blocked;
uint32_t cp_project(void) { return 0; }
uint32_t cp_kit(uint32_t p) { (void)p; return 0; }
uint32_t pp_current_pattern(void) { return 0; }
void cp_refresh(uint32_t s, uint32_t t) { (void)s; (void)t; }
uint32_t dkrt_irq_mask(void) { return 0x2700; }
void dkrt_irq_restore(uint32_t sr) { (void)sr; }
void canopy_carrier_suspend(void)
{
    ++resets;
    old_marker=sound[0];
    blocked=canopy_carrier_prepare(1) == 0;
}
uint32_t cp_sound_case(uint32_t kind)
{
    for (uint32_t i=0;i<1024;++i) sound[i]=other[i]=0;
    for (uint32_t i=0;i<280;++i) record[i]=0;
    for (uint32_t i=0;i<8;++i) cp_admission[i]=0;
    resets=old_marker=blocked=0;
    sound[0]=0x12345678;
    record[1]=3; record[2]=0x23456789;
    ((uint8_t *)record)[0xd2]=1;
    ((uint8_t *)sound)[0xa2]=1; /* Same pointer AND same machine type. */
    cp_admission[0]=1;
    cp_admission[1]=(uint32_t)sound;
    if (kind == 3) {
        uint32_t a=cp_sound_begin((uint32_t)sound,(uint32_t)record);
        uint32_t b=cp_sound_begin((uint32_t)sound,(uint32_t)record);
        if (!a || !b || resets!=1 || cp_admission[6]!=2) return 1;
        cp_sound_end(a);
        if (cp_admission[6]!=1 || canopy_carrier_prepare(1)!=0) return 2;
        cp_sound_end(b);
        cp_sound_end(0);
        return cp_admission[6] || cp_admission[7]!=2;
    }
    if (kind == 2) record[1]=0; /* Stock rejects this format before writing. */
    uint32_t result=cp_sound_entry((uint32_t)(kind==1 ? other : sound),(uint32_t)record);
    if (kind == 0) {
        if ((result&255)!=1 || sound[0]!=record[2] || ((uint8_t *)sound)[0xa2]!=1) return 3;
        if (resets!=1 || old_marker!=0x12345678 || !blocked) return 4;
        if (cp_admission[0] || cp_admission[4]!=1 || cp_admission[6] || cp_admission[7]!=1) return 5;
    } else {
        if ((result&255)!=(kind==1)) return 600+(result&255)*10+kind;
        if (sound[0]!=0x12345678) return 6;
        if (resets || cp_admission[0]!=1 || cp_admission[6] || cp_admission[7]) return 7;
        if (kind==1 && other[0]!=record[2]) return 8;
    }
    return 0;
}
