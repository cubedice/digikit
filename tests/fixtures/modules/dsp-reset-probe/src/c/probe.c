/* Explicit hardware diagnostic, DTII 1.17 only. Never runs reset at init.
 * Command 0x3f: empty = snapshot, RSET = assert while SPI is running,
 * RLSE = attempt release after an assertion. Whole-device restart is required
 * afterwards: releasing the DSP pin does not reload its firmware. */
#include <dk/api.h>
typedef uint32_t u32;
typedef uint8_t u8;
static u32 fired;
static u32 reads[24];
static void snapshot(u32 offset) {
    reads[offset] = *(volatile u32 *)0xec038000u;
    reads[offset+1] = *(volatile u32 *)0xec03802cu;
    reads[offset+2] = *(volatile uint16_t *)0xfc04539eu;
    reads[offset+3] = *(volatile uint16_t *)0xfc0453beu;
    reads[offset+4] = *(volatile u32 *)0xfc0453a0u; /* TX SADDR */
    reads[offset+5] = *(volatile u32 *)0xfc045390u; /* RX DADDR */
    reads[offset+7] = *(volatile u8 *)0x402a1b58u;
    reads[offset+6] = *(volatile u8 *)(reads[offset+7] ? 0xec094020u : 0xec09401du);
}
static int32_t command(u32 cmd, u32 seq, const u8 *body, u32 len, u8 *out) {
    u32 i, value;
    (void)cmd; (void)seq;
    if (len) {
        if (len!=4) return -77;
        value=((u32)body[0]<<24)|((u32)body[1]<<16)|((u32)body[2]<<8)|body[3];
        /* Refuse the unpatched stock reset helper. Host also verifies the
         * exact installed hook bytes against the prepared image before use. */
        if (*(volatile uint16_t *)0x400cd01cu != 0x4ef9u) return -79;
        if (value==0x52534554u) {
            if (fired) return -80;
            snapshot(0);
            if (!(reads[1]&0x40000000u)) return -78;
            ((void (*)(u32))0x400cd01cu)(1);
            fired=1;
            snapshot(8);
        } else if (value==0x524c5345u) {
            if (!fired) return -81;
            ((void (*)(u32))0x400cd01cu)(0);
        } else return -77;
    }
    snapshot(16);
    for(i=0;i<24;++i) {
        out[4*i]=(u8)(reads[i]>>24); out[4*i+1]=(u8)(reads[i]>>16);
        out[4*i+2]=(u8)(reads[i]>>8); out[4*i+3]=(u8)reads[i];
    }
    return 96;
}
int32_t dkm_init(const dk_api *api) { return api->register_cmds(0x3f,1,command); }
