/* The control provider of the phrase effect (PRISM): its settings, the control words it publishes once a block and the
 * status it gets back (layout: sdk/dsp/contract/dk_phrase.inc, version 3). No audio is read on the ColdFire. */
#include <dk/phrase.h>
typedef uint32_t u32;
typedef uint16_t u16;
extern u32 dkrt_irq_mask(void);
extern void dkrt_irq_restore(u32);
extern int dkrt_settings_get(const char *, void *, u32);
extern int dkrt_settings_put(const char *, const void *, u32);

static u32 raw[PH_PARAM_COUNT], control[2][PH_CONTROL_WORDS];
static volatile u32 published;
static u32 owner, page_code, row_index, generation;
static u32 frame, reply_frame, assembled_seq, fragments, status[PH_STATUS_WORDS];
/* The block in which the engine's heartbeat last moved. The DSP side returns its latest snapshot again and again; only a
 * heartbeat that moves shows that the engine still runs. */
static u32 beat_frame, beat, beating;
static u32 assembling[PH_STATUS_WORDS];
static char key[48], sends_key[48];
static char title[24];
/* The format of the saved settings: the version of the control words. Settings of another format are not read. A record
 * of format 3 has every value up to PH_P_TRKS, or (written before the tracks were a value) every value before it. */
enum { SAVE_FORMAT = 3 };

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
    if (p == PH_P_ZONE)
        return PH_ZONES - 1;
    if (p == PH_P_RATE)
        return PH_RATE_MAX;
    if (p == PH_P_NOTE)
        return PH_TIME_CHOICES - 1;
    if (p == PH_P_SYNC || p == PH_P_FLIP || p == PH_P_HOLD || p == PH_P_INPT)
        return 1;
    if (p == PH_P_TRKS)
        return PH_TRACKS_ALL;
    return 127;
}
static void defaults(void)
{
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i)
        raw[i] = 0;
    /* Zone 5 (150 ms to 1 s) in its middle, a dotted eighth with SYNC, half the way to unity, flat, no halo, both sides
     * alike, the wet signal most of the way up. */
    raw[PH_P_ZONE] = 4;
    raw[PH_P_RATE] = PH_RATE_MAX / 2;
    raw[PH_P_NOTE] = 8;
    raw[PH_P_RPTS] = PH_RPTS_UNITY / 2;
    raw[PH_P_COLR] = 64;
    raw[PH_P_SKEW] = 64;
    raw[PH_P_MIX] = 100;
}
/* What survives a restart: everything but HOLD, which is a gesture, INPT, which follows the loop (after a restart there is
 * none), and XFAD, which is the loop effect's. */
static u32 saved_value(u32 i)
{
    return i == PH_P_HOLD || i == PH_P_INPT || i == PH_P_XFAD ? 0 : raw[i];
}
static void save(void)
{
    /* Versioned byte encoding, independent of host endianness. */
    uint8_t value[4 + PH_PARAM_COUNT * 4];
    value[0] = 'P';
    value[1] = 'H';
    value[2] = SAVE_FORMAT;
    value[3] = PH_PARAM_COUNT;
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i) {
        u32 v = saved_value(i);
        for (u32 j = 0; j < 4; ++j)
            value[4 + 4 * i + j] = (uint8_t)(v >> (24 - 8 * j));
    }
    dkrt_settings_put(key, value, sizeof(value));
}
/* The tracks the effect heard before they were one of its values: the send amounts of the fx service's record of the
 * effect (fx/send/<module id>, one byte a track, finding 64) when it set them from a cell of the send page. A track with
 * an amount above 0 is heard; without the record none is. */
