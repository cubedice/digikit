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
static u32 cell_offset(u32 j)
{
    u32 row = j < 8 ? 17 + j / 2 : 29 + (j - 8) / 2;
    return (0x1c + 0x54 * row + 0x44 + (j & 1) * 4) / 2;
}
static void cell(u32 j, u32 value)
{
    u32 off = cell_offset(j);
    reply_copy[off] = (u16)value;
    reply_copy[off + 1] = (u16)(value >> 16);
}
static u32 read_cell(u32 j)
{
    u32 off = cell_offset(j);
    return reply_copy[off] | ((u32)reply_copy[off + 1] << 16);
}
static void seal_carrier(void)
{
    u32 check = LS_WIRE_MAGIC;
    for (u32 j = 1; j < LS_WIRE_WORDS; ++j) {
        if (j != LS_WIRE_CHECK)
            check = ((check << LS_WIRE_ROT) | (check >> (32 - LS_WIRE_ROT))) ^ read_cell(j);
    }
    cell(LS_WIRE_CHECK, check);
}
/* A reply that carries the loop effect's status: heartbeat, state bits, the loop's frames. */
static void carrier(u32 heartbeat, u32 state, u32 frames)
{
    memset(reply_copy, 0xa5, sizeof(reply_copy));
    for (u32 j = 0; j < LS_WIRE_WORDS; ++j)
        cell(j, 0);
    cell(0, LS_WIRE_MAGIC);
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
    if (read_cell(0) == LS_WIRE_MAGIC)
        seal_carrier();
    loop_reply_frame(reply_copy);
}
static void wave_history(void)
{
    u16 shape[LS_WAVE_COLUMNS], frozen[LS_WAVE_COLUMNS];
    u32 seq = 1000, length = 32 * 512;
    for (u32 bin = 1; bin <= 32; ++bin) {
        carrier(++seq, LS_TRACK, 0);
        cell(LS_WAVE_T, bin * 512);
        cell(LS_NEXT_FRAMES, length);
        cell(LS_WAVE_PEAK, bin <= 16 ? 0xe000 : 0xf0);
        block();
    }
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    assert(shape[0] == 0xee00 && shape[59] == 0xee00 && shape[60] == 0xff00 &&
           shape[119] == 0xff00);
    /* An off-window capture; the leading half was not present after a rapid
     * replacement. A negative-left transient in the last partial bin is kept. */
    carrier(++seq, LS_LOOP | (5u << LS_COUNT_SHIFT), length);
    cell(LS_WAVE_T, length + 96);
    cell(LS_WAVE_EDGE, length + 64);
    cell(LS_WAVE_PEAK, (15u << 16) | 0xf0);
    cell(LS_WAVE_SILENT, length / 2);
    block();
    loop_ui_tick();
    dkrt_loop_wave_peaks(frozen);
    assert(frozen[0] == 0 && frozen[59] == 0 && frozen[62] == 0xff00 && frozen[119] == 0xffff);
    for (u32 bin = 33; bin < 132; ++bin) {
        carrier(++seq, LS_LOOP | LS_STOPPED | (5u << LS_COUNT_SHIFT), length);
        cell(LS_WAVE_T, bin * 512);
        block();
    }
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    assert(!memcmp(shape, frozen, sizeof(shape)));
    carrier(++seq, LS_TRACK, 0);
    cell(LS_NEXT_FRAMES, length);
    cell(LS_WAVE_T, 132 * 512);
    block();
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    for (unsigned x = 0; x < LS_WAVE_COLUMNS; ++x)
        assert(shape[x] == 0);

    /* A long loop crosses the 32-bit frame counter. Transients on each end
     * survive the coarser histories while the middle and silent side stay flat. */
    length = 1031680;
    const u32 start = 0u - length / 2;
    carrier(++seq, LS_TRACK, 0);
    cell(LS_WAVE_T, start);
    cell(LS_NEXT_FRAMES, length);
    block();
    for (u32 at = 512; at <= length; at += 512) {
        carrier(++seq, LS_TRACK, 0);
        cell(LS_WAVE_T, start + at);
        cell(LS_NEXT_FRAMES, length);
        cell(LS_WAVE_PEAK, at == 1024 ? 0x0d00 : at == length - 512 ? 0x000e : 0);
        block();
    }
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    assert(shape[0] == 0x00dd && shape[60] == 0 && shape[119] == 0x00ee);
    for (u32 i = 0; i < STALE; ++i)
        loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    for (unsigned x = 0; x < LS_WAVE_COLUMNS; ++x)
        assert(shape[x] == 0);

    /* Each of these 120 independent source intervals occupies exactly one pixel. */
    const u32 origin = 1u << 23;
    length = LS_WAVE_COLUMNS * 512;
    carrier(++seq, LS_TRACK | LS_WAVE_PRECISE, 0);
    cell(LS_WAVE_T, origin);
    block();
    for (u32 bin = 1; bin <= LS_WAVE_COLUMNS; ++bin) {
        carrier(++seq, LS_TRACK | LS_WAVE_PRECISE, 0);
        cell(LS_WAVE_T, origin + bin * 512);
        cell(LS_NEXT_FRAMES, length);
        cell(LS_WAVE_PEAK, ((121 - bin) << 8) | bin);
        block();
    }
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    for (u32 x = 0; x < LS_WAVE_COLUMNS; ++x)
        assert(shape[x] == (((120 - x) << 8) | (x + 1)));

    /* A precise packet after the capture edge carries both the new rolling
     * interval and the retained partial. Only the partial reaches the loop. */
    carrier(++seq, LS_LOOP | LS_WAVE_PRECISE | (6u << LS_COUNT_SHIFT), length);
    cell(LS_WAVE_T, origin + length + 512);
    cell(LS_WAVE_EDGE, origin + length + 64);
    cell(LS_WAVE_PEAK, (0x9365u << 16) | 0xffff);
    block();
    loop_ui_tick();
    dkrt_loop_wave_peaks(frozen);
    assert(frozen[119] == 0x9378);
}

