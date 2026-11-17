/* UI settings and a bounded per-block phrase sideband. No audio reads on CF. */
#include <dk/phrase.h>
typedef uint32_t u32;
typedef uint16_t u16;
extern u32 dkrt_irq_mask(void);
extern void dkrt_irq_restore(u32);
extern int dkrt_settings_get(const char *, void *, u32);
extern int dkrt_settings_put(const char *, const void *, u32);
extern void pp_phrase_timing(u32 *);

static u32 raw[PH_PARAM_COUNT], control[2][PH_CONTROL_WORDS];
static volatile u32 published;
static u32 owner, page_code, row_index, generation, command, action, selection;
static u32 frame, reply_frame, assembled_seq, fragments, status[PH_STATUS_WORDS];
static u32 assembling[PH_STATUS_WORDS];
static char key[48];
static char title[24];
static u32 phase_seen, prev_phase, prev_boundary, prev_epoch, prev_pattern;
static u32 beat_q16, beat_rem, master_q16, boundary_phase, master_ticks = 5400000;
static u32 local_phase, stopped_seen, timeline, timeline_start;
static u32 clock_valid, start_wait_blocks;
enum { START_PRECOMMIT_BLOCKS = 64 };
static u32 route_fault_seen, route_rearm_pending, route_rearm_frame;
/* A healthy control/reply roundtrip fits well inside this 43ms window. A
 * persistent fault must not leave the displayed fader above the LIVE route. */
enum { ROUTE_REARM_BLOCKS = 64 };

static u32 bits(float v)
{
    union {
        float f;
        u32 u;
    } x;
    x.f = v;
    return x.u;
}
static u32 limit(u32 p)
{
    if (p == PH_P_REC || p == PH_P_FROM)
        return 1;
    if (p == PH_P_MODE)
        return 2;
    if (p == PH_P_TIME)
        return PH_TIME_CHOICES - 1;
    if (p == PH_P_TAKE || p == PH_P_CUE || p == PH_P_SCAN)
        return 128;
    if (p == PH_P_STRT)
        return 126;
    if (p == PH_P_RATE)
        return 12;
    if (p == PH_P_TUNE)
        return 96;
    if (p == PH_P_SHAP)
        return 3;
    if (p == PH_P_MASK)
        return PH_INPUT_MASK;
    return 127;
}
static void defaults(void)
{
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i)
        raw[i] = 0;
    raw[PH_P_LEN] = 127;
    raw[PH_P_SIZE] = 32;
    raw[PH_P_RATE] = 4;
    raw[PH_P_TUNE] = 48;
    raw[PH_P_SCAN] = 64;
    raw[PH_P_WDTH] = 64;
    raw[PH_P_DEC] = 40;
    raw[PH_P_LPF] = 127;
    raw[PH_P_MASK] = 1;
    for (u32 i = PH_P_GAIN0; i <= PH_P_EXT_GAIN; ++i)
        raw[i] = 127;
    /* The tape: a dotted eighth, a third of the way up the feedback, both sides alike, head and grains both full. */
    raw[PH_P_TIME] = 8;
    raw[PH_P_FDBK] = 45;
    raw[PH_P_SKEW] = 64;
    raw[PH_P_BAL] = 64;
}
static void save(void)
{
    /* Versioned byte encoding, independent of host endianness. Volatile REC,
     * FADE and captured audio never survive reboot. */
    uint8_t value[4 + PH_PARAM_COUNT * 4];
    value[0] = 'P';
    value[1] = 'H';
    value[2] = 2;
    value[3] = PH_PARAM_COUNT;
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i) {
        u32 v = (i == PH_P_REC || i == PH_P_FADE) ? 0 : raw[i];
        for (u32 j = 0; j < 4; ++j)
            value[4 + 4 * i + j] = (uint8_t)(v >> (24 - 8 * j));
    }
    dkrt_settings_put(key, value, sizeof(value));
}
static void load(void)
{
    uint8_t value[4 + PH_PARAM_COUNT * 4];
    defaults();
    if (dkrt_settings_get(key, value, sizeof(value)) != (int)sizeof(value) || value[0] != 'P' ||
        value[1] != 'H' || value[2] != 2 || value[3] != PH_PARAM_COUNT)
        return;
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i) {
        u32 v = 0;
        for (u32 j = 0; j < 4; ++j)
            v = (v << 8) | value[4 + 4 * i + j];
        raw[i] = v > limit(i) ? limit(i) : v;
    }
    raw[PH_P_REC] = raw[PH_P_FADE] = 0;
    if (!raw[PH_P_LEN])
        raw[PH_P_LEN] = 1;
}
/* 2^(semitones/12), bounded half-semitone range; coefficient multiplication
 * only on a UI edit, never in the audio ISR. */
