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
struct marker { u32 clock, generation, pattern, step, length, kind; };
static struct marker pending[N];
static u32 head, count, generation, seen, last_step, last_pattern, valid;
volatile u32 pp_position[16];
static u32 external_seen, external_expired, external_stamp;

/* Stock transport may freewheel after MIDI clock disappears. Keep that
 * behavior for position clients, but expire the separate instrument clock.
 * Once externally clocked, require fresh pulses until STOP or receive OFF. */
u32 pp_clock_fresh(u32 transport, u32 receive, u32 sources, u32 now,
                   u32 last, u32 sum, u32 samples) {
    if (transport != 1 || !receive) { external_seen = external_expired = 0; return 1; }
    if (!external_seen && (sources & 2)) {
        external_seen = 1;
        external_stamp = last;
    }
    if (!external_seen) return 1;
    if (last != external_stamp) { external_stamp = last; external_expired = 0; }
    if (external_expired) return 0; /* counter wrap cannot revive a lost clock */
    u32 limit = samples && samples <= 24 ? sum / samples : 0;
    if (limit > 22000000) limit = 22000000;
    limit *= 3;
    if (limit < 5280000) limit = 5280000; /* minimum 40 ms, maximum 500 ms */
    external_expired = (u32)(now - last) > limit;
    return !external_expired;
}

/* Exported pure entry points also allow deterministic queue tests. */
void pp_observe(u32 start, u32 clock, u32 pattern, u32 step, u32 length) {
    if (start) {
        ++generation;
        head = count = seen = valid = 0;
    }
    if (seen && step == last_step && pattern == last_pattern) return;
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
    ++count;
}

void pp_commit(u32 clock, u32 kind, u32 block) {
    if (!count) return;
    struct marker *m = &pending[head];
    if ((int)(clock - m->clock) < 0) return;
    if (clock != m->clock || kind != m->kind) {
        /* Substep events at the same deadline may precede a start marker. */
        if (clock == m->clock) return;
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
    head = (head + 1) & (N - 1);
    --count;
    valid = 1;
}

#ifndef PP_QUEUE_TEST
u32 pp_current_pattern(void) { return U32(0x4316501c); }

void pp_snapshot(u32 start) {
    u32 pattern = U32(0x4316501c), length = 0;
    if (pattern >= 0x40000000 && pattern <= 0x47fe248a && !(pattern & 1)) {
        int n = (short)U16(0x43165130);
        if (n <= 1) n = (short)U16(pattern + 0x1db74);
        if (n > 1) length = (u32)n;
    }
    pp_observe(start, U32(0x43158f7c), pattern, U16(0x43164f9c), length);
}

void pp_due(u32 clock, u32 kind) { pp_commit(clock, kind, U32(0x40966840)); }

void pp_write_frame_impl(u32 queue, u32 suffix) {
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
    for (u32 i = 0; i < 15; ++i) sum ^= pp_position[i];
    pp_position[15] = sum;
    /* Wire v3 adds musical phase in reserved word 9. Keep the public CF
     * position record at ABI 2. The receiver normalizes it for old effects
     * and publishes a separate versioned clock capability. */
    u32 phase = U32(0x80004880);
    u32 stale = pp_clock_fresh(transport, U32(0x429368d6), U32(0x402b6800),
        U32(0xfc07000c), U32(0x44e086e4), U32(0x44e086d4), U32(0x44e086dc)) ? 0 : 4;
    u32 wire_sum = sum ^ 2 ^ 3 ^ phase ^ stale;
    for (u32 i = 0; i < 16; ++i) {
        u32 v = i == 1 ? 3 : i == 5 ? pp_position[5] | stale : i == 9 ? phase : i == 15 ? wire_sum : pp_position[i];
        out[0] = (u16)v;
        out[2] = (u16)(v >> 16);
        out += 4;
    }
}
#endif