static void wave_history_gaps(void)
{
    u32 seq = 100, origin = 1u << 24;
    const u32 fine_span = LS_WAVE_COLUMNS * 512;
    const u32 precise = LS_TRACK | LS_WAVE_PRECISE;
    u16 shape[LS_WAVE_COLUMNS], same_span[LS_WAVE_COLUMNS];

    /* Cross the 128-slot ring and miss bin 129, whose slot still holds the peak of bin 1. */
    for (u32 bin = 0; bin <= 130; ++bin) {
        if (bin == 129)
            continue;
        carrier(++seq, precise, 0);
        cell(LS_WAVE_T, origin + bin * 512);
        cell(LS_NEXT_FRAMES, fine_span);
        cell(LS_WAVE_PEAK, bin == 130 ? 0x6688 : 0x4411);
        block();
    }
    loop_ui_tick();
    assert(dkrt_loop_wave_peaks(shape) == 10);
    for (u32 x = 0; x < LS_WAVE_COLUMNS; ++x)
        assert(shape[x] == (x == 8 ? 0 : x == 9 ? 0x6688 : 0x4411));

    /* No new viewport estimate while the transport re-establishes its position. Keep the previous width. */
    carrier(++seq, LS_WAVE_PRECISE, 0);
    cell(LS_WAVE_T, origin + 130 * 512);
    block();
    loop_ui_tick();
    dkrt_loop_wave_peaks(same_span);
    assert(!memcmp(shape, same_span, sizeof(shape)));

    /* At about five seconds per view, each column covers four fine bins. An older part of the recording remains
     * through a telemetry outage. A smaller hole within the last coarse bin does not erase either known peak. */
    origin = 1u << 25;
    const u32 wide_span = LS_WAVE_COLUMNS * 2048;
    for (u32 bin = 0; bin <= LS_WAVE_COLUMNS * 4; ++bin) {
        if ((bin >= 341 && bin <= 384) || bin == 478)
            continue;
        carrier(++seq, precise, 0);
        cell(LS_WAVE_T, origin + bin * 512);
        cell(LS_NEXT_FRAMES, wide_span);
        u32 peak = bin < 477 ? 0x2233 : bin == 477 ? 0x9000 : bin == 479 ? 0x0075 : 0;
        cell(LS_WAVE_PEAK, peak);
        block();
    }
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    for (u32 x = 0; x < LS_WAVE_COLUMNS - 1; ++x)
        assert(shape[x] == (x >= 85 && x < 96 ? 0 : 0x2233));
    assert(shape[119] == 0x9075);

    /* The last two pre-capture bins never arrived. The capture reply has a newer rolling bin and the partial at
     * the edge. Keep the preceding recording and the partial without adding the newer rolling peak to the loop. */
    origin = 1u << 26;
    for (u32 bin = 0; bin <= 118; ++bin) {
        carrier(++seq, precise, 0);
        cell(LS_WAVE_T, origin + bin * 512);
        cell(LS_NEXT_FRAMES, fine_span);
        cell(LS_WAVE_PEAK, 0x2255);
        block();
    }
    carrier(++seq, LS_LOOP | LS_WAVE_PRECISE | (10u << LS_COUNT_SHIFT), fine_span);
    cell(LS_WAVE_T, origin + fine_span + 512);
    cell(LS_WAVE_EDGE, origin + fine_span + 64);
    cell(LS_WAVE_PEAK, (0x7f09u << 16) | 0xffff);
    block();
    loop_ui_tick();
    dkrt_loop_wave_peaks(shape);
    assert(shape[0] == 0x2255 && shape[117] == 0x2255 && shape[118] == 0 && shape[119] == 0x7f09);
}

