/* DTII only. UI-task consumer of the DSP's versioned Overloop mailbox.
 * No retained View pointers, allocation, or drawing in an interrupt.
 * Stock FX pages and all stock encoder/key handlers remain intact.
 */
#include <stdint.h>
#include "dk_fw_draw.h"
#include "dk_fw_view.h"
extern void loop_stock_update(uint8_t *);

extern const uint32_t fxpages_info[];
extern volatile uint32_t loop_reply_literal;
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

#define MAGIC 0x4f564c31u
#define OVERLOOP_ID 0xca1bu
#define MAX_FRAMES 1048544u

uint32_t loop_ui_snapshot[16];
uint32_t loop_ui_paints, loop_ui_updates, loop_ui_age = 120;
static uint32_t last_seq;
static uint32_t drawn_seq;
static int drawn_fresh;

/* The driver copies the reply inside the frame ISR. Mask only during this
 * bounded 64-byte read, never while formatting or painting. Halfwords arrive
 * low half first (the DSP and ColdFire have different byte order).
 */
void loop_ui_tick(void) {
    uint32_t words[16];
    const volatile uint16_t *p = (const volatile uint16_t *)loop_reply_literal;
    if (loop_ui_age < 120) ++loop_ui_age;
    if (!p) return;
    uint32_t sr = dkrt_irq_mask();
    for (uint32_t j = 0; j < 16; ++j) {
        uint32_t off = (0x1c + 0x54 * (13 + j / 2) + 0x44 + (j & 1) * 4) / 2;
        words[j] = p[off] | ((uint32_t)p[off + 1] << 16);
    }
    dkrt_irq_restore(sr);
    if (words[0] != MAGIC || words[2] > 3 || words[3] > MAX_FRAMES ||
        words[4] > MAX_FRAMES || words[7] > 127 || words[8] > 128 ||
        words[9] > 128 || words[10] > 3 || words[11] > 128 ||
        words[12] > MAX_FRAMES || words[13] > MAX_FRAMES ||
        words[14] > 127 || words[15] > 127) return;
    if (words[1] != last_seq) {
        last_seq = words[1];
        loop_ui_age = 0;
        ++loop_ui_updates;
        for (uint32_t j = 0; j < 16; ++j) loop_ui_snapshot[j] = words[j];
    }
}

/* Five columns, seven rows, bit 0 at the top. Local font keeps formatting
 * independent of firmware strings and contains only the displayed alphabet.
 */
static const uint8_t font[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36}, {6,0x49,0x49,0x29,0x1e},
    {0x7e,9,9,9,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,8,8,8,0x7f},
    {0,0x41,0x7f,0x41,0}, {0x20,0x40,0x41,0x3f,1},
    {0x7f,8,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f}, {0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43},
    {8,8,0x3e,8,8}, {0,0x60,0x60,0,0}, {0x20,0x10,8,4,2},
    {0,0x41,0x22,0x14,8}, {8,8,8,8,8}
};
static void box(uint8_t *b, int x, int y, int w, int h, int ink) {
    /* Firmware rectangles use a bottom-left origin; layout uses top-left. */
    if (w > 0 && h > 0) dk_fw_draw_fill_rect(b, x, 64-y-h, x+w-1, 63-y, ink);
}
static int text(uint8_t *b, int x, int y, const char *s) {
    while (*s && x <= 122) {
        unsigned c = (unsigned char)*s++, k = 99;
        if (c >= '0' && c <= '9') k = c-'0';
        if (c >= 'A' && c <= 'Z') k = c-'A'+10;
        if (c == '+') k=36;
        if (c == '.') k=37;
        if (c == '/') k=38;
        if (c == '>') k=39;
        if (c == '-') k=40;
        if (k < 41) for (int j=0;j<5;++j) {
            /* Coalesce vertical strokes to bound firmware drawing calls. */
            for (int i=0;i<7;) {
                if (!(font[k][j] & (1u<<i))) { ++i; continue; }
                int first=i++;
                while (i<7 && (font[k][j] & (1u<<i))) ++i;
                box(b,x+j,y+first,1,i-first,1);
            }
        }
        x += 6;
    }
    return x;
}
static int number(uint8_t *b, int x, int y, uint32_t value, int digits) {
    char out[11]; int n=0;
    do { out[n++]=(char)('0'+value%10); value/=10; } while (value && n<10);
    while (n<digits && n<10) out[n++]='0';
    while (n) { char c[2]={out[--n],0}; x=text(b,x,y,c); }
    return x;
}
static void seconds(uint8_t *b,int x,int y,uint32_t frames) {
    uint32_t cs=frames/480; x=number(b,x,y,cs/100,1); x=text(b,x,y,".");
    x=number(b,x,y,cs%100,2); text(b,x,y,"S");
}
static uint32_t fade_of(uint32_t bits) {
    if (bits & 0x80000000u) return 0;
    if (bits >= 0x3f800000u) return 127;
    uint32_t e=(bits>>23)&255, shift=150-e;
    if (shift>=32) return 0;
    return (((bits&0x7fffffu)|0x800000u)*127u)>>shift;
}

