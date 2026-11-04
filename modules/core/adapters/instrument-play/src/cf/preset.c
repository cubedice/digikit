#include "preset.h"
static const uint16_t maximum[CP_CONTROLS]={11,3,127,127,500,127,8,4,3,100,75,2,1,0,127,127};
static const uint16_t minimum[CP_CONTROLS]={0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,0};
int cp_valid(const CpPreset *p)
{
    uint32_t i;
    for(i=0;i<CP_CONTROLS;++i)
        if(p->control[i]<minimum[i] || p->control[i]>maximum[i]) return 0;
    if(p->bpm<30 || p->bpm>300 || p->family>1 || p->mode>KB_ROOT || p->clock>2 ||
        (p->clock==1 && p->control[CP_SWING])) return 0;
    if(p->shape.count>4 || p->shape.intervals[0]) return 0;
    for(i=0;i<4;++i)
        if(p->shape.intervals[i]>127 || (i>=p->shape.count && p->shape.intervals[i]) ||
            (i && i<p->shape.count && p->shape.intervals[i]<=p->shape.intervals[i-1])) return 0;
    return 1;
}
static uint32_t checksum(const uint8_t *b)
{
    uint32_t h=2166136261u;
    for(uint32_t i=0;i<60;++i) h=(h^b[i])*16777619u;
    return h;
}
int cp_encode(const CpPreset *p,uint8_t b[CP_RECORD_BYTES])
{
    if(!cp_valid(p)) return -1;
    for(uint32_t i=0;i<CP_RECORD_BYTES;++i) b[i]=0;
    b[0]='C'; b[1]='P'; b[2]='S'; b[3]='T'; b[4]=1; b[5]=CP_CONTROLS;
    for(uint32_t i=0;i<CP_CONTROLS;++i) {
        b[8+2*i]=(uint8_t)(p->control[i]>>8); b[9+2*i]=(uint8_t)p->control[i];
    }
    b[40]=p->family; b[41]=p->mode; b[42]=p->clock;
    b[44]=(uint8_t)(p->bpm>>8); b[45]=(uint8_t)p->bpm;
    b[46]=p->shape.count;
    for(uint32_t i=0;i<4;++i) b[47+i]=p->shape.intervals[i];
    uint32_t h=checksum(b);
    for(uint32_t i=0;i<4;++i) b[60+i]=(uint8_t)(h>>(24-8*i));
    return 0;
}
int cp_decode(CpPreset *out,const uint8_t *b,uint32_t length)
{
    if(length!=CP_RECORD_BYTES || b[0]!='C' || b[1]!='P' || b[2]!='S' || b[3]!='T' ||
        b[4]!=1 || b[5]!=CP_CONTROLS || b[6] || b[7] || b[43]) return -1;
    for(uint32_t i=51;i<60;++i) if(b[i]) return -1;
    uint32_t h=checksum(b);
    for(uint32_t i=0;i<4;++i) if(b[60+i]!=(uint8_t)(h>>(24-8*i))) return -1;
    CpPreset p;
    /* Freestanding ColdFire does not supply libc memset. Keep this bounded
     * initialization explicit, including padding copied below. */
    for(uint32_t i=0;i<sizeof(p);++i) ((volatile uint8_t *)&p)[i]=0;
    for(uint32_t i=0;i<CP_CONTROLS;++i) p.control[i]=(uint16_t)((uint32_t)b[8+2*i]*256+b[9+2*i]);
    p.family=b[40]; p.mode=b[41]; p.clock=b[42]; p.bpm=(uint16_t)((uint32_t)b[44]*256+b[45]);
    p.shape.count=b[46];
    for(uint32_t i=0;i<4;++i) p.shape.intervals[i]=b[47+i];
    if(!cp_valid(&p)) return -1;
    /* Volatile byte copy avoids freestanding compiler-generated memcpy. */
    for(uint32_t i=0;i<sizeof(p);++i) ((volatile uint8_t *)out)[i]=((const volatile uint8_t *)&p)[i];
    return 0;
}