static float pitch(u32 n)
{
    float v = 0.25f;
    for (u32 i = 0; i < n; ++i)
        v *= 1.02930223664f;
    return v;
}
/* A 0..127 control as a signed ten-bit field of the packed tape word. CENTRED: 64 is 0 and the ends are -512 and 511
 * (the ends switch a side off exactly); else 0..127 is 0..508. */
static u32 field(u32 value, u32 centred)
{
    int v = centred ? (value >= 127 ? 511 : ((int)value - 64) * 8) : (int)value * 4;
    return (u32)v & 0x3ffu;
}
/* The delay in frames for the TIME choice at TEMPO (BPM * 120): ticks * 3 600 000 / tempo, in integers (this runs
 * in the block interrupt). Without a tempo the last value stays. The engine bounds it to its tape. */
static u32 tape_time = 24000;
static u32 tape_frames(u32 tempo)
{
    static const u16 ticks[PH_TIME_CHOICES] = {12, 16, 18,  24,  32,  36,  48,  64,
                                               72, 96, 128, 144, 192, 288, 384, 768};
    u32 choice = raw[PH_P_TIME];
    if (tempo && choice < PH_TIME_CHOICES)
        tape_time = (u32)ticks[choice] * 3600000u / tempo;
    return tape_time;
}
static void publish(void)
{
    u32 next = published ^ 1, *c = control[next];
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i)
        c[i] = 0;
