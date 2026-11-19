/* Host harness of core/phrase-control: the provider's own source with stand-ins for the runtime it calls. */
#include <assert.h>
#include <string.h>
#include "../src/control.c"
static unsigned writes;
static uint8_t saved[64];
static u32 saved_bytes;
/* The fx service's record of the effect's send amounts (fx/send/<module id>), which only the carry-over reads. */
static uint8_t sends_record[16];
static u32 sends_bytes;
u32 dkrt_irq_mask(void)
{
    return 0;
}
void dkrt_irq_restore(u32 v)
{
    (void)v;
}
/* The runtime's rule (settings.rs): the value's length, its bytes when they fit, -1 without the key. */
static int get(const uint8_t *value, u32 bytes, void *p, u32 n)
{
    if (!bytes)
        return -1;
    if (bytes > n)
        return -2; /* E_SET_VALUE: nothing written */
    memcpy(p, value, bytes);
    return (int)bytes;
}
int dkrt_settings_get(const char *k, void *p, u32 n)
{
    if (!strcmp(k, "fx/send/fixture/phrase"))
        return get(sends_record, sends_bytes, p, n);
    assert(!strcmp(k, "phrase/fixture/phrase"));
    return get(saved, saved_bytes, p, n);
}
int dkrt_settings_put(const char *k, const void *p, u32 n)
{
    /* the provider never writes the fx service's record */
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
    /* INPT is a switch, published as it is and not saved (a restart has no loop); XFAD is the loop effect's and cannot be
     * set here, and the words of the crossfader and of ARM stay 0 */
    u32 before = writes;
    assert(dkrt_phrase_set(PH_P_INPT, 5) == 0 && dkrt_phrase_get(PH_P_INPT) == 1 &&
           word(PH_C_INPUT) == 1);
    assert(dkrt_phrase_set(PH_P_INPT, 0) == 0 && word(PH_C_INPUT) == 0 && writes == before);
    assert(dkrt_phrase_set(PH_P_XFAD, 64) == -2);
    assert(word(PH_C_XFADE) == 0 && word(PH_C_ARM_SEQ) == 0);
    assert(dkrt_phrase_set(PH_PARAM_COUNT, 1) == -1);
}

/* SYNC as dk_phrase.inc defines it, in doubles, from the zones' free ranges in milliseconds (the engine's table): Z0 is the
 * zone whose centre sqrt(low * high) is nearest the beat in log terms, that is whose low * high / beat ^ 2 is nearest 1 in
 * ratio. */
static const double zone_ms[PH_ZONES][2] = {{1, 10},     {5, 40},     {20, 120},    {60, 350},
                                            {150, 1000}, {500, 2700}, {1300, 5400}, {2700, 10700}};
static double beat_of(u32 tempo120)
{
    return 345600000.0 / tempo120;
}
static u32 zone_of(double beat)
{
    u32 best = 0;
    double best_far = 0;
    for (u32 z = 0; z < PH_ZONES; ++z) {
        double r = zone_ms[z][0] * 48 * zone_ms[z][1] * 48 / (beat * beat), far = r > 1 ? r : 1 / r;
        if (!z || far < best_far) {
            best = z;
            best_far = far;
        }
    }
    return best;
}
/* The ratios of dk_phrase.inc, shortest first. */
static const double ratio[PH_SYNC_RATIOS] = {1.0 / 2, 4.0 / 7, 3.0 / 5, 2.0 / 3, 3.0 / 4,
                                             4.0 / 5, 1,       5.0 / 4, 4.0 / 3, 3.0 / 2,
                                             5.0 / 3, 7.0 / 4, 2};