/* Pure rendering entry also exercised against a host bitmap in tests. */
void loop_ui_render(uint8_t *b, const uint32_t *s, int fresh,
                    uint32_t rec, uint32_t start, uint32_t length) {
    box(b,0,0,128,64,0);
    uint32_t mode=s[2], total=s[3], n=s[11], off=s[12], len=s[13];
    int valid=fresh && total && n && off<total && len && len<=total;
    const char *state=!fresh?"NO STATUS":mode==1?"RECORDING":
        mode==3?(valid?"ARMED+LOOP":"ARMED"):s[10]?"ERROR":valid?"RECORDED":"EMPTY";
    /* The containing view paints its native title over rows 0..9 afterwards. */
    text(b,2,11,state); text(b,98,11,rec?">LOOP":">LIVE");
    if (fresh && mode==3) {
        int x=text(b,2,20,"WAIT "); number(b,x,20,s[7]+1,3);
        if (valid) {x=number(b,74,20,n,1); text(b,x,20,"ST");}
    } else if (fresh && mode==1) {
        int x=text(b,2,20,"REC "); x=number(b,x,20,s[9],1);
        x=text(b,x,20,"/"); x=number(b,x,20,s[8],1); text(b,x,20,"ST");
    } else if (fresh && s[10]) text(b,2,20,s[10]==3?"BUFFER FULL":"TIMING LOST");
    else if (valid) {int x=number(b,2,20,n,1); x=text(b,x,20,"ST "); seconds(b,x,20,total);}
    else text(b,2,20,"--");
    for (int i=0;i<=16;++i) box(b,3+i*122/16,43,1,i%4?1:2,1);
    if (valid) {
        /* Frame-based bounds preserve tempo changes within a recording. */
        for (uint32_t i=0;i<122;++i) {
            uint32_t f=i*total/122, rel=(f+total-off)%total;
            if (rel<len) box(b,3+i,36,1,6,1);
            else if (!(i&1)) box(b,3+i,38,1,2,1);
        }
        int sx=3+off*121/total, ex=3+((off+len)%total)*121/total;
        int sl=sx>122?122:sx, el=ex>122?122:ex;
        /* Full-length and very short windows can share a marker pixel. Keep
         * both labels legible without moving the actual boundary lines. */
        if (sl-el<6 && el-sl<6) {
            if (sl<=116) el=sl+6;
            else { el=122; sl=116; }
        }
        text(b,sl,28,"S"); text(b,el,28,"E");
        box(b,sx,35,1,8,1); box(b,ex,35,1,8,1);
        uint32_t phase=s[5]%len;
        int cx=3+((off+phase)%total)*121/total;
        box(b,cx-1,35,3,8,0); box(b,cx,35,1,8,1);
    } else if (fresh && mode==1 && s[8]) box(b,3,36,122*s[9]/s[8],6,1);
    else box(b,3,39,122,1,1);
    int x=text(b,2,46,"S"); x=number(b,x,46,start+1,3); x=text(b,x,46," L");
    if (!length) x=text(b,x,46,"ALL");
    else x=number(b,x,46,valid && length>n?n:length,3);
    if (valid) seconds(b,x+6,46,len);
    text(b,2,55,"LIVE"); text(b,104,55,"LOOP");
    uint32_t fade=valid?fade_of(s[6]):0;
    box(b,33,58,62,1,1); box(b,33+fade*59/127,54,3,9,1);
    box(b,fade<64?2:104,63,23,1,1);
}

static int overloop_row(uint32_t page) {
    const uint32_t *info=fxpages_info;
    if (info[0]!=0x46585047 || info[2]!=1 || page<info[5]) return -1;
    uint32_t row=page-info[5];
    if (row>=info[6] || row>=4) return -1;
    if (((const uint16_t *)info[28])[row]!=OVERLOOP_ID) return -1;
    return (int)row;
}

/* Invoked by ViewController while it owns its live-view list lock. Walk only
 * the drawable suffix, bounded to 64 nodes; never retain a view or manager.
 * A modal's opaque view hides OVL and suppresses our animation requests.
 */
void loop_view_update(uint8_t *view) {
    loop_stock_update(view);
    uint32_t *vt=*(uint32_t **)view;
    uint32_t page=((uint32_t (*)(uint8_t *))vt[0x88/4])(view);
    if (overloop_row(page)<0) return;
    uint8_t *mgr=*(uint8_t **)(view+0x2c);
    if (!mgr) return;
    uint8_t *node=*(uint8_t **)(mgr+0x1c);
    for (unsigned i=0; node && node!=mgr+0x14 && i<64; ++i) {
        if (*(uint8_t **)(node+8)==view) {
            int fresh=loop_ui_age<60;
            // Audio runs at 1500 blocks/s: 60 blocks is a 25 Hz ceiling on
            // animation requests. Native input may still redraw immediately.
            if (fresh!=drawn_fresh ||
                (fresh && (uint32_t)(loop_ui_snapshot[1]-drawn_seq)>=60))
                dk_fw_view_invalidate(view);
            return;
        }
        node=*(uint8_t **)node;
    }
}

int loop_ui_paint(uint8_t *view, uint8_t *bitmap, uint32_t page) {
    (void)view;
    int row=overloop_row(page);
    if (row<0) return 0;
    const uint32_t *info=fxpages_info;
    uint32_t kit=*(volatile uint32_t *)info[19];
    if (kit<0x40000000 || kit>=0x48000000 || (kit&1)) return 0;
    const uint8_t *idx=(const uint8_t *)info+80+8*row;
    const volatile uint16_t *v=(const volatile uint16_t *)(kit+info[14]);
    loop_ui_render(bitmap,loop_ui_snapshot,loop_ui_age<60,
                   v[idx[0]]>=0x3f80,v[idx[1]]/256,v[idx[3]]/256);
    drawn_seq=loop_ui_snapshot[1];
    drawn_fresh=loop_ui_age<60;
    ++loop_ui_paints;
    return 1;
}