#define W(name, v) c[(name) / 4] = (v)
    W(PH_C_MAGIC, PH_CONTROL_MAGIC);
    W(PH_C_VERSION, PH_VERSION);
    W(PH_C_SIZE, PH_CONTROL_WORDS * 4);
    W(PH_C_ENABLED, owner != 0);
    W(PH_C_CMD_SEQ, command);
    W(PH_C_ACTION, action);
    W(PH_C_FROM, raw[PH_P_FROM]);
    W(PH_C_TAKE_STEPS, raw[PH_P_TAKE]);
    W(PH_C_CUE, raw[PH_P_CUE]);
    W(PH_C_SELECTION_EPOCH, selection);
    u32 mask = raw[PH_P_MASK];
    for (u32 i = 0; i < 18; ++i) {
        u32 g = raw[i < 16 ? PH_P_GAIN0 + i : PH_P_EXT_GAIN];
        if (!g)
            mask &= ~(1u << i);
        c[PH_C_GAINS / 4 + i] = bits((float)g / 127.0f);
    }
    W(PH_C_NEXT_MASK, mask);
    W(PH_C_FADE, bits((float)raw[PH_P_FADE] / 127.0f));
    W(PH_C_CROP_START, bits((float)raw[PH_P_STRT] / 127.0f));
    W(PH_C_CROP_LEN, bits((float)raw[PH_P_LEN] / 127.0f));
    u32 sz = raw[PH_P_SIZE];
    W(PH_C_SIZE_FRAMES, 240 + 23760 * sz * sz / (127 * 127));
    static const u32 rates[13] = {0,    262144, 131072, 65536, 32768, 16384, 8192,
                                  4096, 43691,  21845,  10923, 5461,  2731};
    W(PH_C_RATE_BEATS, rates[raw[PH_P_RATE]]);
    W(PH_C_PITCH_RATIO, bits(pitch(raw[PH_P_TUNE])));
    W(PH_C_SHAPE, raw[PH_P_SHAP]);
    W(PH_C_SPRAY, bits((float)raw[PH_P_SPRY] / 127.0f));
    W(PH_C_JITTER, bits((float)raw[PH_P_JITR] / 127.0f));
    W(PH_C_POS, bits((float)raw[PH_P_POS] / 127.0f));
    W(PH_C_SCAN_RATIO, bits(((float)raw[PH_P_SCAN] - 64.0f) / 32.0f));
    W(PH_C_REVERSE, bits((float)raw[PH_P_REVR] / 127.0f));
    W(PH_C_WIDTH, bits((float)raw[PH_P_WDTH] / 127.0f));
    W(PH_C_DIFF, bits((float)raw[PH_P_DIFF] / 127.0f));
    /* RT60 across the diffusion network's mean136.5sample delay. */
    float decay = (float)raw[PH_P_DEC] / 127.0f;
    float exponent = 0.019643f / (0.15f + 7.85f * decay * decay);
    W(PH_C_DECAY_FEEDBACK,
      bits(1.0f - exponent + 0.5f * exponent * exponent - exponent * exponent * exponent / 6.0f));
    /* Stable one-pole coefficients, zero HP and full LP are bypass endpoints. */
    float hp = (float)raw[PH_P_HPF] / 127.0f, lp = (float)raw[PH_P_LPF] / 127.0f;
    W(PH_C_HPF_COEFF, bits(hp * hp * 0.55f));
    W(PH_C_LPF_COEFF, bits(lp * lp * 0.99f + 0.01f));
    /* The tape head. TIME is filled in per block from the tempo (tape_frames). */
    W(PH_C_TAPE_FEEDBACK, bits((float)raw[PH_P_FDBK] * (1.1f / 127.0f)));
    W(PH_C_TAPE_PACK, (raw[PH_P_MODE] == 1 ? PH_TAPE_FLIP : 0) |
                          (raw[PH_P_MODE] == 2 ? PH_TAPE_HOLD : 0) | field(raw[PH_P_SKEW], 1) << 2 |
                          field(raw[PH_P_WOW], 0) << 12 | field(raw[PH_P_BAL], 1) << 22);
    W(PH_C_DRIVE, bits((float)raw[PH_P_DRIV] / 127.0f));
    W(PH_C_OWNER_ROW, row_index);
    W(PH_C_OWNER_GENERATION, generation);
#undef W
    u32 sr = dkrt_irq_mask();
    published = next;
    dkrt_irq_restore(sr);
}

/* Runtime calls only for a loader-validated exclusive capability owner.
 * physical_row is the absolute core/fx registry row, native_page the FX code. */
