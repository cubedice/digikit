/* Host harness of core/phrase-control: the provider's own source with stand-ins for the runtime it calls. */
#include <assert.h>
#include <string.h>
#include "../src/control.c"
static unsigned writes;
static uint8_t saved[64];
static u32 saved_bytes;
u32 dkrt_irq_mask(void)
{
    return 0;
}
void dkrt_irq_restore(u32 v)
{
    (void)v;
}
int dkrt_settings_get(const char *k, void *p, u32 n)
{
    assert(!strcmp(k, "phrase/fixture/phrase"));
    if (!saved_bytes)
        return -1;
    if (saved_bytes != n)
        return (int)saved_bytes; /* a value of another size: the caller refuses it */
    memcpy(p, saved, n);
    return (int)n;
}
int dkrt_settings_put(const char *k, const void *p, u32 n)
{
    assert(!strcmp(k, "phrase/fixture/phrase"));
    assert(n <= sizeof(saved));
    memcpy(saved, p, n);
    saved_bytes = n;
    ++writes;
    return 0;
}
static float f(u32 word)
{
    float v;
    memcpy(&v, &word, 4);
    return v;
}
static u32 word(u32 name)
{
    return control[published][name / 4];
}
/* One reply fragment in the cells the loop telemetry carrier uses. */
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
/* One block's control words as the provider sends them, for a position suffix whose word 5 holds TEMPO120 in its upper
 * half (phrase_write_frame itself only moves these words into the frame queue, whose 32-bit address a host has not). */
static u32 block[PH_CONTROL_WORDS];
static u32 sent(u32 tempo120, u32 name)
{
    ++frame;
    frame_words(block, tempo120 << 16);
    return block[name / 4];
}

static void controls(void)
{
    /* the defaults: zone 5 in its middle, repeats half way to unity, flat, no halo, both sides alike */
    assert(word(PH_C_MAGIC) == PH_CONTROL_MAGIC && word(PH_C_VERSION) == 3 &&
           word(PH_C_SIZE) == 256);
    assert(word(PH_C_ENABLED) == 1);
    assert(word(PH_C_ZONE) == 5);
    assert(f(word(PH_C_RATE)) == 0.5f);
    assert(word(PH_C_FLAGS) == 0);
    assert(f(word(PH_C_RPTS)) == 0.375f);
    assert(f(word(PH_C_COLR)) == 0.0f && f(word(PH_C_SKEW)) == 0.0f && f(word(PH_C_HALO)) == 0.0f);
    assert(f(word(PH_C_MIX)) == 100.0f / 127.0f);
    /* each raw value becomes its control word; the ends are exact */
    assert(dkrt_phrase_set(PH_P_ZONE, 0) == 0 && word(PH_C_ZONE) == 1);
    assert(dkrt_phrase_set(PH_P_ZONE, 99) == 0 && word(PH_C_ZONE) == 8); /* clamped */
    assert(dkrt_phrase_set(PH_P_RATE, 0) == 0 && f(word(PH_C_RATE)) == 0.0f);
    assert(dkrt_phrase_set(PH_P_RATE, PH_RATE_MAX) == 0 && f(word(PH_C_RATE)) == 1.0f);
    assert(dkrt_phrase_set(PH_P_RATE, PH_RATE_COARSE) == 0 && f(word(PH_C_RATE)) == 8.0f / 1016.0f);
    assert(dkrt_phrase_set(PH_P_RPTS, 0) == 0 && f(word(PH_C_RPTS)) == 0.0f);
    assert(dkrt_phrase_set(PH_P_RPTS, PH_RPTS_UNITY) == 0 &&
           f(word(PH_C_RPTS)) == 0.75f); /* unity, exactly */
    assert(dkrt_phrase_set(PH_P_RPTS, 127) == 0);
    assert(f(word(PH_C_RPTS)) > 0.9999f && f(word(PH_C_RPTS)) < 1.0001f);
    assert(dkrt_phrase_set(PH_P_COLR, 0) == 0 && f(word(PH_C_COLR)) == -1.0f);
    assert(dkrt_phrase_set(PH_P_COLR, 127) == 0 && f(word(PH_C_COLR)) == 1.0f);
    assert(dkrt_phrase_set(PH_P_COLR, 64) == 0 && f(word(PH_C_COLR)) == 0.0f);
    assert(dkrt_phrase_set(PH_P_SKEW, 0) == 0 && f(word(PH_C_SKEW)) == -1.0f);
    assert(dkrt_phrase_set(PH_P_SKEW, 127) == 0 && f(word(PH_C_SKEW)) == 1.0f);
    assert(dkrt_phrase_set(PH_P_HALO, 127) == 0 && f(word(PH_C_HALO)) == 1.0f);
    assert(dkrt_phrase_set(PH_P_MIX, 0) == 0 && f(word(PH_C_MIX)) == 0.0f);
    assert(dkrt_phrase_set(PH_P_MIX, 127) == 0 && f(word(PH_C_MIX)) == 1.0f);
    /* the three switches are the three flag bits */
    assert(dkrt_phrase_set(PH_P_SYNC, 1) == 0 && word(PH_C_FLAGS) == PH_F_SYNC);
    assert(dkrt_phrase_set(PH_P_FLIP, 1) == 0 && word(PH_C_FLAGS) == (PH_F_SYNC | PH_F_FLIP));
    assert(dkrt_phrase_set(PH_P_HOLD, 7) == 0 &&
           word(PH_C_FLAGS) == (PH_F_SYNC | PH_F_FLIP | PH_F_HOLD));
    /* the words kept for the loop are published as 0, and nothing sets their values */
    assert(dkrt_phrase_set(PH_P_INPT, 1) == -2 && dkrt_phrase_set(PH_P_XFAD, 64) == -2);
    assert(word(PH_C_INPUT) == 0 && word(PH_C_XFADE) == 0 && word(PH_C_ARM_SEQ) == 0);
    assert(dkrt_phrase_set(PH_PARAM_COUNT, 1) == -1);
}