static void wave_print(void)
{
    u32 seq = 1200, origin = 1u << 27;
    const u32 span = LS_WAVE_COLUMNS * 512;
    u16 shape[LS_WAVE_COLUMNS];
    const u32 precise = LS_TRACK | LS_WAVE_PRECISE;
    carrier(++seq, precise, 0);
    cell(LS_WAVE_T, origin);
    cell(LS_NEXT_FRAMES, span);
    block();
    loop_ui_tick();
    assert(dkrt_loop_wave_peaks(shape) == LS_WAVE_COLUMNS);
    /* Each new bin changes only the column the cursor just passed. */
    for (u32 bin = 1; bin <= LS_WAVE_COLUMNS; ++bin) {
        carrier(++seq, precise, 0);
        cell(LS_WAVE_T, origin + bin * 512);
        cell(LS_NEXT_FRAMES, span);
        cell(LS_WAVE_PEAK, bin);
        block();
        loop_ui_tick();
        assert(dkrt_loop_wave_peaks(shape) == bin % LS_WAVE_COLUMNS);
        for (u32 x = 0; x < LS_WAVE_COLUMNS; ++x)
            assert(shape[x] == (x < bin ? x + 1 : 0));
    }
    /* On wrap, the first new bin overwrites the first column. The previous
     * pass stays visible ahead of the cursor. A tiny estimate change stays put. */
    carrier(++seq, precise, 0);
    cell(LS_WAVE_T, origin + span + 512);
    cell(LS_NEXT_FRAMES, span + 128);
    cell(LS_WAVE_PEAK, 0x7722);
    block();
    loop_ui_tick();
    assert(dkrt_loop_wave_peaks(shape) == 1 && shape[0] == 0x7722);
    for (u32 x = 1; x < LS_WAVE_COLUMNS; ++x)
        assert(shape[x] == x + 1);
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
    /* A carrier with a damaged checksum or the old magic is refused. */
    carrier(112, LS_TRACK, 0);
    seal_carrier();
    cell(LS_WIRE_CHECK, read_cell(LS_WIRE_CHECK) ^ 1u);
    loop_reply_frame(reply_copy);
    carrier(116, LS_TRACK, 0);
    cell(0, 0x4f564c32);
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

static void reply_order(void)
{
    u32 out[LS_WORDS];
    u16 shape[LS_WAVE_COLUMNS], frozen[LS_WAVE_COLUMNS];
    const u32 length = 1024;
    const u32 old_state = LS_TRACK | LS_WAVE_PRECISE | (7u << LS_COUNT_SHIFT);
    const u32 held_state = LS_LOOP | LS_HELD | LS_WAVE_PRECISE | (8u << LS_COUNT_SHIFT);
    const u32 pre_capture = 10002, capture = 10004, drop = 10008;
    for (u32 bin = 0; bin <= 2; ++bin) {
        carrier(10000 + bin, old_state, 0);
        cell(LS_WAVE_T, bin * 512);
        cell(LS_WAVE_PEAK, bin == 1 ? 0x4411 : 0x3366);
        block();
    }
    loop_ui_tick();

    /* A pre-capture reply may reappear before the UI takes the capture. */
    carrier(capture, held_state, length);
    cell(LS_WAVE_T, length);
    cell(LS_WAVE_EDGE, length);
    block();
    carrier(pre_capture, old_state, 0);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == capture &&
           out[LS_STATE] == held_state && out[LS_LOOP_FRAMES] == length);
    assert(dkrt_loop_get(LP_SEEN) == 8 && dkrt_loop_get(LP_XFAD) == LP_MAX);
    dkrt_loop_wave_peaks(frozen);
    assert(frozen[0] == 0x4411 && frozen[LS_WAVE_COLUMNS - 1] == 0x3366);

    /* A late reply also cannot replace a published capture or reset the performer's crossfade. */
    dkrt_loop_xfad(41);
    u32 updates = loop_control_updates;
    carrier(pre_capture, old_state, 0);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == capture &&
           out[LS_STATE] == held_state);
    assert(dkrt_loop_get(LP_SEEN) == 8 && dkrt_loop_get(LP_XFAD) == 41 &&
           loop_control_updates == updates);
    dkrt_loop_wave_peaks(shape);
    assert(!memcmp(shape, frozen, sizeof(shape)));

    /* A torn reply can look like a newer drop even when length and state agree. */
    carrier(capture + 1, held_state, length);
    seal_carrier();
    cell(LS_STATE, LS_TRACK | (8u << LS_COUNT_SHIFT));
    cell(LS_LOOP_FRAMES, 0);
    loop_reply_frame(reply_copy);
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == capture &&
           out[LS_STATE] == held_state && loop_control_updates == updates);
    dkrt_loop_wave_peaks(shape);
    assert(!memcmp(shape, frozen, sizeof(shape)));

    /* A real newer drop is visible, even when the previous held reply arrives last. */
    carrier(drop, LS_TRACK | (8u << LS_COUNT_SHIFT), 0);
    block();
    carrier(capture, held_state, length);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == drop && !(out[LS_STATE] & LS_LOOP));
    assert(dkrt_loop_get(LP_SEEN) == 8 && dkrt_loop_get(LP_XFAD) == 41 &&
           loop_control_updates == updates + 1);

    /* A new forward heartbeat, armed flag and nonzero length can all be plausible in a mixed reply. */
    const u32 fake_loop = LS_LOOP | LS_HELD | (9u << LS_COUNT_SHIFT);
    carrier(drop + 1, LS_TRACK | (8u << LS_COUNT_SHIFT), 0);
    seal_carrier();
    cell(LS_HEARTBEAT, drop + 4000);
    cell(LS_STATE, fake_loop | LS_ARMED);
    cell(LS_LOOP_FRAMES, length);
    loop_reply_frame(reply_copy);
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == drop &&
           !(out[LS_STATE] & (LS_LOOP | LS_ARMED)));
    assert(dkrt_loop_get(LP_SEEN) == 8 && dkrt_loop_get(LP_XFAD) == 41 &&
           loop_control_updates == updates + 1);

    /* A malformed newer carrier with the LOOP bit but no frames is not a capture or a heartbeat.
     * The mixed carrier above already used the first pass after the drop. */
    for (u32 i = 1; i < STALE; ++i) {
        carrier(drop, LS_TRACK | (8u << LS_COUNT_SHIFT), 0);
        block();
        carrier(pre_capture, old_state, 0);
        block();
        carrier(drop + 1 + i, fake_loop, 0);
        block();
        loop_ui_tick();
        assert((dkrt_loop_status(out) != 0) == (i + 1 < STALE));
        assert(out[LS_HEARTBEAT] == drop && !(out[LS_STATE] & LS_LOOP));
        assert(dkrt_loop_get(LP_SEEN) == 8 && dkrt_loop_get(LP_XFAD) == 41);
    }
    assert(dkrt_loop_status(out) == 0 && out[LS_HEARTBEAT] == drop &&
           loop_control_updates == updates + 1);

    /* A stale link can restart low. Once the next status is pending, keep it over an earlier restart reply. */
    const u32 restarted = LS_LOOP | (9u << LS_COUNT_SHIFT);
    carrier(5, LS_TRACK, 0);
    block();
    carrier(8, restarted, length);
    block();
    carrier(6, LS_TRACK, 0);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == 8 && out[LS_STATE] == restarted);
    assert(dkrt_loop_get(LP_SEEN) == 9 && dkrt_loop_get(LP_XFAD) == LP_MAX);

    /* Move near the serial wrap after another link loss, then cross it while live. */
    for (u32 i = 0; i < STALE; ++i)
        loop_ui_tick();
    carrier(UINT32_MAX - 2, restarted, length);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == UINT32_MAX - 2);
    carrier(UINT32_MAX, restarted, length);
    block();
    carrier(0, restarted, length);
    block();
    carrier(UINT32_MAX - 1, LS_TRACK, 0);
    block();
    loop_ui_tick();
    assert(dkrt_loop_status(out) == 1 && out[LS_HEARTBEAT] == 0 && out[LS_STATE] == restarted);
    assert(!masked);
}

/* A status as the engine publishes it: heartbeat, state bits, the count of loops. */
static void report(u32 heartbeat, u32 bits, u32 count)
{
    u32 words[LS_WORDS] = {0};
    words[LS_MAGIC] = LS_MAGIC_VALUE;
    words[LS_HEARTBEAT] = heartbeat;
    words[LS_STATE] = bits | (count << LS_COUNT_SHIFT);
    words[LS_LOOP_FRAMES] = bits & LS_LOOP ? 96000 : 0;
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
    wave_history();
    reply_order();
    wave_history_gaps();
    wave_print();
    puts("loop control: ok");
    return 0;
}