static u32 expected(u32 tempo120, u32 zone, u32 r)
{
    double beat = beat_of(tempo120), frames = beat * ratio[r];
    for (u32 z = zone_of(beat); z < zone; ++z)
        frames *= 2;
    for (u32 z = zone; z < zone_of(beat); ++z)
        frames /= 2;
    return frames > PH_SYNC_MAX ? PH_SYNC_MAX : (u32)(frames + 0.5);
}
static void sync_times(void)
{
    /* The default: the zone's 1/1. */
    assert(PH_SYNC_RATIOS == 13 && ratio[PH_SYNC_UNITY] == 1);
    /* Z0, the zone of the beat: 60 BPM (1 s) LONG, 96 BPM (625 ms) ECHO, 120 and 180 BPM ECHO, above 253 BPM SLAP, below
     * 34.2 BPM PHRS; the provider's edges agree with the definition at every tempo but within a frame of an edge. */
    assert(sync_zone(7200) == 5 && zone_of(beat_of(7200)) == 5);
    assert(sync_zone(11520) == 4 && zone_of(beat_of(11520)) == 4);
    assert(sync_zone(14400) == 4 && sync_zone(21600) == 4);
    assert(sync_zone(36000) == 3 && sync_zone(3600) == 6);
    static const u32 edges[PH_ZONES - 1] = {PH_SYNC_EDGE_1, PH_SYNC_EDGE_2, PH_SYNC_EDGE_3,
                                            PH_SYNC_EDGE_4, PH_SYNC_EDGE_5, PH_SYNC_EDGE_6,
                                            PH_SYNC_EDGE_7};
    for (u32 tempo = 3600; tempo <= 36000; ++tempo) {
        double beat = beat_of(tempo);
        int near_edge = 0;
        for (u32 e = 0; e < PH_ZONES - 1; ++e)
            near_edge |= beat > edges[e] - 1.0 && beat < edges[e] + 1.0;
        assert(near_edge || sync_zone(tempo) == zone_of(beat));
    }
    /* At 96 BPM the beat is 30000 frames: ECHO's 1/1. Every ratio there and in SLAP, an octave below, is the formula's. */
    assert(dkrt_phrase_set(PH_P_ZONE, 4) == 0);
    for (u32 r = 0; r < PH_SYNC_RATIOS; ++r) {
        assert(dkrt_phrase_set(PH_P_RATIO, r) == 0);
        assert(sent(11520, PH_C_SYNC_TIME) == expected(11520, 4, r));
    }
    assert(dkrt_phrase_set(PH_P_RATIO, PH_SYNC_UNITY) == 0 && sent(11520, PH_C_SYNC_TIME) == 30000);
    assert(dkrt_phrase_set(PH_P_RATIO, 1) == 0 && sent(11520, PH_C_SYNC_TIME) == 17143); /* 4/7 */
    assert(dkrt_phrase_set(PH_P_ZONE, 3) == 0);
    for (u32 r = 0; r < PH_SYNC_RATIOS; ++r) {
        assert(dkrt_phrase_set(PH_P_RATIO, r) == 0);
        assert(sent(11520, PH_C_SYNC_TIME) == expected(11520, 3, r));
    }
    assert(dkrt_phrase_set(PH_P_RATIO, PH_SYNC_UNITY) == 0 && sent(11520, PH_C_SYNC_TIME) == 15000);
    /* LONG is a half note at 96 BPM; at 60 BPM LONG is the beat itself. */
    assert(dkrt_phrase_set(PH_P_ZONE, 5) == 0 && sent(11520, PH_C_SYNC_TIME) == 60000);
    assert(sent(7200, PH_C_SYNC_TIME) == 48000);
    /* Every zone, ratio and tempo is the formula's, to the frame, but within a frame of an edge. */
    for (u32 zone = 0; zone < PH_ZONES; ++zone)
        for (u32 r = 0; r < PH_SYNC_RATIOS; ++r)
            for (u32 tempo = 3600; tempo <= 36000; tempo += 37) {
                double beat = beat_of(tempo);
                int near_edge = 0;
                for (u32 e = 0; e < PH_ZONES - 1; ++e)
                    near_edge |= beat > edges[e] - 1.0 && beat < edges[e] + 1.0;
                u32 got = sync_time_of(tempo, zone, r), want = expected(tempo, zone, r);
                assert(near_edge || got == want || got + 1 == want || got == want + 1);
            }
    /* The top: at 35 BPM the beat (1.71 s) is LONG's 1/1, LOOP is four of it (6.86 s), and from 5/3 on LOOP is the longest
     * time, 10.7 s. */
    assert(dkrt_phrase_set(PH_P_ZONE, 7) == 0);
    assert(dkrt_phrase_set(PH_P_RATIO, 9) == 0 &&
           sent(4200, PH_C_SYNC_TIME) == expected(4200, 7, 9));
    assert(sent(4200, PH_C_SYNC_TIME) < PH_SYNC_MAX);
    for (u32 r = 10; r < PH_SYNC_RATIOS; ++r) {
        assert(dkrt_phrase_set(PH_P_RATIO, r) == 0);
        assert(sent(4200, PH_C_SYNC_TIME) == PH_SYNC_MAX);
    }
    /* Without a tempo the last one stays; a tempo out of range is none. */
    assert(dkrt_phrase_set(PH_P_ZONE, 4) == 0 && dkrt_phrase_set(PH_P_RATIO, PH_SYNC_UNITY) == 0);
    assert(sent(10800, PH_C_SYNC_TIME) == 32000); /* 90 BPM: ECHO's 1/1 is the beat */
    assert(sent(0, PH_C_SYNC_TIME) == 32000);
    assert(sent(100, PH_C_SYNC_TIME) == 32000);
    /* and a zone or ratio change without a tempo is taken at that tempo */
    assert(dkrt_phrase_set(PH_P_RATIO, 12) == 0 && sent(0, PH_C_SYNC_TIME) == 64000);
    assert(dkrt_phrase_set(PH_P_RATIO, PH_SYNC_UNITY) == 0);
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
    /* the same snapshot again and again (an engine that stopped, its last status still returned) is fresh only for half a
     * second after its heartbeat last moved */
    s[PH_S_HEARTBEAT / 4] = s[PH_S_SEQ_END / 4] = 78;
    for (u32 n = 0; n < 5; ++n, frame += 200) {
        for (u32 i = 0; i < 3; ++i) {
            packet(wire, i, 20 + n, s);
            reply(wire);
        }
        assert(dkrt_phrase_snapshot(out, 32) == (n < 4));
    }
    /* a heartbeat that moves again: fresh */
    s[PH_S_HEARTBEAT / 4] = s[PH_S_SEQ_END / 4] = 79;
    for (u32 i = 0; i < 3; ++i) {
        packet(wire, i, 30, s);
        reply(wire);
    }
    assert(dkrt_phrase_snapshot(out, 32));
}

