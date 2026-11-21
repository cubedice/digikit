/* DT2 1.17. All entry points execute with interrupts masked by their shim.
 * Match the existing timed events; never allocate or enqueue a firmware event.
 * The audio ISR sends the preceding frame before dispatching these events.
 * A committed boundary therefore belongs to the next transmitted frame.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define U32(a) (*(volatile u32 *)(a))
#define U16(a) (*(volatile u16 *)(a))
#define N 16
struct marker {
    u32 clock, generation, pattern, step, length, kind, period;
};
static struct marker pending[N];
static u32 head, count, generation, seen, last_step, last_pattern, valid;
volatile u32 pp_position[16];
static u32 external_seen, external_expired, external_stamp;
static u32 committed_period;

/* DTII 1.17 master scheduler: advanced mode uses six900000-tick pulses.
 * Common mode uses the pattern scale. This is musical time, not audio time. */
u32 pp_period(u32 mode, u32 scale)
{
    static const u32 ticks[7] = {2700000, 3600000, 5400000, 7200000, 10800000, 21600000, 43200000};
    return mode == 1 ? 5400000 : scale < 7 ? ticks[scale] : 0;
}

/* Stock transport may freewheel after MIDI clock disappears. Keep that
 * behavior for position clients, but expire the separate instrument clock.
 * Once externally clocked, require fresh pulses until STOP or receive OFF. */
u32 pp_clock_fresh(u32 transport, u32 receive, u32 sources, u32 now, u32 last, u32 sum, u32 samples)
{
    if (transport != 1 || !receive) {
        external_seen = external_expired = 0;
        return 1;
    }
    if (!external_seen && (sources & 2)) {
        external_seen = 1;
        external_stamp = last;
    }
    if (!external_seen)
        return 1;
    if (last != external_stamp) {
        external_stamp = last;
        external_expired = 0;
    }
    if (external_expired)
        return 0; /* counter wrap cannot revive a lost clock */
    u32 limit = samples && samples <= 24 ? sum / samples : 0;
    if (limit > 22000000)
        limit = 22000000;
    limit *= 3;
    if (limit < 5280000)
        limit = 5280000; /* minimum 40 ms, maximum 500 ms */
    external_expired = (u32)(now - last) > limit;
    return !external_expired;
}

/* Exported pure entry points also allow deterministic queue tests. */
void pp_observe_scaled(u32 start, u32 clock, u32 pattern, u32 step, u32 length, u32 period)
{
    if (start) {
        ++generation;
        head = count = seen = valid = 0;
        committed_period = 0;
    }
    if (seen && step == last_step && pattern == last_pattern)
        return;
    seen = 1;
    last_step = step;
    last_pattern = pattern;
    if (!length || step >= length || count == N) {
        valid = 0;
        count = 0;
        return;
    }
    struct marker *m = &pending[(head + count) & (N - 1)];
    m->clock = clock;
    m->generation = generation;
    m->pattern = pattern;
    m->step = step;
    m->length = length;
    m->kind = start ? 7 : 5;
    m->period = period;
    ++count;
}

/* Existing queue-test/consumer entry retains normal-scale semantics. */
void pp_observe(u32 start, u32 clock, u32 pattern, u32 step, u32 length)
{
    pp_observe_scaled(start, clock, pattern, step, length, 5400000);
}

/* The pattern a boundary carries is the one whose step begins there: the playing pattern, not the scheduled one.
 * The stock tick schedules a step about one step ahead; at a change at the pattern's end it moves the scheduled pointer
 * at the edge of the old pattern's last step, one step before the new pattern is heard (finding 365). The playing
 * pattern of track 1's current step is set by the tick when that step becomes current. It is taken at a master step
 * edge (substep 0) and at START; between edges the one taken at the last edge stays, so a track faster than the
 * master grid cannot move it into the middle of a step. Its length, taken with it: the scheduled pattern's (the
 * master override first) when the two are the same; while the scheduled one has moved on, the length taken for the
 * playing one at the edge before; else its own. */