void phrase_bind(u32 own, u32 physical_row, u32 native_page, const char *module, const char *name)
{
    if (owner == own && own && page_code == native_page)
        return;
    owner = 0;
    page_code = 0;
    raw[PH_P_FADE] = raw[PH_P_REC] = 0;
    ++generation;
    ++selection;
    command = action = 0;
    route_fault_seen = route_rearm_pending = route_rearm_frame = 0;
    for (u32 i = 0; i < PH_STATUS_WORDS; ++i)
        status[i] = 0;
    reply_frame = 0;
    fragments = 0;
    if (own) {
        u32 j = 0;
        for (; j < 23 && name[j]; ++j)
            title[j] = name[j];
        title[j] = 0;
        const char prefix[] = "phrase/";
        u32 n = 0;
        for (; n < 7; ++n)
            key[n] = prefix[n];
        for (u32 i = 0; i < 31 && module[i]; ++i)
            key[n++] = module[i];
        key[n] = 0;
        load();
        row_index = physical_row;
        page_code = native_page;
        owner = own;
    }
    publish();
}
u32 dkrt_phrase_active(void)
{
    return owner ? page_code : 0;
}
const char *dkrt_phrase_title(void)
{
    return title;
}
u32 dkrt_phrase_get(u32 i)
{
    return i < PH_PARAM_COUNT ? raw[i] : 0;
}
u32 dkrt_phrase_snapshot(u32 *out, u32 words)
{
    if (!out || words < PH_STATUS_WORDS)
        return 0;
    u32 sr = dkrt_irq_mask();
    for (u32 i = 0; i < PH_STATUS_WORDS; ++i)
        out[i] = status[i];
    u32 fresh = reply_frame && (u32)(frame - reply_frame) < 750 &&
                status[PH_S_OWNER_GENERATION / 4] == generation;
    dkrt_irq_restore(sr);
    return fresh;
}
int32_t dkrt_phrase_action(u32 a)
{
    if (!owner || a < PH_ACTION_ARM || a > PH_ACTION_CLEAR)
        return -1;
    u32 st = status[PH_S_STATE / 4];
    if (st == PH_HANDOFF && a == PH_ACTION_ARM)
        return -2;
    action = a;
    ++command;
    raw[PH_P_REC] = a == PH_ACTION_ARM;
    /* CLEAR drops the held phrase; MIX stays, because the effect runs without one. After a routing fault it still
     * takes MIX down and cancels a retry, as every safety path does. */
    if (a == PH_ACTION_CLEAR && (route_fault_seen || route_rearm_pending)) {
        raw[PH_P_FADE] = 0;
        route_rearm_pending = 0;
    }
    publish();
    return 0;
}
int32_t dkrt_phrase_set(u32 i, u32 value)
{
    if (!owner || i >= PH_PARAM_COUNT)
        return -1;
    if (i == PH_P_REC)
        return dkrt_phrase_action(value ? PH_ACTION_ARM : PH_ACTION_CANCEL);
    u32 st = status[PH_S_STATE / 4];
    u32 input = i == PH_P_MASK || (i >= PH_P_GAIN0 && i <= PH_P_EXT_GAIN);
    if (input && (raw[PH_P_REC] || st == PH_ARMED || st == PH_RECORDING))
        return -2;
    if (value > limit(i))
        value = limit(i);
    if (i == PH_P_LEN && !value)
        value = 1;
    /* MIX needs a live engine, not a held phrase: the effect is on whenever it is loaded. */
    if (i == PH_P_FADE && value && (!reply_frame || frame - reply_frame >= 750))
        value = 0;
    if (raw[i] == value)
        return 0;
    if (i == PH_P_FADE) {
        if (!value)
            route_rearm_pending = 0;
        else if (!raw[i] && route_fault_seen && status[PH_S_ROUTE_FAULT / 4]) {
            /* The router keeps its fault flag until it sees this explicit
             * zero-to-raised gesture. Do not erase that gesture using the
             * previous reply while its new control frame is in flight. */
            route_rearm_pending = 1;
            route_rearm_frame = frame;
        }
    }
    raw[i] = value;
    if (input)
        ++selection;
    publish();
    if (i != PH_P_FADE)
        save();
    return 0;
}
void phrase_control_tick(void)
{
    if (!owner)
        return;
    if (raw[PH_P_REC] && status[PH_S_COMMAND_ACK / 4] == command && !status[PH_S_REC_ACTIVE / 4]) {
        raw[PH_P_REC] = 0;
        publish();
    }
    u32 fault = status[PH_S_ROUTE_FAULT / 4], new_fault = fault && !route_fault_seen;
    route_fault_seen = fault != 0;
    if (!fault || new_fault)
        route_rearm_pending = 0;
    u32 failed_rearm =
        fault && route_rearm_pending && frame - route_rearm_frame >= ROUTE_REARM_BLOCKS;
    if (raw[PH_P_FADE] &&
        (!reply_frame || frame - reply_frame >= 750 || new_fault || failed_rearm ||
         status[PH_S_ERROR / 4] == PH_ERROR_STORAGE || status[PH_S_ERROR / 4] == PH_ERROR_LINK)) {
        raw[PH_P_FADE] = 0;
        route_rearm_pending = 0;
        publish();
    }
}