/* A saved record of format 3 with COUNT values: value i is i + 1 (in range for each), the tracks (if counted) TRACKS. */
static void store(u32 count, u32 tracks)
{
    memset(saved, 0, sizeof(saved));
    saved[0] = 'P';
    saved[1] = 'H';
    saved[2] = 3;
    saved[3] = (uint8_t)count;
    for (u32 i = 0; i < count; ++i) {
        u32 v = i == PH_P_TRKS ? tracks : i + 1;
        for (u32 j = 0; j < 4; ++j)
            saved[4 + 4 * i + j] = (uint8_t)(v >> (24 - 8 * j));
    }
    saved_bytes = 4 + 4 * count;
}
static void sends(const uint8_t *amounts, u32 bytes)
{
    memset(sends_record, 0, sizeof(sends_record));
    if (amounts)
        memcpy(sends_record, amounts, bytes);
    sends_bytes = bytes;
}
static void restart(u32 own)
{
    phrase_bind(0, 0, 0, 0, 0);
    phrase_bind(own, 1, 0x20, "fixture/phrase", "TEST");
}
/* The tracks the effect hears: a saved value, global like the others; the send amounts the fx service gets for them; where
 * the first ones come from on a device that had none. */
static void tracks(void)
{
    uint8_t a[16];
    /* The owner's device: PRISM's record from before the tracks (13 values) and the fx service's record with 127 for tracks 1
     * and 11. The tracks are those two, the 13 values are kept, and the bind writes nothing. */
    store(13, 0);
    uint8_t owner_amounts[16] = {127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0};
    sends(owner_amounts, 16);
    unsigned n = writes;
    restart(6);
    assert(dkrt_phrase_get(PH_P_TRKS) == (1u | 1u << 10));
    assert(dkrt_phrase_get(PH_P_ZONE) == 1 && dkrt_phrase_get(PH_P_RATE) == 2 &&
           dkrt_phrase_get(PH_P_MIX) == 11 && dkrt_phrase_get(PH_P_SKEW) == 10);
    assert(writes == n && saved_bytes == 4 + 13 * 4);
    assert(phrase_sends(a) == 1);
    for (u32 t = 0; t < 16; ++t)
        assert(a[t] == (t == 0 || t == 10 ? PH_SEND_FULL : 0));
    /* A change saves the tracks with every value; after a restart they are the saved ones, not the send amounts. */
    assert(dkrt_phrase_set(PH_P_TRKS, 1u << 2) == 0 && writes == n + 1);
    assert(saved_bytes == 4 + PH_PARAM_COUNT * 4 && saved[3] == PH_PARAM_COUNT);
    restart(7);
    assert(dkrt_phrase_get(PH_P_TRKS) == 1u << 2 && dkrt_phrase_get(PH_P_ZONE) == 1);
    assert(phrase_sends(a) == 1 && a[2] == PH_SEND_FULL && a[0] == 0 && a[10] == 0);
    /* No track at all is saved as such. */
    assert(dkrt_phrase_set(PH_P_TRKS, 0) == 0);
    restart(8);
    assert(dkrt_phrase_get(PH_P_TRKS) == 0);
    /* Without PRISM's record any amount above 0 counts as heard. */
    saved_bytes = 0;
    uint8_t other[16] = {0, 64, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 200};
    sends(other, 16);
    restart(9);
    assert(dkrt_phrase_get(PH_P_TRKS) == (1u << 1 | 1u << 2 | 1u << 15));
    assert(dkrt_phrase_get(PH_P_ZONE) == 4 && dkrt_phrase_get(PH_P_MIX) == 100); /* the defaults */
    /* Nothing stored: no track, and the sends are all 0. A send record of another size is not read. */
    saved_bytes = 0;
    sends(0, 0);
    restart(10);
    assert(dkrt_phrase_get(PH_P_TRKS) == 0);
    assert(phrase_sends(a) == 1);
    for (u32 t = 0; t < 16; ++t)
        assert(a[t] == 0);
    sends(owner_amounts, 8);
    restart(11);
    assert(dkrt_phrase_get(PH_P_TRKS) == 0);
    /* The owner's record from the note lengths: a NOTE of 15 (2BAR) is the ratio 12 (X2), a NOTE of 8 the ratio 8; the
     * other values and the tracks are kept. */
    store(PH_PARAM_COUNT, 0x0801);
    saved[4 + 4 * PH_P_RATIO + 3] = 15;
    restart(13);
    assert(dkrt_phrase_get(PH_P_RATIO) == PH_SYNC_RATIOS - 1 &&
           dkrt_phrase_get(PH_P_TRKS) == 0x0801);
    assert(dkrt_phrase_get(PH_P_ZONE) == 1 && dkrt_phrase_get(PH_P_MIX) == 11 &&
           dkrt_phrase_get(PH_P_SKEW) == 10);
    saved[4 + 4 * PH_P_RATIO + 3] = 8;
    restart(14);
    assert(dkrt_phrase_get(PH_P_RATIO) == 8);
    /* Nothing stored: the 1/1. */
    saved_bytes = 0;
    restart(15);
    assert(dkrt_phrase_get(PH_P_RATIO) == PH_SYNC_UNITY);
    /* A record of 14 values with the tracks out of range: clamped to the 16 tracks. */
    store(PH_PARAM_COUNT, 0x12345);
    restart(12);
    assert(dkrt_phrase_get(PH_P_TRKS) == PH_TRACKS_ALL);
    assert(dkrt_phrase_set(PH_P_TRKS, 0x10000) == 0 && dkrt_phrase_get(PH_P_TRKS) == PH_TRACKS_ALL);
    /* The tracks are no control word: setting them publishes the same words. */
    u32 before[PH_CONTROL_WORDS];
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i)
        before[i] = control[published][i];
    assert(dkrt_phrase_set(PH_P_TRKS, 5) == 0);
    for (u32 i = 0; i < PH_CONTROL_WORDS; ++i)
        assert(control[published][i] == before[i]);
    /* No owner: nothing is written, and the fx service gets nothing. */
    phrase_bind(0, 0, 0, 0, 0);
    memset(a, 0x55, sizeof(a));
    assert(phrase_sends(a) == 0 && a[0] == 0x55 && phrase_sends(0) == 0);
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
           dkrt_phrase_get(PH_P_RATIO) == PH_SYNC_RATIOS - 1 && dkrt_phrase_get(PH_P_MIX) == 127);
    assert(!dkrt_phrase_get(PH_P_HOLD) && !dkrt_phrase_get(PH_P_INPT) &&
           !dkrt_phrase_get(PH_P_XFAD) && dkrt_phrase_get(PH_P_TRKS) == PH_TRACKS_ALL);
    tracks();
    return 0;
}