static u32 carried_tracks(void)
{
    uint8_t amount[PH_TRACKS];
    u32 tracks = 0;
    if (dkrt_settings_get(sends_key, amount, sizeof(amount)) != (int)sizeof(amount))
        return 0;
    for (u32 t = 0; t < PH_TRACKS; ++t)
        if (amount[t])
            tracks |= 1u << t;
    return tracks;
}
static void load(void)
{
    uint8_t value[4 + PH_PARAM_COUNT * 4];
    defaults();
    int n = dkrt_settings_get(key, value, sizeof(value));
    u32 count = 0;
    if (n >= 4 && value[0] == 'P' && value[1] == 'H' && value[2] == SAVE_FORMAT &&
        (value[3] == PH_PARAM_COUNT || value[3] == PH_P_TRKS) && n == 4 + 4 * (int)value[3])
        count = value[3];
    for (u32 i = 0; i < count; ++i) {
        u32 v = 0;
        for (u32 j = 0; j < 4; ++j)
            v = (v << 8) | value[4 + 4 * i + j];
        raw[i] = v > limit(i) ? limit(i) : v;
    }
    raw[PH_P_HOLD] = raw[PH_P_INPT] = raw[PH_P_XFAD] = 0;
    /* No tracks of its own (a record from before, another format or none): the ones the send amounts gave. They are saved
     * with the next change of any value. */
    if (count < PH_PARAM_COUNT)
        raw[PH_P_TRKS] = carried_tracks();
}
/* The time in frames of the NOTE choice at TEMPO (BPM * 120): ticks * 3 600 000 / tempo, in integers (this runs in the
 * block interrupt; a tick is 1/96 of a quarter note). Without a tempo the last value stays. The engine bounds it to its
 * tape. */
static u32 sync_time = 24000;
static u32 sync_frames(u32 tempo)
{
    static const u16 ticks[PH_TIME_CHOICES] = {12, 16, 18,  24,  32,  36,  48,  64,
                                               72, 96, 128, 144, 192, 288, 384, 768};
    u32 choice = raw[PH_P_NOTE];
    if (tempo && choice < PH_TIME_CHOICES)
        sync_time = (u32)ticks[choice] * 3600000u / tempo;
    return sync_time;
}
/* RPTS as the engine takes it: 0 .. PH_RPTS_UNITY is 0 .. 0.75 (unity exactly), the rest of the way up to 1. */
static float repeats(u32 v)
{
    if (v <= PH_RPTS_UNITY)
        return (float)v * (0.75f / PH_RPTS_UNITY);
    return 0.75f + (float)(v - PH_RPTS_UNITY) * (0.25f / (127 - PH_RPTS_UNITY));
}
/* A control with its centre at 64: -1 .. 1, both ends exact. */
static float centred(u32 v)
{
    return v >= 64 ? (float)(v - 64) / 63.0f : -(float)(64 - v) / 64.0f;
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
    W(PH_C_ZONE, raw[PH_P_ZONE] + 1);
    W(PH_C_RATE, bits((float)raw[PH_P_RATE] / (float)PH_RATE_MAX));
    W(PH_C_FLAGS, (raw[PH_P_SYNC] ? PH_F_SYNC : 0) | (raw[PH_P_FLIP] ? PH_F_FLIP : 0) |
                      (raw[PH_P_HOLD] ? PH_F_HOLD : 0));
    /* SYNC_TIME is filled in per block from the tempo (sync_frames). */
    W(PH_C_RPTS, bits(repeats(raw[PH_P_RPTS])));
    W(PH_C_COLR, bits(centred(raw[PH_P_COLR])));
    W(PH_C_HALO, bits((float)raw[PH_P_HALO] / 127.0f));
    W(PH_C_SKEW, bits(centred(raw[PH_P_SKEW])));
    W(PH_C_MIX, bits((float)raw[PH_P_MIX] / 127.0f));
    W(PH_C_INPUT, raw[PH_P_INPT]);
    /* PH_C_XFADE and PH_C_ARM_SEQ stay 0: the crossfader and ARM are the loop effect's (core/loop-control). */
    W(PH_C_OWNER_ROW, row_index);
    W(PH_C_OWNER_GENERATION, generation);
#undef W
    u32 sr = dkrt_irq_mask();
    published = next;
    dkrt_irq_restore(sr);
}

/* The fx service calls this for the one effect that imports the control mailbox, when its page is registered, and
 * with owner 0 when the effect leaves. physical_row is the row of the effect table, native_page the FX page code. */
