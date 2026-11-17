#include <assert.h>
#include <string.h>
#include "../src/control.c"
static unsigned writes;
static uint8_t saved[4 + PH_PARAM_COUNT * 4];
u32 dkrt_irq_mask(void)
{
    return 0;
}
void dkrt_irq_restore(u32 v)
{
    (void)v;
}
void pp_phrase_timing(u32 *out)
{
    out[0] = 5400000;
    out[1] = 0;
}
int dkrt_settings_get(const char *k, void *p, u32 n)
{
    (void)k;
    if (!writes)
        return -1;
    memcpy(p, saved, n);
    return n;
}
int dkrt_settings_put(const char *k, const void *p, u32 n)
{
    (void)k;
    assert(n == sizeof(saved));
    memcpy(saved, p, n);
    ++writes;
    return 0;
}
static void packet(u16 *out, u32 part, u32 seq, const u32 *snapshot)
{
    u32 w[14] = {PH_REPLY_MAGIC, (seq << 2) | part};
    for (u32 i = 0; i < 12 && part * 12 + i < 32; ++i)
        w[i + 2] = snapshot[part * 12 + i];
    for (u32 i = 0; i < 14; ++i) {
        u32 r = i < 8 ? 17 + i / 2 : 29 + (i - 8) / 2;
        u32 off = (0x1c + 0x54 * r + 0x44 + (i & 1) * 4) / 2;
        out[off] = (u16)w[i];
        out[off + 1] = (u16)(w[i] >> 16);
    }
}
static void timing_start_reason(void)
{
    u32 c[64] = {0}, p[16] = {0};
    const u32 tempo = 14400u << 16;
    p[5] = tempo | 3;
    p[6] = 7;
    p[8] = 16;
    phase_seen = 0;
    timing(c, p, 5400000, 0);
    assert(!(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START)); /* startup is not START */
    p[5] = tempo;
    timing(c, p, 5400000, 0);
    assert((c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_STOPPED) &&
           !(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START));
    p[5] = tempo | 3;
    ++p[4];
    timing(c, p, 5400000, 0);
    u32 epoch = c[PH_C_TIMELINE_EPOCH / 4];
    assert((c[PH_C_TIMING_FLAGS / 4] & (PH_TIMING_START | PH_TIMING_VALID | PH_TIMING_RUNNING)) ==
           (PH_TIMING_START | PH_TIMING_VALID | PH_TIMING_RUNNING));
    for (u32 i = 0; i < 4; ++i) {
        p[9] += 28800;
        timing(c, p, 5400000, 0);
        assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch);
        assert(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START); /* retained reason */
    }
    ++p[4];
    timing(c, p, 5400000, 0);
    assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch + 1);
    assert(!(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START)); /* unrelated running reset */
    /* Every case first establishes a stopped grid, then tries to start.
     * Invalid timing, tempo, period, or simultaneous pattern/scale changes
     * must never authorize the DSP to preserve a pending numbered cue. */
    const u32 cases[][3] = {
        {tempo | 3, 8, 5400000},         /* pattern changed */
        {tempo | 3, 7, 10800000},        /* scale changed */
        {tempo | 2, 7, 5400000},         /* missing native timing validity */
        {tempo | 7, 7, 5400000},         /* native timing discontinuity */
        {tempo | 3, 7, 0},               /* invalid period */
        {3, 7, 5400000},                 /* missing tempo */
        {(36001u << 16) | 3, 7, 5400000} /* tempo above supported range */
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        phase_seen = 0;
        p[4] = 0;
        p[5] = tempo;
        p[6] = 7;
        p[9] = 0;
        timing(c, p, 5400000, 0);
        p[4] = 1;
        p[5] = cases[i][0];
        p[6] = cases[i][1];
        timing(c, p, cases[i][2], 0);
        assert(!(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START));
    }
    phase_seen = 0;
    p[4] = 0;
    p[5] = tempo;
    p[6] = 7;
    p[9] = 0;
    timing(c, p, 5400000, 0);
    p[4] = 1;
    p[5] = tempo | 3;
    timing(c, p, 5400000, 0);
    assert(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START);
    epoch = c[PH_C_TIMELINE_EPOCH / 4];
    p[9] += 21600001;
    timing(c, p, 5400000, 0);
    assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch + 1);
    assert(!(c[PH_C_TIMING_FLAGS / 4] & (PH_TIMING_START | PH_TIMING_VALID)));
}
static void timing_start_precommit(void)
{
    u32 c[64] = {0}, p[16] = {0};
    const u32 tempo = 14400u << 16;
    phase_seen = 0;
    p[4] = 8;
    p[5] = tempo;
    p[6] = 7;
    p[8] = 16;
    p[9] = 0xfffffff0u;
    timing(c, p, 10800000, 0);
    u32 epoch = c[PH_C_TIMELINE_EPOCH / 4], beat = c[PH_C_BEAT_PHASE / 4];
    u32 phase = c[PH_C_MASTER_PHASE / 4];
    assert(clock_valid && start_wait_blocks == 0);
    p[5] = tempo | 2;
    p[9] = 0; /* native phase can reset before the timed commit */
    for (u32 i = 0; i < 2; ++i) {
        timing(c, p, 0, 0);
        assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch);
        assert(c[PH_C_TIMING_FLAGS / 4] ==
               (PH_TIMING_VALID | PH_TIMING_STOPPED | PH_TIMING_TEMPO_VALID));
        assert(c[PH_C_BEAT_PHASE / 4] > beat && c[PH_C_MASTER_PHASE / 4] > phase);
        beat = c[PH_C_BEAT_PHASE / 4];
        phase = c[PH_C_MASTER_PHASE / 4];
        assert(stopped_seen && prev_epoch == 8 && master_ticks == 10800000);
        assert(start_wait_blocks == i + 1);
    }
    ++p[4];
    ++p[3];
    p[5] = tempo | 3;
    timing(c, p, 10800000, 0);
    assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch + 1 && start_wait_blocks == 0);
    assert(c[PH_C_TIMING_FLAGS / 4] ==
           (PH_TIMING_VALID | PH_TIMING_RUNNING | PH_TIMING_TEMPO_VALID | PH_TIMING_START));
    p[9] += 28800;
    timing(c, p, 10800000, 0);
    assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch + 1 && (c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START));

    /* The 65th uncommitted block fails invalid and cannot renew the grace.
     * A later commit can recover clock validity, but cannot preserve ARM. */
    phase_seen = 0;
    p[4] = 20;
    p[3] = 0;
    p[5] = tempo;
    p[6] = 7;
    p[9] = 0;
    timing(c, p, 5400000, 0);
    epoch = c[PH_C_TIMELINE_EPOCH / 4];
    p[5] = tempo | 2;
    for (u32 i = 0; i < START_PRECOMMIT_BLOCKS; ++i) {
        timing(c, p, 0, 0);
        assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch);
        assert(c[PH_C_TIMING_FLAGS / 4] ==
               (PH_TIMING_VALID | PH_TIMING_STOPPED | PH_TIMING_TEMPO_VALID));
    }
    for (u32 i = 0; i < 3; ++i) {
        timing(c, p, 0, 0);
        assert(c[PH_C_TIMING_FLAGS / 4] == (PH_TIMING_RUNNING | PH_TIMING_TEMPO_VALID));
        assert(c[PH_C_TIMELINE_EPOCH / 4] == epoch + 1 && !stopped_seen);
        assert(start_wait_blocks == START_PRECOMMIT_BLOCKS);
    }
    ++p[4];
    p[5] = tempo | 3;
    timing(c, p, 5400000, 0);
    assert(c[PH_C_TIMING_FLAGS / 4] ==
           (PH_TIMING_VALID | PH_TIMING_RUNNING | PH_TIMING_TEMPO_VALID));
    assert(start_wait_blocks == 0);
    p[5] = tempo;
    timing(c, p, 5400000, 0); /* an actual new stop permits a new attempt */
    epoch = c[PH_C_TIMELINE_EPOCH / 4];
    p[5] = tempo | 2;
    timing(c, p, 0, 0);
    assert(start_wait_blocks == 1 && stopped_seen && c[PH_C_TIMELINE_EPOCH / 4] == epoch);

    /* A stale/native-discontinuous clock, an external clock without selected
     * tempo, a new pattern/generation, or a nonzero invalid period is not
     * the specifically identified precommit handoff. */
    const u32 cases[][4] = {{tempo | 6, 7, 30, 0},
                            {2, 7, 30, 0},
                            {tempo | 2, 8, 30, 0},
                            {tempo | 2, 7, 31, 0},
                            {tempo | 2, 7, 30, 1}};
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        phase_seen = 0;
        p[4] = 30;
        p[5] = tempo;
        p[6] = 7;
        p[9] = 0;
        timing(c, p, 5400000, 0);
        p[5] = cases[i][0];
        p[6] = cases[i][1];
        p[4] = cases[i][2];
        timing(c, p, cases[i][3], 0);
        assert(
            !(c[PH_C_TIMING_FLAGS / 4] & (PH_TIMING_VALID | PH_TIMING_STOPPED | PH_TIMING_START)));
        assert(start_wait_blocks == 0);
    }
    /* Invalid previous stopped timing and first-ever running frames also
     * cannot manufacture a usable stopped grid. */
    phase_seen = 0;
    p[4] = 40;
    p[5] = tempo;
    p[6] = 7;
    timing(c, p, 0, 0);
    assert(!clock_valid);
    p[5] = tempo | 2;
    timing(c, p, 0, 0);
    assert(!(c[PH_C_TIMING_FLAGS / 4] & (PH_TIMING_VALID | PH_TIMING_STOPPED | PH_TIMING_START)));
    phase_seen = 0;
    timing(c, p, 0, 0);
    assert(!clock_valid && start_wait_blocks == 0 &&
           !(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_STOPPED));
    /* Multiple skipped start generations must cancel a pending cue, including
     * after otherwise qualified precommit frames. Exact +1 permits wrap. */
    const u32 epochs[][2] = {{50, 52}, {0xffffffffu, 0}};
    for (u32 i = 0; i < 2; ++i) {
        phase_seen = 0;
        p[4] = epochs[i][0];
        p[5] = tempo;
        p[6] = 7;
        p[9] = 0;
        timing(c, p, 5400000, 0);
        p[5] = tempo | 2;
        timing(c, p, 0, 0);
        p[4] = epochs[i][1];
        p[5] = tempo | 3;
        timing(c, p, 5400000, 0);
        assert(!!(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_START) == (i == 1));
    }
}
static void timing_fractional_precision(void)
{
    u32 c[64] = {0}, p[16] = {0}, seed = 1, expected = 0, remainder = 0;
    phase_seen = 0;
    p[5] = (14400u << 16) | 3;
    p[8] = 16;
    timing(c, p, 5400000, 0);
    for (u32 i = 0; i < 4096; ++i) {
        seed = seed * 1664525u + 1013904223u;
        u32 delta = seed % 21600001u;
        p[9] += delta;
        uint64_t numerator = (uint64_t)delta * 65536 + remainder;
        expected += (u32)(numerator / 21600000);
        remainder = (u32)(numerator % 21600000);
        timing(c, p, 5400000, 0);
        assert(c[PH_C_BEAT_PHASE / 4] == expected && beat_rem == remainder);
        assert(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_VALID);
    }
}
int main(void)
{
    phrase_bind(1, 1, 0x20, "fixture/phrase", "TEST");
    assert(dkrt_phrase_active() == 0x20);
    assert(dkrt_phrase_set(PH_P_MASK, 5) == 0);
    assert(dkrt_phrase_set(PH_P_GAIN0 + 2, 0) == 0);
    assert(control[published][PH_C_NEXT_MASK / 4] == 1);
    assert(raw[PH_P_MASK] == 5); /* zero gain does not erase the user's selection */
    assert(dkrt_phrase_set(PH_P_GAIN0 + 2, 100) == 0);
    assert(control[published][PH_C_NEXT_MASK / 4] == 5);
    assert(dkrt_phrase_set(PH_P_FADE, 127) == 0 && raw[PH_P_FADE] == 0);
    dkrt_phrase_action(PH_ACTION_ARM);
    assert(raw[PH_P_REC] == 1);
    assert(dkrt_phrase_set(PH_P_MASK, 8) == -2);
    dkrt_phrase_action(PH_ACTION_CANCEL);
    assert(raw[PH_P_REC] == 0);
    u32 s[32] = {0}, out[32] = {0};
    u16 wire[0xabc / 2] = {0};
    s[0] = PH_STATUS_MAGIC;
    s[1] = 1;
    s[PH_S_HEARTBEAT / 4] = 77;
    s[PH_S_SEQ_END / 4] = 77;
    s[PH_S_OWNER_GENERATION / 4] = generation;
    s[PH_S_HELD_FRAMES / 4] = 48000;
    s[PH_S_HELD_MASK / 4] = 5;
    s[PH_S_STATE / 4] = PH_HELD;
    frame = 100;
    packet(wire, 1, 5, s);
    reply(wire);
    packet(wire, 2, 5, s);
    reply(wire);
    assert(!dkrt_phrase_snapshot(out, 32)); /* partial/out-of-order snapshot is invisible */
    packet(wire, 0, 6, s);
    reply(wire);
    packet(wire, 1, 6, s);
    reply(wire);
    packet(wire, 2, 7, s);
    reply(wire);
    assert(!dkrt_phrase_snapshot(out, 32));
    packet(wire, 2, 6, s);
    reply(wire);
    assert(dkrt_phrase_snapshot(out, 32));
    assert(out[PH_S_HELD_FRAMES / 4] == 48000);
    dkrt_phrase_set(PH_P_FADE, 127);
    assert(raw[PH_P_FADE] == 127);
    /* A router fault restores both audible dry and the displayed fader even
     * while the engine's held status remains coherent and fresh. */
    s[PH_S_ROUTE_FAULT / 4] = 1;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 8, s);
        reply(wire);
    }
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 0);
    /* The old fault reply remains latched until the DSP receives the new
     * FADE gesture. A UI tick before that frame must not erase the rearm. */
    dkrt_phrase_set(PH_P_FADE, 64);
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 64);
    frame += ROUTE_REARM_BLOCKS - 1;
    dkrt_phrase_set(PH_P_FADE, 127); /* further turns cannot extend the grace */
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 127);
    s[PH_S_ROUTE_FAULT / 4] = 0;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 9, s);
        reply(wire);
    }
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 127 && !route_fault_seen && !route_rearm_pending);
    /* A later observed episode forces LIVE immediately again. */
    s[PH_S_ROUTE_FAULT / 4] = 1;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 10, s);
        reply(wire);
    }
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 0);
    dkrt_phrase_set(PH_P_FADE, 64);
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 64);
    frame += ROUTE_REARM_BLOCKS;
    dkrt_phrase_set(PH_P_FADE, 127);
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 0 && !route_rearm_pending);
    /* A still-faulting process can be retried deliberately. CLEAR cancels a
     * retry; other commands neither disable safety nor manufacture recovery. */
    dkrt_phrase_set(PH_P_FADE, 127);
    dkrt_phrase_action(PH_ACTION_CANCEL);
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 127 && route_rearm_pending);
    dkrt_phrase_action(PH_ACTION_CLEAR);
    assert(raw[PH_P_FADE] == 0 && !route_rearm_pending);
    dkrt_phrase_set(PH_P_FADE, 127);
    s[PH_S_ERROR / 4] = PH_ERROR_STORAGE;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 11, s);
        reply(wire);
    }
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 0);
    dkrt_phrase_set(PH_P_FADE, 127);
    s[PH_S_ERROR / 4] = PH_ERROR_LINK;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 12, s);
        reply(wire);
    }
    phrase_control_tick();
    assert(raw[PH_P_FADE] == 0);
    s[PH_S_ROUTE_FAULT / 4] = s[PH_S_ERROR / 4] = 0;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 13, s);
        reply(wire);
    }
    phrase_control_tick();
    dkrt_phrase_set(PH_P_FADE, 127);
    assert(raw[PH_P_FADE] == 127);
    dkrt_phrase_set(PH_P_MASK, 8);
    assert(status[PH_S_HELD_MASK / 4] == 5);
    frame += 750;
    phrase_control_tick();
    assert(!raw[PH_P_FADE]);
    assert(!dkrt_phrase_snapshot(out, 32));
    dkrt_phrase_set(PH_P_TUNE, 900);
    assert(raw[PH_P_TUNE] == 96);
    dkrt_phrase_set(PH_P_LEN, 0);
    assert(raw[PH_P_LEN] == 1);
    s[PH_S_ROUTE_FAULT / 4] = 1;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 14, s);
        reply(wire);
    }
    phrase_control_tick();
    dkrt_phrase_set(PH_P_FADE, 127);
    assert(route_fault_seen && route_rearm_pending);
    phrase_bind(0, 0, 0, 0, 0);
    assert(dkrt_phrase_active() == 0);
    phrase_bind(2, 2, 0x21, "fixture/phrase", "TEST");
    assert(raw[PH_P_MASK] == 8 && !raw[PH_P_FADE] && !raw[PH_P_REC]);
    assert(!route_fault_seen && !route_rearm_pending);
    /* 120 BPM stopped: 375blocks is a quarter second/two master steps. */
    u32 c[64] = {0}, p[16] = {0};
    p[5] = 14400u << 16;
    timing(c, p, 5400000, 0);
    u32 before = c[PH_C_MASTER_PHASE / 4], beat = c[PH_C_BEAT_PHASE / 4];
    for (unsigned i = 0; i < 375; ++i)
        timing(c, p, 5400000, 0);
    assert(c[PH_C_MASTER_PHASE / 4] - before == 131072);
    assert(c[PH_C_BEAT_PHASE / 4] - beat == 32768);
    assert(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_STOPPED);
    /* A tempo change changes rate without jumping musical position. */
    before = c[PH_C_MASTER_PHASE / 4];
    p[5] = 7200u << 16;
    for (unsigned i = 0; i < 750; ++i)
        timing(c, p, 5400000, 0);
    assert(c[PH_C_MASTER_PHASE / 4] - before == 131072);
    /* Running scale is correct on the first block, without a measured cycle. */
    p[5] = (14400u << 16) | 3;
    p[8] = 16;
    p[4] = 1;
    p[3] = 10;
    p[9] = 0;
    timing(c, p, 10800000, 0);
    for (unsigned i = 0; i < 375; ++i) {
        p[9] += 28800;
        if (i == 374) {
            p[3]++;
            p[7]++;
        }
        timing(c, p, 10800000, i == 374 ? p[9] : 0);
    }
    assert(master_ticks == 10800000);
    assert(c[PH_C_MASTER_PHASE / 4] == 65536);
    assert(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_VALID);
    /* Enabling mid-step retains the committed fractional origin. */
    phase_seen = 0;
    p[9] = 5400000;
    p[3] = 12;
    timing(c, p, 10800000, 0);
    assert(c[PH_C_MASTER_PHASE / 4] == 32768);
    p[9] = 10800000;
    p[3]++;
    timing(c, p, 10800000, 10800000);
    assert(c[PH_C_MASTER_PHASE / 4] == 65536);
    /* A stopped grid follows the selected scale; tempo changes remain smooth. */
    p[5] = 14400u << 16;
    timing(c, p, 10800000, 0);
    before = c[PH_C_MASTER_PHASE / 4];
    for (unsigned i = 0; i < 375; ++i)
        timing(c, p, 10800000, 0);
    assert(c[PH_C_MASTER_PHASE / 4] - before == 65536);
    timing(c, p, 0, 0);
    assert(!(c[PH_C_TIMING_FLAGS / 4] & PH_TIMING_VALID));
    timing_start_reason();
    timing_start_precommit();
    timing_fractional_precision();
    return 0;
}