static u32 queued(u32 queue, u32 entry)
{
    volatile u16 *p = (volatile u16 *)(queue + 4 * entry + 2);
    return p[0] | ((u32)p[2] << 16);
}
/* Same reply cells as the loop UI, explicitly time multiplexed by magic.
 * Page0 latches a32-word status snapshot; pages1/2 finish that same snapshot. */
static void reply(const volatile u16 *p)
{
    if (!p)
        return;
    u32 w[14];
    for (u32 j = 0; j < 14; ++j) {
        u32 row = j < 8 ? 17 + j / 2 : 29 + (j - 8) / 2;
        u32 off = (0x1c + 0x54 * row + 0x44 + (j & 1) * 4) / 2;
        w[j] = p[off] | ((u32)p[off + 1] << 16);
    }
    if (w[0] != PH_REPLY_MAGIC)
        return;
    u32 part = w[1] & 3, seq = w[1] >> 2;
    if (part > 2)
        return;
    if (part == 0) {
        assembled_seq = seq;
        fragments = 0;
    }
    if (seq != assembled_seq)
        return;
    for (u32 i = 0; i < 12 && part * 12 + i < PH_STATUS_WORDS; ++i)
        assembling[part * 12 + i] = w[i + 2];
    fragments |= 1u << part;
    if (fragments != 7 || assembling[0] != PH_STATUS_MAGIC || assembling[1] != PH_VERSION ||
        assembling[PH_S_HEARTBEAT / 4] != assembling[PH_S_SEQ_END / 4] ||
        assembling[PH_S_OWNER_GENERATION / 4] != generation)
        return;
    for (u32 i = 0; i < PH_STATUS_WORDS; ++i)
        status[i] = assembling[i];
    reply_frame = frame;
    fragments = 0;
}
/* Musical clock uses stock21600000 ticks/quarter. The position provider stages
 * the exact master scale and phase origin with each nominal boundary.
 * A stopped NOW grid advances at the stock selected BPM, without local tempo.
 * Whole and remainder division avoids cumulative fractional-clock drift. */