static u32 shown_pattern, shown_length, shown_seen;
void pp_playing(u32 start, u32 edge, u32 playing, u32 scheduled, u32 scheduled_length,
                u32 own_length, u32 out[2])
{
    if (start || edge || !shown_seen) {
        u32 length = playing == scheduled                     ? scheduled_length
                     : shown_seen && playing == shown_pattern ? shown_length
                                                              : own_length;
        shown_pattern = playing;
        shown_length = length;
        shown_seen = 1;
    }
    out[0] = shown_pattern;
    out[1] = shown_length;
}

void pp_commit(u32 clock, u32 kind, u32 block)
{
    if (!count)
        return;
    struct marker *m = &pending[head];
    if ((int)(clock - m->clock) < 0)
        return;
    if (clock != m->clock || kind != m->kind) {
        /* Substep events at the same deadline may precede a start marker. */
        if (clock == m->clock)
            return;
        count = valid = 0;
        return;
    }
    if (valid && pp_position[10] == block) {
        count = valid = 0; /* Two boundaries in one audio block: unrepresentable. */
        return;
    }
    ++pp_position[3];
    pp_position[4] = m->generation;
    pp_position[6] = m->pattern;
    pp_position[7] = m->step;
    pp_position[8] = m->length;
    pp_position[9] = 0;
    pp_position[10] = block;
    pp_position[11] = clock;
    committed_period = m->period;
    head = (head + 1) & (N - 1);
    --count;
    valid = 1;
}

#ifndef PP_QUEUE_TEST
/* The literal of the stock tick's `lea playing,a6` (anchor cf.pattern.site.playing_lit): 16 words, the pattern each
 * track's current step belongs to, which the tick copies when that step becomes current and START sets. Track 1's is
 * the first. */
extern volatile u32 pp_playing_lit;

/* The scheduled pattern: the pointer the tick schedules from, a step ahead of what is heard at a change at the end. */
u32 pp_current_pattern(void)
{
    return U32(0x4316501c);
}

static int pattern_ok(u32 pattern)
{
    return pattern >= 0x40000000 && pattern <= 0x47fe2484 && !(pattern & 1);
}

static u32 pattern_period(u32 pattern)
{
    if (!pattern_ok(pattern))
        return 0;
    return pp_period(*(volatile u8 *)(pattern + 0x1db79), *(volatile u8 *)(pattern + 0x1db7a));
}

/* A pattern's own length (pattern + 0x1db74), or 0 for unknown. */
static u32 own_length(u32 pattern)
{
    int n = pattern_ok(pattern) ? (short)U16(pattern + 0x1db74) : 0;
    return n > 1 ? (u32)n : 0;
}

u32 pp_record_steps(void)
{
    int master = (short)U16(0x43165130);
    u32 steps = U32(0x43164f94) == 1 && valid ? pp_position[8]
                : master > 1 ? (u32)master : own_length(pp_current_pattern());
    return steps <= 1024 ? steps : 0;
}

/* Duration for a recorder at the current tempo. Bars 0 means the playing
 * pattern (loaded pattern while stopped). Return 0 for an unknown cycle. */
u32 pp_record_frames(u32 bars)
{
    u32 tempo = U32(0x402b67fc);
    if (tempo < 3600 || tempo > 36000)
        return 0;
    u32 steps = bars * 4, ticks = 21600000;
    if (!bars) {
        u32 running = U32(0x43164f94) == 1 && valid;
        u32 pattern = running ? pp_position[6] : pp_current_pattern();
        steps = pp_record_steps();
        ticks = running ? committed_period : pattern_period(pattern);
    }
    if (!steps || steps > 1024 || !ticks)
        return 0;
    /* 48 kHz times 60 times 120 / 21600000 = 16. Divide before
     * multiplying by steps to keep even a slow, long cycle in 32 bits. */
    u32 numer = ticks * 16;
    return (numer / tempo) * steps + ((numer % tempo) * steps + tempo / 2) / tempo;
}

/* Four recorder timing words published in the same audio ISR as pp_position.
 * The selected duration is revalidated at the boundary, and beat quantization
 * follows quarter notes independently of pattern length or track scale. */
