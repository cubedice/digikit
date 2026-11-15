#include "sound-codec.h"
/* Fixed-width fields preserve both remembered source banks, including legacy
 * values. The record is a feasibility proof, not yet a Sound storage ABI. */
static const uint8_t control_bits[CP_CONTROLS]={
    4,2,7,7,9,7,4,3,3,7,7,2,0,0,7,7,
    3,10,7,10,2,2,1,8,7,7,8,8,7,4,8,8
};
static const uint8_t fm_bits[13]={7,2,7,4,4,7,7,7,7,7,8,8,8};
enum { HEADER=3, CRC_AT=40, PAYLOAD_BITS=(CRC_AT-HEADER)*8 };
static uint16_t crc(const uint8_t *p)
{
    uint16_t c=0xffff;
    for(uint32_t i=0;i<CRC_AT;++i) {
        c^=(uint16_t)p[i]<<8;
        for(uint32_t j=0;j<8;++j) c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));
    }
    return c;
}
static void write_bits(uint8_t *b,uint32_t *at,uint32_t value,uint32_t width)
{
    for(uint32_t i=width;i>0;--i) {
        uint32_t bit=(*at)++;
        b[HEADER+bit/8]|=(uint8_t)(((value>>(i-1))&1u)<<(7-bit%8));
    }
}
static uint32_t read_bits(const uint8_t *b,uint32_t *at,uint32_t width)
{
    uint32_t value=0;
    for(uint32_t i=0;i<width;++i) {
        uint32_t bit=(*at)++;
        value=(value<<1)|((b[HEADER+bit/8]>>(7-bit%8))&1u);
    }
    return value;
}
int sc_encode(const CpPreset *p,uint8_t out[SC_BYTES])
{
    if(!p || !out || !cp_valid(p) || p->clock!=CP_ARP_CLOCK || p->bpm!=CP_ARP_BPM_RESERVED) return -1;
    uint8_t b[SC_BYTES];
    for(uint32_t i=0;i<SC_BYTES;++i) b[i]=0;
    b[0]='C'; b[1]='S'; b[2]=1;
    uint32_t at=0;
    for(uint32_t i=0;i<CP_CONTROLS;++i) write_bits(b,&at,p->control[i],control_bits[i]);
    for(uint32_t i=0;i<13;++i) write_bits(b,&at,p->fm[i],fm_bits[i]);
    write_bits(b,&at,p->family,2); write_bits(b,&at,p->mode,1);
    write_bits(b,&at,p->shape.count,3);
    for(uint32_t i=0;i<4;++i) write_bits(b,&at,p->shape.intervals[i],7);
    if(at>PAYLOAD_BITS) return -1;
    uint16_t sum=crc(b); b[CRC_AT]=(uint8_t)(sum>>8); b[CRC_AT+1]=(uint8_t)sum;
    for(uint32_t i=0;i<SC_BYTES;++i) out[i]=b[i];
    return 0;
}
int sc_decode(CpPreset *out,const uint8_t in[SC_BYTES])
{
    if(!out || !in || in[0]!='C' || in[1]!='S' || in[2]!=1) return -1;
    uint16_t sum=crc(in);
    if(in[CRC_AT]!=(uint8_t)(sum>>8) || in[CRC_AT+1]!=(uint8_t)sum) return -1;
    CpPreset p;
    for(uint32_t i=0;i<sizeof(p);++i) ((volatile uint8_t *)&p)[i]=0;
    uint32_t at=0;
    for(uint32_t i=0;i<CP_CONTROLS;++i) p.control[i]=(uint16_t)read_bits(in,&at,control_bits[i]);
    for(uint32_t i=0;i<13;++i) p.fm[i]=(uint16_t)read_bits(in,&at,fm_bits[i]);
    p.family=(uint8_t)read_bits(in,&at,2); p.mode=(uint8_t)read_bits(in,&at,1);
    p.shape.count=(uint8_t)read_bits(in,&at,3);
    for(uint32_t i=0;i<4;++i) p.shape.intervals[i]=(uint8_t)read_bits(in,&at,7);
    while(at<PAYLOAD_BITS) if(read_bits(in,&at,1)) return -1;
    p.clock=CP_ARP_CLOCK; p.bpm=CP_ARP_BPM_RESERVED;
    if(!cp_valid(&p)) return -1;
    for(uint32_t i=0;i<sizeof(p);++i) ((volatile uint8_t *)out)[i]=((const volatile uint8_t *)&p)[i];
    return 0;
}

/* The stock v3 Sound writer zeroes these 21 halfwords. The stock reader maps
 * the first eight to dummy parameter 0 and ignores the last thirteen. They
 * are not a runtime Sound home. */
static uint32_t sound_offset(uint32_t i)
{
    uint32_t slot=i<8 ? 4*(i+1) : 77+(i-8);
    return 0x1c+2*slot;
}
int sc_sound_record_write(uint8_t *record,uint32_t length,const CpPreset *p)
{
    uint8_t payload[SC_BYTES];
    if(!record || length<0x455 || sc_encode(p,payload)) return -1;
    for(uint32_t i=0;i<21;++i) {
        uint32_t at=sound_offset(i);
        record[at]=payload[2*i]; record[at+1]=payload[2*i+1];
    }
    return 0;
}
int sc_sound_record_read(CpPreset *out,const uint8_t *record,uint32_t length)
{
    uint8_t payload[SC_BYTES];
    if(!out || !record || length<0x455) return -1;
    for(uint32_t i=0;i<21;++i) {
        uint32_t at=sound_offset(i);
        payload[2*i]=record[at]; payload[2*i+1]=record[at+1];
    }
    return sc_decode(out,payload);
}
