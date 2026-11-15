#include "src-lock-apply.h"

int ilp_apply(const uint32_t base[32],const IlpSnapshot *snapshot,uint32_t output[32])
{
    if(!base || !snapshot || !output || snapshot->count>ILP_LANES ||
       (base[0]&255u)!=11 || base[1]>3) return ILP_INVALID;
    uint32_t bank[32],family=base[1];
    for(uint32_t i=0;i<32;++i) bank[i]=base[i];
    /* Source-family/keyboard-mode and chord choices alter the CF mapper or
     * remembered banks. They cannot safely be approximated in a DSP bank. */
    for(uint32_t i=0;i<snapshot->count;++i) {
        uint32_t id=snapshot->pair[i].id,value=snapshot->pair[i].value;
        uint16_t encoded;
        if(!ilp_pack(id,value,&encoded)) return ILP_INVALID;
        for(uint32_t j=0;j<i;++j) if(snapshot->pair[j].id==id) return ILP_DUPLICATE;
        uint32_t control=ilp_control_for(family,id);
        if(control==UINT32_MAX) continue; /* a lock for an inactive sound family */
        if(control==12 || control==13 || control==34 || control>36 || control==32 || control==33)
            return ILP_APPLY_UNSUPPORTED;
        if(control==35) { bank[13]=value; continue; }
        if(control==36) { if(value<30) return ILP_INVALID; bank[7]=value; continue; }
        if((control==6 || control==9 || (family==2 && control==19)) && !value)
            return ILP_INVALID;
        if(control==0) {
            bank[0]=(bank[0]&~0x300u)|(value ? 0x100u : 0u);
            if(value) bank[16]=(bank[16]&0xffff0000u)|(value-1);
        } else if(control<16) {
            static const int8_t duplicate[16]={-1,-1,2,3,15,6,8,10,11,9,12,14,-1,-1,4,5};
            uint32_t word=16+control;
            bank[word]=(bank[word]&0xffff0000u)|value;
            int32_t target=duplicate[control];
            if(target>=0) bank[target]=control==4 ? (value*3u)/2u : value;
        } else {
            bank[control]=(bank[control]&0xffffu)|(value<<16);
        }
    }
    /* The same safety rules as the live MOD adapter, evaluated after all
     * controls so a snapshot does not depend on its lane order. */
    if(bank[13]==1) { bank[26]&=0xffff0000u; bank[12]=0; }
    if(family==2 && (bank[22]>>16) && (bank[19]>>16)>7)
        bank[19]=(bank[19]&0xffffu)|(7u<<16);
    for(uint32_t i=0;i<32;++i) output[i]=bank[i];
    return 1;
}
