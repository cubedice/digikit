/* Device setup only. Independent of sound presets and MIDI performance state. */
#ifndef DK_CANOPY_ROUTING_H
#define DK_CANOPY_ROUTING_H
#include <stdint.h>
enum { CR_BYTES=16 };
typedef struct { uint8_t enabled,usb,din,channel,selector_octave,selector_channel; } CrRoute;
static int cr_valid(const CrRoute *r)
{
    return r->enabled<=1 && r->usb<=1 && r->din<=1 && r->channel>=1 && r->channel<=16 &&
        r->selector_octave<=9 && r->selector_channel<=16 && r->selector_channel!=r->channel;
}
static int cr_encode(const CrRoute *r,uint8_t b[CR_BYTES])
{
    if(!cr_valid(r)) return -1;
    for(uint32_t i=0;i<CR_BYTES;++i) b[i]=0;
    b[0]='C'; b[1]='R'; b[2]=1;
    b[4]=r->enabled; b[5]=r->usb; b[6]=r->din; b[7]=r->channel;
    b[8]=r->selector_octave; b[9]=r->selector_channel;
    for(uint32_t i=0;i<14;++i) { b[14]+=b[i]; b[15]+=b[14]; }
    return 0;
}
static int cr_decode(CrRoute *out,const uint8_t *b,uint32_t n)
{
    if(n!=CR_BYTES || b[0]!='C' || b[1]!='R' || b[2]!=1 || b[3]) return -1;
    for(uint32_t i=10;i<14;++i) if(b[i]) return -1;
    uint8_t a=0,c=0;
    for(uint32_t i=0;i<14;++i) { a+=b[i]; c+=a; }
    if(a!=b[14] || c!=b[15]) return -1;
    CrRoute r={b[4],b[5],b[6],b[7],b[8],b[9]};
    if(!cr_valid(&r)) return -1;
    *out=r; return 0;
}
/* Per-track external channels (0: the routing CHANNEL), saved with SAVE
 * ROUTING under its own key so the version 1 routing record stays unchanged. */
enum { CC_BYTES=20 };
static int cc_encode(const uint8_t channels[16],uint8_t b[CC_BYTES])
{
    for(uint32_t i=0;i<16;++i) if(channels[i]>16) return -1;
    b[0]='C'; b[1]='C'; b[2]=1; b[3]=0;
    uint8_t a=0;
    for(uint32_t i=0;i<16;++i) { b[4+i]=channels[i]; a+=channels[i]; }
    b[3]=(uint8_t)(0x5a^a);
    return 0;
}
static int cc_decode(uint8_t out[16],const uint8_t *b,uint32_t n)
{
    if(n!=CC_BYTES || b[0]!='C' || b[1]!='C' || b[2]!=1) return -1;
    uint8_t a=0;
    for(uint32_t i=0;i<16;++i) { if(b[4+i]>16) return -1; a+=b[4+i]; }
    if(b[3]!=(uint8_t)(0x5a^a)) return -1;
    for(uint32_t i=0;i<16;++i) out[i]=b[4+i];
    return 0;
}
#endif
