#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/control.c"
/* The interrupt mask: counted, so that a test sees every mask restored. */
static u32 masked;
u32 dkrt_irq_mask(void)
{
    return ++masked;
}
void dkrt_irq_restore(u32 sr)
{
    assert(sr == masked);
    --masked;
}

/* The copy of a DSP reply (0xabc bytes) as the block interrupt hands it on, and the carrier's fourteen cells in it. */
static u16 reply_copy[0xabc / 2];
static void cell(u32 j, u32 value)
{
    u32 row = j < 8 ? 17 + j / 2 : 29 + (j - 8) / 2;
    u32 off = (0x1c + 0x54 * row + 0x44 + (j & 1) * 4) / 2;
    reply_copy[off] = (u16)value;
    reply_copy[off + 1] = (u16)(value >> 16);
}
/* A reply that carries the loop effect's status (OVL2): heartbeat, state bits, the loop's frames. */
static void carrier(u32 heartbeat, u32 state, u32 frames)
{
    memset(reply_copy, 0xa5, sizeof(reply_copy));
    for (u32 j = 0; j < 14; ++j)
        cell(j, 0);
    cell(0, WIRE_MAGIC);
    cell(LS_HEARTBEAT, heartbeat);
    cell(LS_STATE, state);
    cell(LS_LOOP_FRAMES, frames);
}
/* A reply of the phrase effect in the same cells (its magic). */
static void phrase_reply(u32 serial)
{
    memset(reply_copy, 0x5a, sizeof(reply_copy));
    cell(0, 0x50485231);
    cell(1, serial);
}
/* The block interrupt, as core/dsp-link calls the hook. */
static void block(void)
{
    loop_reply_frame(reply_copy);
}
static void reply_hook(void)
{
    u32 out[LS_WORDS];
    u32 updates = loop_control_updates;
    /* No reply yet, or no copy: nothing to take. */
    loop_reply_frame(0);
    loop_ui_tick();
    assert(loop_control_updates == updates && masked == 0);
    /* The status in one reply of four, the phrase reply in the other three: the UI pass takes it whichever block it comes
     * after. */
    carrier(100, LS_TRACK, 0);
    block();
    phrase_reply(1);
    block();
    phrase_reply(2);
    block();
    loop_ui_tick();
    assert(loop_control_updates == updates + 1 && masked == 0);
    assert(dkrt_loop_status(out) == 1 && out[LS_MAGIC] == LS_MAGIC_VALUE &&
           out[LS_HEARTBEAT] == 100 && out[LS_STATE] == LS_TRACK && out[14] == 0 && out[15] == 0);
    /* Two statuses between UI passes: the newest one. */
    carrier(104, LS_TRACK | LS_ARMED, 0);
    block();
    phrase_reply(3);
    block();
    carrier(108, LS_LOOP, 96000);
    block();
    phrase_reply(4);
    block();
    loop_ui_tick();
    assert(loop_control_updates == updates + 2);
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == 108 && out[LS_STATE] == LS_LOOP &&
           out[LS_LOOP_FRAMES] == 96000);
    /* A UI pass without a new reply takes nothing again. */
    loop_ui_tick();
    assert(loop_control_updates == updates + 2);
    /* A carrier with packed bits in its word 7, or without the magic, is not the loop effect's status. */
    carrier(112, LS_TRACK, 0);
    cell(7, 1);
    block();
    carrier(116, LS_TRACK, 0);
    cell(0, LS_MAGIC_VALUE);
    block();
    loop_ui_tick();
    assert(loop_control_updates == updates + 2 && dkrt_loop_status(out) == 1 &&
           out[LS_HEARTBEAT] == 108);
    /* The status stops: after STALE passes it is not fresh. */
    for (u32 i = 0; i < STALE; ++i)
        loop_ui_tick();
    assert(dkrt_loop_status(out) == 0 && masked == 0);
    carrier(120, LS_TRACK, 0);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == 120);
}

