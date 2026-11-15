/* DISPOSABLE EMULATOR ONLY. A synthetic LZ4 frame carrying one byte is
 * dispatched on DigiKit's worker task after forcing reservation failure. It is not a
 * captured transport frame and does not prove task identity or hardware safety. */
#include <stdint.h>
extern int32_t dkrt_job_post(uintptr_t,uintptr_t,uint8_t *);
extern volatile uint32_t canopy_load_guard[];
volatile uint32_t canopy_memory_test[14];
volatile uint32_t canopy_memory_target;
volatile uint32_t canopy_memory_cleanup_armed,canopy_memory_cleanup_count,canopy_memory_cleanup_ptr;
static uint32_t header[6];
/* LZ4 frame, independent 64 KiB blocks, one uncompressed byte, end marker. */
static uint8_t input[]={0x04,0x22,0x4d,0x18,0x60,0x40,0x82,
    0x01,0x00,0x00,0x80,0x42,0x00,0x00,0x00,0x00};

static uint32_t sound_hash(uint32_t address)
{
    volatile const uint8_t *sound=(volatile const uint8_t *)(uintptr_t)address;
    uint32_t hash=2166136261u;
    for(uint32_t i=0;i<0x450;i++) hash=(hash^sound[i])*16777619u;
    return hash;
}

static int32_t step(uint8_t *unused)
{
    (void)unused;
    uint8_t *state=((uint8_t *(*)(void))(uintptr_t)0x401af782u)();
    uint8_t *ctx=((uint8_t *(*)(void))(uintptr_t)0x4019e44eu)();
    if(!state || !ctx) { canopy_memory_test[2]=1; return -1; }
    canopy_memory_test[3]=state[0x1c];
    if(!canopy_memory_target) { canopy_memory_test[2]=2; return -1; }
    canopy_memory_test[12]=sound_hash(canopy_memory_target);
    ((void (*)(uint8_t *))(uintptr_t)0x40135b7au)(state);
    canopy_memory_test[4]=state[0x1c];
    ((uint8_t *)header)[14]=0x10;
    ((uint8_t *)header)[15]=0x1d;
    canopy_load_guard[0]=1;
    canopy_memory_test[5]=canopy_load_guard[1];
    canopy_memory_test[6]=canopy_load_guard[2];
    ((void (*)(uint8_t *,uint32_t,void *,void *,uint32_t))(uintptr_t)0x400d45c4u)
        (ctx,0,header,input,sizeof(input));
    canopy_load_guard[0]=0;
    canopy_memory_test[7]=state[0x1c];
    canopy_memory_test[8]=canopy_load_guard[1];
    canopy_memory_test[9]=canopy_load_guard[2];
    canopy_memory_test[13]=sound_hash(canopy_memory_target);
    canopy_memory_test[2]=3;
    return 0;
}
static void done(uint8_t *unused,int32_t result)
{ (void)unused; canopy_memory_test[10]=(uint32_t)result; canopy_memory_test[11]=1; }
void canopy_memory_probe_tick(void)
{
    if(canopy_memory_test[0]!=1) return;
    canopy_memory_test[0]=0;
    canopy_memory_test[1]=(uint32_t)dkrt_job_post((uintptr_t)step,(uintptr_t)done,0);
}