static void timing(u32 *c, const u32 *p, u32 period, u32 origin)
{
    u32 flags = p[5], tempo = flags >> 16, phase = p[9], running = flags & 2;
    u32 valid = (flags & 1) && !(flags & 4), stopped = !running;
    u32 period_valid = period >= 2700000 && period <= 43200000;
    if (tempo < 3600 || tempo > 36000)
        tempo = 0;
    if (!phase_seen) {
        clock_valid = 0;
        start_wait_blocks = 0;
    }
    /* Stock transport becomes RUNNING before its timed START boundary is
     * committed. During that narrow handoff the provider has no period and
     * still publishes the preceding generation/pattern. Keep the previously
     * valid stopped grid for at most 64 blocks; do not publish a START reason
     * or consume the stopped-to-running transition before the valid commit.
     * Any other invalid clock is immediately invalid. A timeout cannot open
     * another grace window until an actual stop or valid commit is observed. */
    if (!running || (valid && period_valid))
        start_wait_blocks = 0;
    if (phase_seen && stopped_seen && clock_valid && (flags & 7) == 2 && !period &&
        p[4] == prev_epoch && p[6] == prev_pattern && tempo && master_ticks >= 2700000 &&
        master_ticks <= 43200000 && start_wait_blocks < START_PRECOMMIT_BLOCKS) {
        ++start_wait_blocks;
        running = 0;
        stopped = 1;
        period = master_ticks;
        period_valid = 1;
    }
    if (!period_valid)
        period = 5400000;
    if (!phase_seen || p[4] != prev_epoch || stopped != stopped_seen || p[6] != prev_pattern ||
        (stopped && master_ticks != period)) {
        /* Preserve the reason for the whole epoch so a queued control frame
         * cannot miss the one transition on which a stopped numbered cue
         * becomes runnable. Pattern, scale or invalid-clock changes are not
         * an ordinary START, even when they also enter running transport. */
        timeline_start = phase_seen && stopped_seen && clock_valid && running &&
                                 p[4] - prev_epoch == 1 && p[6] == prev_pattern &&
                                 master_ticks == period && valid && period_valid && tempo
                             ? PH_TIMING_START
                             : 0;
        ++timeline;
        phase_seen = 1;
        prev_phase = phase;
        beat_q16 = beat_rem = 0;
        master_q16 = 0;
        boundary_phase = running ? origin : phase;
        prev_boundary = p[3];
        prev_epoch = p[4];
        prev_pattern = p[6];
        local_phase = phase;
        stopped_seen = stopped;
    }
    master_ticks = period;
    if (stopped) {
        local_phase += tempo * 2;
        phase = local_phase;
        valid = tempo != 0;
    }
    valid = valid && period_valid;
    u32 delta = phase - prev_phase;
    prev_phase = phase;
    if (delta > 21600000) {
        valid = 0;
        ++timeline;
        timeline_start = 0;
        delta = 0;
    }
    /* delta<=21600000. quotient and remainder before multiplying by65536. */
    u32 q = delta / 21600000, r = delta % 21600000;
    uint64_t fine = (uint64_t)r * 65536 + beat_rem;
    u32 fine_q = (u32)(fine / 21600000);
    beat_q16 += q * 65536 + fine_q;
    /* The true remainder is below21600000. Unsigned low-word subtraction
     * recovers it exactly, avoiding a second64-bit division helper on CF. */
    beat_rem = (u32)fine - fine_q * 21600000u;
    if (running && p[3] != prev_boundary) {
        if (p[3] - prev_boundary != 1)
            valid = 0;
        master_q16 = (master_q16 + 65536) & 0xffff0000u;
        boundary_phase = origin;
        prev_boundary = p[3];
    }
    u32 frac = (u32)(((uint64_t)(phase - boundary_phase) * 65536) / master_ticks);
    if (running && frac > 65535)
        frac = 65535;
    c[PH_C_MASTER_PHASE / 4] = master_q16 + frac;
    c[PH_C_BEAT_PHASE / 4] = beat_q16;
    c[PH_C_TIMELINE_EPOCH / 4] = timeline;
    c[PH_C_TIMING_FLAGS / 4] = (valid ? PH_TIMING_VALID : 0) | (running ? PH_TIMING_RUNNING : 0) |
                               (tempo ? PH_TIMING_TEMPO_VALID : 0) |
                               (stopped ? PH_TIMING_STOPPED : 0) | timeline_start;
    clock_valid = valid;
    c[PH_C_STEP / 4] = p[7];
    c[PH_C_LENGTH / 4] = p[8];
    c[PH_C_STEP_SEQUENCE / 4] = p[3];
    c[PH_C_TEMPO_120 / 4] = tempo;
    c[PH_C_TAPE_TIME / 4] = tape_frames(tempo);
}
void phrase_write_frame(u32 queue, u32 entry, const volatile u16 *rec)
{
    u32 c[PH_CONTROL_WORDS], p[16], clock[2];
    ++frame;
    reply(rec);
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i)
        c[i] = control[published][i];
    /* PHRASE suffix starts at0x8b4; position suffix at0xa7c. */
    for (u32 i = 0; i < 16; ++i)
        p[i] = queued(queue, 0xa7c / 2 + 2 * i);
    pp_phrase_timing(clock);
    timing(c, p, clock[0], clock[1]);
    c[PH_C_SEQ / 4] = c[PH_C_SEQ_END / 4] = frame;
    u32 check = PH_CHECK_SALT;
    for (u32 i = 0; i < PH_CONTROL_WORDS - 1; ++i)
        check ^= c[i];
    c[PH_C_CHECK / 4] = check;
    volatile u16 *out = (volatile u16 *)(queue + 4 * entry + 2);
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i) {
        out[0] = (u16)c[i];
        out[2] = (u16)(c[i] >> 16);
        out += 4;
    }
}