/* A status as the engine publishes it: heartbeat, state bits, the count of loops. */
static void report(u32 heartbeat, u32 bits, u32 count)
{
    u32 words[LS_WORDS] = {0};
    words[LS_MAGIC] = LS_MAGIC_VALUE;
    words[LS_HEARTBEAT] = heartbeat;
    words[LS_STATE] = bits | (count << LS_COUNT_SHIFT);
    words[LS_LOOP_FRAMES] = 96000;
    loop_control_status(words);
}
int main(void)
{
    u32 out[LS_WORDS];
    u16 held[8];
    /* Nothing is kept across a restart: LIVE, no arm, no status. */
    for (u32 i = 0; i < LP_COUNT; ++i)
        assert(dkrt_loop_get(i) == 0);
    assert(dkrt_loop_get(LP_COUNT) == 0);
    assert(dkrt_loop_status(out) == 0);

    /* The crossfader: 0 .. 127, a larger value is LOOP. */
    dkrt_loop_xfad(64);
    assert(dkrt_loop_get(LP_XFAD) == 64);
    dkrt_loop_xfad(500);
    assert(dkrt_loop_get(LP_XFAD) == LP_MAX);
    dkrt_loop_xfad(0);

    /* ARM and DROP are counters the engine watches for a change; they wrap inside seven bits. */
    dkrt_loop_arm();
    assert(dkrt_loop_get(LP_ARM) == 1 && dkrt_loop_get(LP_DROP) == 0);
    dkrt_loop_drop();
    dkrt_loop_drop();
    assert(dkrt_loop_get(LP_DROP) == 2);
    for (u32 i = 0; i < 127; ++i)
        dkrt_loop_arm();
    assert(dkrt_loop_get(LP_ARM) == 0);

    /* The fx service's question: the loop effect's values are held here, on the kit's scale; no other effect's are. */
    dkrt_loop_xfad(100);
    dkrt_loop_arm();
    memset(held, 0xff, sizeof(held));
    assert(dkrt_loop_page() == 0);
    assert(fx_held_values(LOOP_EFFECT, 15, held) == 1);
    assert(dkrt_loop_page() == 15);
    assert(held[LP_XFAD] == 100 << 8 && held[LP_ARM] == 1 << 8 && held[LP_DROP] == 2 << 8 &&
           held[LP_SEEN] == 0);
    for (u32 j = LP_COUNT; j < 8; ++j)
        assert(held[j] == 0);
    held[0] = 0x1234;
    assert(fx_held_values(0x1234, 16, held) == 0 && held[0] == 0x1234);
    assert(fx_held_values(0, 16, held) == 0);
    assert(dkrt_loop_page() == 15);
    /* The effect is gone when the service has not asked for a while: no page is the loop's. */
    for (u32 i = 0; i < STALE - 1; ++i)
        loop_control_pass();
    assert(dkrt_loop_page() == 15);
    loop_control_pass();
    assert(dkrt_loop_page() == 0);
    assert(fx_held_values(LOOP_EFFECT, 14, held) == 1 && dkrt_loop_page() == 14);

    /* A status makes the snapshot fresh; the same heartbeat again is no news. */
    dkrt_loop_xfad(20);
    report(1, LS_TRACK, 0);
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == 1 && out[LS_STATE] == LS_TRACK);
    assert(dkrt_loop_get(LP_XFAD) == 20 && dkrt_loop_get(LP_SEEN) == 0);
    u32 updates = loop_control_updates;
    report(1, LS_TRACK | LS_ARMED, 0);
    assert(loop_control_updates == updates && dkrt_loop_status(out) == 1 &&
           out[LS_STATE] == LS_TRACK);

    /* An armed change took a loop: the crossfader stands at LOOP, and the engine reads the count back as SEEN. */
    report(2, LS_LOOP | LS_HELD, 1);
    assert(dkrt_loop_get(LP_XFAD) == LP_MAX && dkrt_loop_get(LP_SEEN) == 1);
    assert(fx_held_values(LOOP_EFFECT, 14, held) == 1 && held[LP_SEEN] == 1 << 8 &&
           held[LP_XFAD] == LP_MAX << 8);
    /* From there the crossfader is the performer's again: later reports of the same loop leave it alone. */
    dkrt_loop_xfad(30);
    report(3, LS_LOOP, 1);
    assert(dkrt_loop_get(LP_XFAD) == 30);
    /* A second loop: LOOP again. */
    report(4, LS_LOOP | LS_HELD, 2);
    assert(dkrt_loop_get(LP_XFAD) == LP_MAX && dkrt_loop_get(LP_SEEN) == 2);
    /* A dropped loop leaves the crossfader where it stands. */
    dkrt_loop_xfad(90);
    report(5, 0, 2);
    assert(dkrt_loop_get(LP_XFAD) == 90);
    /* An engine that starts anew (its count is 0 again) is followed without moving the crossfader. */
    report(6, 0, 0);
    assert(dkrt_loop_get(LP_SEEN) == 0 && dkrt_loop_get(LP_XFAD) == 90);
    /* Two pages (Overloop's and PRISM's) share the one home: what one sets the other reads, and an ARM from each is two presses
     * (the second takes the arm back), whichever page they came from. */
    dkrt_loop_xfad(30);
    assert(dkrt_loop_get(LP_XFAD) == 30 && loop_control_raw[LP_XFAD] == 30);
    u32 arms = dkrt_loop_get(LP_ARM);
    dkrt_loop_arm();
    dkrt_loop_arm();
    assert(dkrt_loop_get(LP_ARM) == ((arms + 2) & LP_MAX));
    /* An engine that starts anew goes on counting from its status: its next loop is a new count, and the crossfader goes to
     * LOOP. */
    report(7, 0, 0);
    dkrt_loop_xfad(10);
    report(8, LS_LOOP | LS_HELD, 1);
    assert(dkrt_loop_get(LP_XFAD) == LP_MAX && dkrt_loop_get(LP_SEEN) == 1);
    reply_hook();
    puts("loop control: ok");
    return 0;
}