void phrase_bind(u32 own, u32 physical_row, u32 native_page, const char *module, const char *name)
{
    if (owner == own && own && page_code == native_page)
        return;
    owner = 0;
    page_code = 0;
    raw[PH_P_HOLD] = 0;
    ++generation;
    for (u32 i = 0; i < PH_STATUS_WORDS; ++i)
        status[i] = 0;
    reply_frame = 0;
    fragments = 0;
    beating = 0;
    if (own) {
        u32 j = 0;
        for (; j < 23 && name[j]; ++j)
            title[j] = name[j];
        title[j] = 0;
        static const char prefix[] = "phrase/", sends[] = "fx/send/";
        u32 n = 0, s = 0;
        for (; n < 7; ++n)
            key[n] = prefix[n];
        for (; s < 8; ++s)
            sends_key[s] = sends[s];
        for (u32 i = 0; i < 31 && module[i]; ++i) {
            key[n++] = module[i];
            sends_key[s++] = module[i];
        }
        key[n] = 0;
        sends_key[s] = 0;
        load();
        row_index = physical_row;
        page_code = native_page;
        owner = own;
    }
    publish();
}
/* The fx service calls this once a UI pass for the effect's row (an `[[info]]` entry, like phrase_bind): the send amount of
 * each of the 16 tracks into AMOUNTS, PH_SEND_FULL for a track the effect hears (PH_P_TRKS) and 0 for the others. It
 * returns 1, or 0 without an owner, and then writes nothing. The send route mixes the tracks by these amounts. */
u32 phrase_sends(uint8_t *amounts)
{
    if (!owner || !amounts)
        return 0;
    for (u32 t = 0; t < PH_TRACKS; ++t)
        amounts[t] = (raw[PH_P_TRKS] >> t) & 1 ? PH_SEND_FULL : 0;
    return 1;
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
    u32 fresh = reply_frame && (u32)(frame - reply_frame) < 750 && beating &&
                (u32)(frame - beat_frame) < 750 && status[PH_S_OWNER_GENERATION / 4] == generation;
    dkrt_irq_restore(sr);
    return fresh;
}
int32_t dkrt_phrase_set(u32 i, u32 value)
{
    if (!owner || i >= PH_PARAM_COUNT)
        return -1;
    /* XFAD is the loop effect's crossfader, held by core/loop-control. */
    if (i == PH_P_XFAD)
        return -2;
    if (value > limit(i))
        value = limit(i);
    if (raw[i] == value)
        return 0;
    raw[i] = value;
    publish();
    if (i != PH_P_HOLD && i != PH_P_INPT)
        save();
    return 0;
}

static u32 queued(u32 queue, u32 entry)
{
    volatile u16 *p = (volatile u16 *)(queue + 4 * entry + 2);
    return p[0] | ((u32)p[2] << 16);
}
/* Same reply cells as the loop UI, explicitly time multiplexed by magic.
 * Page 0 latches a 32-word status snapshot; pages 1 and 2 finish that same snapshot. */
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
    if (!beating || status[PH_S_HEARTBEAT / 4] != beat) {
        beat = status[PH_S_HEARTBEAT / 4];
        beat_frame = frame;
        beating = 1;
    }
    fragments = 0;
}
/* The tempo of the position suffix (core/pattern-position): word 5 holds BPM * 120 in its upper half. */
static u32 tempo_of(u32 flags)
{
    u32 tempo = flags >> 16;
    return tempo < 3600 || tempo > 36000 ? 0 : tempo;
}
/* The control words of this block: the published ones, the tempo and the SYNC time from the position suffix's word 5, the
 * block sequence at both ends and the checksum the DSP side checks. */
static void frame_words(u32 *c, u32 position_flags)
{
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i)
        c[i] = control[published][i];
    u32 tempo = tempo_of(position_flags);
    c[PH_C_TEMPO_120 / 4] = tempo;
    c[PH_C_SYNC_TIME / 4] = sync_frames(tempo);
    c[PH_C_SEQ / 4] = c[PH_C_SEQ_END / 4] = frame;
    u32 check = PH_CHECK_SALT;
    for (u32 i = 0; i < PH_CONTROL_WORDS - 1; ++i)
        check ^= c[i];
    c[PH_C_CHECK / 4] = check;
}
/* core/dsp-link calls this once a block, in the block interrupt, with the frame it is about to send. The phrase suffix
 * starts at 0x8b4 (ENTRY), the position suffix at 0xa7c. */
void phrase_write_frame(u32 queue, u32 entry, const volatile u16 *rec)
{
    u32 c[PH_CONTROL_WORDS];
    ++frame;
    reply(rec);
    frame_words(c, queued(queue, 0xa7c / 2 + 2 * 5));
    volatile u16 *out = (volatile u16 *)(queue + 4 * entry + 2);
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i) {
        out[0] = (u16)c[i];
        out[2] = (u16)(c[i] >> 16);
        out += 4;
    }
}