static void sync_times(void)
{
    /* A note length is ticks * 3 600 000 / (BPM * 120) frames, a tick being 1/96 of a quarter note: at 120 BPM a quarter
     * note is 24000 frames. */
    static const u32 at120[PH_TIME_CHOICES] = {3000,  4000,  4500,  6000,  8000,  9000,
                                               12000, 16000, 18000, 24000, 32000, 36000,
                                               48000, 72000, 96000, 192000};
    for (u32 note = 0; note < PH_TIME_CHOICES; ++note) {
        assert(dkrt_phrase_set(PH_P_NOTE, note) == 0);
        assert(sent(14400, PH_C_SYNC_TIME) == at120[note]);
    }
    assert(dkrt_phrase_set(PH_P_NOTE, 9) == 0);
    assert(sent(7200, PH_C_SYNC_TIME) == 48000);  /* 60 BPM */
    assert(sent(36000, PH_C_SYNC_TIME) == 9600);  /* 300 BPM */
    assert(sent(10800, PH_C_SYNC_TIME) == 32000); /* 90 BPM */
    assert(sent(0, PH_C_SYNC_TIME) == 32000);     /* no tempo: the last time stays */
    assert(sent(100, PH_C_SYNC_TIME) == 32000);   /* a tempo out of range is none */
    assert(sent(14400, PH_C_TEMPO_120) == 14400);
    /* the frame as the DSP checks it: the sequence at both ends, the checksum over the first 63 words */
    u32 check = PH_CHECK_SALT, seq = sent(14400, PH_C_SEQ);
    for (u32 i = 0; i < 63; ++i)
        check ^= block[i];
    assert(block[63] == check);
    assert(block[PH_C_SEQ_END / 4] == seq);
    assert(sent(14400, PH_C_SEQ) == seq + 1);
}

static void status_assembly(void)
{
    u32 s[32] = {0}, out[32] = {0};
    u16 wire[0xabc / 2] = {0};
    s[0] = PH_STATUS_MAGIC;
    s[1] = PH_VERSION;
    s[PH_S_HEARTBEAT / 4] = 77;
    s[PH_S_SEQ_END / 4] = 77;
    s[PH_S_OWNER_GENERATION / 4] = generation;
    s[PH_S_TIME_L / 4] = 18000;
    s[PH_S_TIME_R / 4] = 27000;
    s[PH_S_STATE / 4] = PH_READY;
    frame = 100;
    packet(wire, 1, 5, s);
    reply(wire);
    packet(wire, 2, 5, s);
    reply(wire);
    assert(!dkrt_phrase_snapshot(out, 32)); /* a snapshot without its first part is invisible */
    packet(wire, 0, 6, s);
    reply(wire);
    packet(wire, 1, 6, s);
    reply(wire);
    packet(wire, 2, 7, s);
    reply(wire);
    assert(
        !dkrt_phrase_snapshot(out, 32)); /* a part of another snapshot does not finish this one */
    packet(wire, 2, 6, s);
    reply(wire);
    assert(dkrt_phrase_snapshot(out, 32));
    assert(out[PH_S_TIME_L / 4] == 18000 && out[PH_S_TIME_R / 4] == 27000);
    assert(!dkrt_phrase_snapshot(out, 31) && !dkrt_phrase_snapshot(0, 32));
    /* a status of another version, of another owner or with a torn heartbeat is not taken */
    for (u32 bad = 0; bad < 3; ++bad) {
        u32 t[32];
        memcpy(t, s, sizeof(t));
        t[PH_S_TIME_L / 4] = 1;
        if (bad == 0)
            t[1] = 2;
        if (bad == 1)
            t[PH_S_OWNER_GENERATION / 4] = generation + 1;
        if (bad == 2)
            t[PH_S_SEQ_END / 4] = 78;
        for (u32 i = 0; i < 3; ++i) {
            packet(wire, i, 8 + bad, t);
            reply(wire);
        }
        assert(dkrt_phrase_snapshot(out, 32) && out[PH_S_TIME_L / 4] == 18000);
    }
    /* without a reply for 750 blocks (half a second) the status is no longer fresh; the values stay */
    frame += 750;
    assert(!dkrt_phrase_snapshot(out, 32) && out[PH_S_TIME_L / 4] == 18000);
}

