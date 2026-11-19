/* The home of the loop effect's controls (DTII): the crossfader and the counters of ARM and DROP, held outside the kit,
 * and the engine's status as the DSP returns it. No audio is read on the ColdFire.
 *
 * To the engine: the fx service asks fx_held_values for the effect's values instead of reading the kit, and sends them
 * as the parameter words of the effect's row, the way it sends any knob. From the engine: core/dsp-link returns the
 * status mailbox in fourteen spare words of the reply; loop_ui_tick reads them on the UI task. */
#include <dk/loop.h>
typedef uint32_t u32;
typedef uint16_t u16;
extern volatile u32 loop_reply_literal;
extern u32 dkrt_irq_mask(void);
extern void dkrt_irq_restore(u32);

#define WIRE_MAGIC 0x4f564c32u
/* register::effect_id of the loop effect: the module id of Overloop and its effect id "loop" */
#define LOOP_EFFECT 0xca1bu
/* UI passes without a new heartbeat after which the status is not fresh */
#define STALE 60u

static u32 raw[LP_COUNT];
static u32 status[LS_WORDS];
static u32 age = 2 * STALE, last_seq;
/* The native FX page of the loop effect, as the fx service last said, and the UI passes since it said so. */
static u32 page, asked = 2 * STALE;
/* Diagnostics for the device test: status updates taken, and loops the home has seen. */
u32 loop_control_updates, loop_control_seen;

u32 dkrt_loop_get(u32 index)
{
    return index < LP_COUNT ? raw[index] : 0;
}
void dkrt_loop_xfad(u32 value)
{
    raw[LP_XFAD] = value > LP_MAX ? LP_MAX : value;
}
void dkrt_loop_arm(void)
{
    raw[LP_ARM] = (raw[LP_ARM] + 1) & LP_MAX;
}
void dkrt_loop_drop(void)
{
    raw[LP_DROP] = (raw[LP_DROP] + 1) & LP_MAX;
}
u32 dkrt_loop_status(u32 *out)
{
    for (u32 i = 0; i < LS_WORDS; ++i)
        out[i] = status[i];
    return age < STALE;
}

/* The native FX page of the loop effect, or 0 while no loop effect is loaded (the fx service has not asked for its values
 * for a while). */
u32 dkrt_loop_page(void)
{
    return asked < STALE ? page : 0;
}

/* One UI pass: the status and the page grow older. */
void loop_control_pass(void)
{
    if (age < 2 * STALE)
        ++age;
    if (asked < 2 * STALE)
        ++asked;
}

/* The fx service's question (its `[[info]]` entry), once a UI pass for every effect that is loaded: the raw values of the
 * effect ID, on the kit's scale (a value 0 .. 127 in the high byte), when this home holds them. NATIVE_PAGE is the FX page
 * of that effect. */
int fx_held_values(u32 id, u32 native_page, u16 *out)
{
    if (id != LOOP_EFFECT)
        return 0;
    page = native_page;
    asked = 0;
    for (u32 j = 0; j < 8; ++j)
        out[j] = j < LP_COUNT ? (u16)(raw[j] << 8) : 0;
    return 1;
}

/* A status the DSP returned (LS_WORDS words; words 7, 14 and 15 are 0). A new loop sets the crossfader to LOOP, where the
 * engine holds it until it reads this count back in LP_SEEN. */
void loop_control_status(const u32 *words)
{
    if (words[LS_HEARTBEAT] == last_seq)
        return;
    last_seq = words[LS_HEARTBEAT];
    age = 0;
    ++loop_control_updates;
    for (u32 i = 0; i < LS_WORDS; ++i)
        status[i] = words[i];
    u32 count = (words[LS_STATE] >> LS_COUNT_SHIFT) & LP_MAX;
    if (count != raw[LP_SEEN]) {
        raw[LP_SEEN] = count;
        if (words[LS_STATE] & LS_LOOP)
            raw[LP_XFAD] = LP_MAX;
        ++loop_control_seen;
    }
}

#ifndef LOOP_CONTROL_TEST
/* Once a UI pass (the platform's optional tick). The driver copies the reply inside the frame interrupt: mask only during
 * this bounded read. Halfwords arrive low half first (the DSP and the ColdFire have different byte order). */
void loop_ui_tick(void)
{
    u32 words[LS_WORDS];
    const volatile u16 *p = (const volatile u16 *)loop_reply_literal;
    loop_control_pass();
    if (!p)
        return;
    u32 sr = dkrt_irq_mask();
    for (u32 j = 0; j < 14; ++j) {
        /* The carrier's cells: +0x44 and +0x48 of rows 17 to 20 and 29 to 31. FX diagnostics own rows 1 to 16,
         * instruments 21 to 28. */
        u32 row = j < 8 ? 17 + j / 2 : 29 + (j - 8) / 2;
        u32 off = (0x1c + 0x54 * row + 0x44 + (j & 1) * 4) / 2;
        words[j] = p[off] | ((u32)p[off + 1] << 16);
    }
    dkrt_irq_restore(sr);
    /* The carrier packs words 7, 14 and 15 into its word 7: this engine leaves them 0. */
    if (words[0] != WIRE_MAGIC || words[7])
        return;
    words[LS_MAGIC] = LS_MAGIC_VALUE;
    words[14] = words[15] = 0;
    loop_control_status(words);
}
#endif