void pp_record_timing(u32 out[4])
{
    static u32 prev_tempo, prev_steps, prev_ticks, pattern_frames, bar_frames;
    static u32 prev_phase, prev_generation, fraction, beats, tracking;
    u32 running = U32(0x43164f94) == 1 && valid;
    u32 pat = running ? pp_position[6] : pp_current_pattern();
    int master = (short)U16(0x43165130);
    u32 steps = running ? pp_position[8] : master > 1 ? (u32)master : own_length(pat);
    u32 ticks = running ? committed_period : pattern_period(pat);
    u32 tempo = U32(0x402b67fc);
    if (tempo != prev_tempo || ticks != prev_ticks || steps != prev_steps) {
        pattern_frames = pp_record_frames(0);
        bar_frames = pp_record_frames(1);
        prev_tempo = tempo;
        prev_ticks = ticks;
        prev_steps = steps;
    }
    u32 phase = U32(0x80004880);
    if (running) {
        if (!tracking || prev_generation != pp_position[4]) {
            prev_generation = pp_position[4];
            prev_phase = pp_position[11];
            fraction = 0;
            ++beats;
            tracking = 1;
        }
        int delta = (int)(phase - prev_phase);
        if (delta > 0) {
            prev_phase = phase;
            fraction += (u32)delta;
            beats += fraction / 21600000u;
            fraction %= 21600000u;
        }
    } else
        tracking = 0;
    out[0] = 0x524c5431;
    out[1] = pattern_frames;
    out[2] = bar_frames;
    out[3] = beats;
}

void pp_snapshot(u32 start)
{
    u32 scheduled = U32(0x4316501c), playing = U32(pp_playing_lit), length = 0, shown[2];
    if (pattern_ok(scheduled)) {
        int n = (short)U16(0x43165130);
        length = n > 1 ? (u32)n : own_length(scheduled);
    }
    if (!pattern_ok(playing))
        playing = scheduled;
    /* substep 0: a master step edge */
    pp_playing(start, *(volatile u8 *)0x43164f9a == 0, playing, scheduled, length,
               own_length(playing), shown);
    pp_observe_scaled(start, U32(0x43158f7c), shown[0], U16(0x43164f9c), shown[1],
                      pattern_period(shown[0]));
}

void pp_due(u32 clock, u32 kind)
{
    pp_commit(clock, kind, U32(0x40966840));
}

void pp_write_frame_impl(u32 queue, u32 suffix)
{
    volatile u16 *out = (volatile u16 *)(queue + 4 * suffix + 2);
    u32 transport = U32(0x43164f94);
    if (transport != 1) {
        valid = count = seen = 0;
    }
    pp_position[0] = 0x50504f53;
    pp_position[1] = 2;
    ++pp_position[2];
    pp_position[5] = transport == 1 ? 2 | valid : 0;
    pp_position[12] = U32(0x40966840);
    pp_position[13] = U16(0x43164f9c);
    pp_position[14] = transport;
    u32 sum = 0x7b91d6e3;
    for (u32 i = 0; i < 15; ++i)
        sum ^= pp_position[i];
    pp_position[15] = sum;
    /* Wire v4 adds the selected stock tempo (1/120 BPM) in flag bits16..31.
     * It remains available while stopped. Wire v3 added musical phase in
     * reserved word 9. Keep the public CF
     * position record at ABI 2. The receiver normalizes it for old effects
     * and publishes a separate versioned clock capability. */
    u32 phase = U32(0x80004880);
    u32 stale = pp_clock_fresh(transport, U32(0x429368d6), U32(0x402b6800), U32(0xfc07000c),
                               U32(0x44e086e4), U32(0x44e086d4), U32(0x44e086dc))
                    ? 0
                    : 4;
    u32 tempo = U32(0x402b67fc);
    if (tempo < 3600 || tempo > 36000)
        tempo = 0;
    u32 clock_flags = stale | (tempo << 16);
    u32 wire_sum = sum ^ 2 ^ 4 ^ phase ^ clock_flags;
    for (u32 i = 0; i < 16; ++i) {
        u32 v = i == 1    ? 4
                : i == 5  ? pp_position[5] | clock_flags
                : i == 9  ? phase
                : i == 15 ? wire_sum
                          : pp_position[i];
        out[0] = (u16)v;
        out[2] = (u16)(v >> 16);
        out += 4;
    }
}
#endif