int main(void)
{
    /* no owner: nothing is published as enabled and nothing can be set */
    assert(dkrt_phrase_active() == 0 && dkrt_phrase_set(PH_P_MIX, 1) == -1);
    phrase_bind(1, 1, 0x20, "fixture/phrase", "TEST");
    assert(dkrt_phrase_active() == 0x20 && !strcmp(dkrt_phrase_title(), "TEST"));
    controls();
    sync_times();
    status_assembly();
    /* The settings are global and survive the owner: every value but HOLD. */
    assert(writes && saved_bytes == 4 + PH_PARAM_COUNT * 4 && saved[2] == 3);
    u32 before[PH_PARAM_COUNT];
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i)
        before[i] = dkrt_phrase_get(i);
    assert(before[PH_P_HOLD] == 1 && before[PH_P_FLIP] == 1 && before[PH_P_SYNC] == 1);
    u32 seen = generation;
    phrase_bind(0, 0, 0, 0, 0);
    assert(dkrt_phrase_active() == 0 && word(PH_C_ENABLED) == 0);
    phrase_bind(2, 2, 0x21, "fixture/phrase", "TEST");
    assert(dkrt_phrase_active() == 0x21 && generation == seen + 2);
    for (u32 i = 0; i < PH_PARAM_COUNT; ++i)
        assert(dkrt_phrase_get(i) == (i == PH_P_HOLD ? 0 : before[i]));
    assert(word(PH_C_FLAGS) == (PH_F_SYNC | PH_F_FLIP) && word(PH_C_OWNER_ROW) == 2);
    /* a HOLD press alone writes no settings */
    unsigned n = writes;
    assert(dkrt_phrase_set(PH_P_HOLD, 1) == 0 && writes == n);
    assert(dkrt_phrase_set(PH_P_MIX, 64) == 0 && writes == n + 1);
    assert(dkrt_phrase_set(PH_P_MIX, 64) == 0 && writes == n + 1); /* no change, no write */
    /* Settings of the engine before this one (format 2, 46 values) or of another size are not read: the defaults. */
    phrase_bind(0, 0, 0, 0, 0);
    saved[2] = 2;
    phrase_bind(3, 1, 0x20, "fixture/phrase", "TEST");
    assert(dkrt_phrase_get(PH_P_ZONE) == 4 && dkrt_phrase_get(PH_P_MIX) == 100 &&
           !dkrt_phrase_get(PH_P_FLIP));
    phrase_bind(0, 0, 0, 0, 0);
    saved_bytes = 4 + 46 * 4;
    phrase_bind(4, 1, 0x20, "fixture/phrase", "TEST");
    assert(dkrt_phrase_get(PH_P_ZONE) == 4 && dkrt_phrase_get(PH_P_RPTS) == PH_RPTS_UNITY / 2);
    /* A stored value out of range is clamped. */
    phrase_bind(0, 0, 0, 0, 0);
    saved_bytes = 4 + PH_PARAM_COUNT * 4;
    memset(saved, 0xff, sizeof(saved));
    saved[0] = 'P';
    saved[1] = 'H';
    saved[2] = 3;
    saved[3] = PH_PARAM_COUNT;
    phrase_bind(5, 1, 0x20, "fixture/phrase", "TEST");
    assert(dkrt_phrase_get(PH_P_ZONE) == 7 && dkrt_phrase_get(PH_P_RATE) == PH_RATE_MAX &&
           dkrt_phrase_get(PH_P_NOTE) == PH_TIME_CHOICES - 1 && dkrt_phrase_get(PH_P_MIX) == 127);
    assert(!dkrt_phrase_get(PH_P_HOLD) && !dkrt_phrase_get(PH_P_INPT) &&
           !dkrt_phrase_get(PH_P_XFAD));
    return 0;
}
