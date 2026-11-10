#include "preset.h"
static const uint16_t maximum[CP_CONTROLS]={11,3,127,127,500,127,8,4,6,100,75,2,1,0,127,127,4,700,100,1000,2,2,1,200,127,127,200,200,100,15,200,200};
static const uint16_t minimum[CP_CONTROLS]={0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};
void cp_wavetable_defaults(uint16_t controls[CP_CONTROLS])
{
    static const uint16_t defaults[12]={0,0,0,10,0,0,0,100,5,15,100,100};
    for(uint32_t i=0;i<12;++i) controls[CP_WT_TABLE+i]=defaults[i];
    controls[CP_WTFM_DEPTH]=0; controls[CP_WTFM_RATIO]=3;
    controls[CP_WTFM_ENV]=100; controls[CP_WTFM_WHEEL]=100;
}
void cp_fm_defaults(uint16_t fm[13])
{
    static const uint16_t defaults[13]={32,0,50,3,8,0,5,40,50,15,100,100,100};
    for(uint32_t i=0;i<13;++i) fm[i]=defaults[i];
}
int cp_valid(const CpPreset *p)
{
    uint32_t i;
    for(i=0;i<CP_CONTROLS;++i)
        if(p->control[i]<minimum[i] || p->control[i]>maximum[i]) return 0;
    static const uint16_t fm_max[13]={127,3,100,15,15,100,127,127,100,127,200,200,200};
    for(i=0;i<13;++i) if(p->fm[i]>fm_max[i]) return 0;
    if(p->bpm<30 || p->bpm>300 || p->family>3 || p->mode>KB_ROOT || p->clock>2 ||
        (p->clock==1 && p->control[CP_SWING])) return 0;
    if(p->control[CP_WT_CLOCK] && p->control[CP_WT_RATE]>7) return 0;
    if(p->shape.count>4 || p->shape.intervals[0]) return 0;
    for(i=0;i<4;++i)
        if(p->shape.intervals[i]>127 || (i>=p->shape.count && p->shape.intervals[i]) ||
            (i && i<p->shape.count && p->shape.intervals[i]<=p->shape.intervals[i-1])) return 0;
    return 1;
}
static uint32_t checksum(const uint8_t *b,uint32_t count)
{
    uint32_t h=2166136261u;
    for(uint32_t i=0;i<count;++i) h=(h^b[i])*16777619u;
    return h;
}
int cp_encode(const CpPreset *p,uint8_t b[CP_RECORD_BYTES])
{
    if(!cp_valid(p)) return -1;
    for(uint32_t i=0;i<CP_RECORD_BYTES;++i) ((volatile uint8_t *)b)[i]=0;
    b[0]='C'; b[1]='P'; b[2]='S'; b[3]='T'; b[4]=5; b[5]=CP_CONTROLS;
    for(uint32_t i=0;i<16;++i) {
        b[8+2*i]=(uint8_t)(p->control[i]>>8); b[9+2*i]=(uint8_t)p->control[i];
    }
    b[40]=p->family; b[41]=p->mode; b[42]=p->clock;
    b[44]=(uint8_t)(p->bpm>>8); b[45]=(uint8_t)p->bpm;
    b[46]=p->shape.count;
    for(uint32_t i=0;i<4;++i) b[47+i]=p->shape.intervals[i];
    for(uint32_t i=0;i<12;++i) {
        b[64+2*i]=(uint8_t)(p->control[16+i]>>8); b[65+2*i]=(uint8_t)p->control[16+i];
    }
    b[88]=CP_TABLE_VERSION;
    for(uint32_t i=0;i<13;++i) {
        b[92+2*i]=(uint8_t)(p->fm[i]>>8); b[93+2*i]=(uint8_t)p->fm[i];
    }
    b[118]=CP_FM_VERSION;
    for(uint32_t i=0;i<4;++i) {
        b[120+2*i]=(uint8_t)(p->control[CP_WTFM_DEPTH+i]>>8);
        b[121+2*i]=(uint8_t)p->control[CP_WTFM_DEPTH+i];
    }
    b[128]=CP_WTFM_VERSION;
    uint32_t h=checksum(b,140);
    for(uint32_t i=0;i<4;++i) b[140+i]=(uint8_t)(h>>(24-8*i));
    return 0;
}
int cp_decode(CpPreset *out,const uint8_t *b,uint32_t length)
{
    if((length!=CP_RECORD_BYTES && length!=CP_FM_BYTES && length!=CP_WAVETABLE_BYTES && length!=CP_LEGACY_BYTES) || b[0]!='C' || b[1]!='P' || b[2]!='S' || b[3]!='T') return -1;
    int legacy=length==CP_LEGACY_BYTES;
    int wtfm=length==CP_RECORD_BYTES,fm=length>=CP_FM_BYTES;
    if((legacy ? (b[4]!=1 && b[4]!=2) || b[5]!=16 : b[4]!=(wtfm?5:fm?4:3) || b[5]!=(wtfm?32:28)) || b[6] || b[7] || b[43]) return -1;
    for(uint32_t i=51;i<(legacy?60:64);++i) if(b[i]) return -1;
    if(!legacy && (b[88]!=CP_TABLE_VERSION || b[89] || b[90] || b[91])) return -1;
    if(fm) {
        if(b[118]!=CP_FM_VERSION || b[119]) return -1;
        if(!wtfm) { for(uint32_t i=120;i<124;++i) if(b[i]) return -1; }
    }
    if(wtfm) {
        if(b[128]!=CP_WTFM_VERSION) return -1;
        for(uint32_t i=129;i<140;++i) if(b[i]) return -1;
    }
    uint32_t end=length-4,h=checksum(b,end);
    for(uint32_t i=0;i<4;++i) if(b[end+i]!=(uint8_t)(h>>(24-8*i))) return -1;
    CpPreset p;
    /* Freestanding ColdFire does not supply libc memset. Keep this bounded
     * initialization explicit, including padding copied below. */
    for(uint32_t i=0;i<sizeof(p);++i) ((volatile uint8_t *)&p)[i]=0;
    cp_wavetable_defaults(p.control);
    cp_fm_defaults(p.fm);
    for(uint32_t i=0;i<16;++i) p.control[i]=(uint16_t)((uint32_t)b[8+2*i]*256+b[9+2*i]);
    if(!legacy) for(uint32_t i=0;i<12;++i) p.control[16+i]=(uint16_t)((uint32_t)b[64+2*i]*256+b[65+2*i]);
    if(legacy && b[40]>1) return -1;
    if(!legacy && !fm && b[40]>2) return -1;
    if(fm) for(uint32_t i=0;i<13;++i) p.fm[i]=(uint16_t)((uint32_t)b[92+2*i]*256+b[93+2*i]);
    if(wtfm) for(uint32_t i=0;i<4;++i) p.control[CP_WTFM_DEPTH+i]=(uint16_t)((uint32_t)b[120+2*i]*256+b[121+2*i]);
    if(b[4]==1) {
        if(p.control[CP_OCTAVES]<1 || p.control[CP_OCTAVES]>3) return -1;
        p.control[CP_OCTAVES]+=2;
    }
    p.family=b[40]; p.mode=b[41]; p.clock=b[42]; p.bpm=(uint16_t)((uint32_t)b[44]*256+b[45]);
    p.shape.count=b[46];
    for(uint32_t i=0;i<4;++i) p.shape.intervals[i]=b[47+i];
    if(!cp_valid(&p)) return -1;
    /* Volatile byte copy avoids freestanding compiler-generated memcpy. */
    for(uint32_t i=0;i<sizeof(p);++i) ((volatile uint8_t *)out)[i]=((const volatile uint8_t *)&p)[i];
    return 0;
}
