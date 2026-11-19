#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/model.h"
#include "../src/render.h"
/* The provider: the values as the page sets them, and the count of writes. */
static uint32_t raw[PH_PARAM_COUNT], writes;
uint32_t dkrt_phrase_get(uint32_t p)
{
    assert(p < PH_PARAM_COUNT);
    return raw[p];
}
int32_t dkrt_phrase_set(uint32_t p, uint32_t v)
{
    assert(p < PH_PARAM_COUNT);
    /* The page writes only what the provider takes: XFAD is the loop effect's, INPT a switch. */
    assert(p != PH_P_XFAD && (p != PH_P_INPT || v <= 1));
    raw[p] = v;
    ++writes;
    return 0;
}
static void press(PhraseUi *s, uint32_t k)
{
    assert(pu_key(s, k, PU_PRESS) == PU_OWN);
}
static void release(PhraseUi *s, uint32_t k)
{
    assert(pu_key(s, k, PU_RELEASE) == PU_OWN);
}
static void tap(PhraseUi *s, uint32_t k)
{
    press(s, k);
    release(s, k);
}
static void status(PhraseUi *s, uint32_t left, uint32_t right)
{
    uint32_t words[PH_STATUS_WORDS] = {0};
    words[PH_S_TIME_L / 4] = left;
    words[PH_S_TIME_R / 4] = right;
    pu_status(s, words, 1);
}
static void init(PhraseUi *s)
{
    memset(s, 0, sizeof(*s));
    memset(raw, 0, sizeof(raw));
    /* the provider's defaults */
    raw[PH_P_ZONE] = 4;
    raw[PH_P_RATE] = 508;
    raw[PH_P_NOTE] = 8;
    raw[PH_P_RPTS] = 48;
    raw[PH_P_COLR] = 64;
    raw[PH_P_SKEW] = 64;
    raw[PH_P_MIX] = 100;
    writes = 0;
    pu_context(s, 1, 1);
    s->title = "PRISM";
    status(s, 18000, 18000);
}
static void encoders(void)
{
    PhraseUi s;
    init(&s);
    /* C to F and LEVEL: one tick is one step, and the range ends at 0 and 127. */
    static const uint8_t plain[5][2] = {
        {2, PH_P_RPTS}, {3, PH_P_COLR}, {4, PH_P_HALO}, {5, PH_P_SKEW}, {PU_LEVEL, PH_P_MIX}};
    for (uint32_t i = 0; i < 5; ++i) {
        uint32_t encoder = plain[i][0], p = plain[i][1], before = raw[p];
        assert(pu_parameter(encoder) == p);
        assert(pu_turn(&s, encoder, 3, 0) == PU_OWN && raw[p] == before + 3);
        assert(pu_turn(&s, encoder, -1, 0) == PU_OWN && raw[p] == before + 2);
        /* FUNC makes no difference here */
        assert(pu_turn(&s, encoder, 1, 1) == PU_OWN && raw[p] == before + 3);
        assert(pu_turn(&s, encoder, 500, 0) == PU_OWN && raw[p] == 127);
        assert(pu_turn(&s, encoder, -500, 0) == PU_OWN && raw[p] == 0);
    }
    /* A, ZONE: a choice takes four ticks, and one long event moves one zone. */
    assert(pu_parameter(0) == PH_P_ZONE);
    for (uint32_t i = 0; i < 3; ++i)
        assert(pu_turn(&s, 0, 1, 0) == PU_OWN && raw[PH_P_ZONE] == 4);
    assert(pu_turn(&s, 0, 1, 0) == PU_OWN && raw[PH_P_ZONE] == 5);
    assert(pu_turn(&s, 0, 40, 0) == PU_OWN && raw[PH_P_ZONE] == 6);
    assert(pu_turn(&s, 0, -4, 0) == PU_OWN && raw[PH_P_ZONE] == 5);
    for (uint32_t i = 0; i < 12; ++i)
        pu_turn(&s, 0, 4, 0);
    assert(raw[PH_P_ZONE] == PH_ZONES - 1);
    for (uint32_t i = 0; i < 12; ++i)
        pu_turn(&s, 0, -4, 0);
    assert(raw[PH_P_ZONE] == 0);
    /* B, RATE: a coarse step, and with FUNC the fine one. */
    assert(pu_parameter(1) == PH_P_RATE);
    assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_RATE] == 508 + PH_RATE_COARSE);
    assert(pu_turn(&s, 1, -2, 0) == PU_OWN && raw[PH_P_RATE] == 508 - PH_RATE_COARSE);
    assert(pu_turn(&s, 1, 1, 1) == PU_OWN && raw[PH_P_RATE] == 508 - PH_RATE_COARSE + 1);
    assert(pu_turn(&s, 1, -3, 1) == PU_OWN && raw[PH_P_RATE] == 508 - PH_RATE_COARSE - 2);
    assert(pu_turn(&s, 1, 1000, 0) == PU_OWN && raw[PH_P_RATE] == PH_RATE_MAX);
    assert(pu_turn(&s, 1, -1000, 0) == PU_OWN && raw[PH_P_RATE] == 0);
    /* G and H: taken, and nothing is written. */
    uint32_t before = writes;
    for (uint32_t encoder = 6; encoder < 8; ++encoder) {
        assert(pu_parameter(encoder) == PH_PARAM_COUNT);
        assert(pu_turn(&s, encoder, 5, 0) == PU_OWN && pu_turn(&s, encoder, -5, 1) == PU_OWN);
    }
    assert(writes == before);
    /* No such encoder, and a page that is not on top: the stock handler's. */
    assert(pu_turn(&s, 9, 1, 0) == PU_PASS);
    pu_context(&s, 0, 1);
    assert(pu_turn(&s, 2, 1, 0) == PU_PASS && pu_turn(&s, 6, 1, 0) == PU_PASS);
    assert(writes == before);
}
static void presses(void)
{
    PhraseUi s;
    init(&s);
    /* A is FLIP, B is SYNC, C is HOLD: the press toggles, the release is taken and does nothing. */
    static const uint8_t switches[3] = {PH_P_FLIP, PH_P_SYNC, PH_P_HOLD};
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t before = writes;
        press(&s, PU_PUSH + i);
        assert(raw[switches[i]] == 1 && writes == before + 1);
        /* a repeat of the held key and its release toggle nothing */
        assert(pu_key(&s, PU_PUSH + i, PU_REPEAT) == PU_OWN);
        release(&s, PU_PUSH + i);
        assert(raw[switches[i]] == 1 && writes == before + 1);
        tap(&s, PU_PUSH + i);
        assert(raw[switches[i]] == 0);
    }
    /* D to H: taken, so that the stock page does nothing with them; nothing is written. */
    uint32_t before = writes;
    for (uint32_t i = 3; i < 8; ++i)
        tap(&s, PU_PUSH + i);
    assert(writes == before);
    /* FUNC + NO: taken and nothing happens. FUNC itself, NO alone and the other keys are the stock page's. */
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    tap(&s, PU_NO);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, PU_NO, PU_PRESS) == PU_PASS && pu_key(&s, PU_NO, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, PU_FX, PU_PRESS) == PU_PASS && pu_key(&s, PU_FX, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, 11, PU_PRESS) == PU_PASS && pu_key(&s, 11, PU_RELEASE) == PU_PASS);
    assert(pu_key(&s, 64, PU_PRESS) == PU_PASS);
    assert(writes == before);
    /* A key that was down before the page was on top stays the stock handler's gesture. */
    pu_context(&s, 0, 1);
    assert(pu_key(&s, PU_PUSH, PU_PRESS) == PU_PASS);
    pu_context(&s, 1, 1);
    assert(pu_key(&s, PU_PUSH, PU_REPEAT) == PU_PASS && pu_key(&s, PU_PUSH, PU_RELEASE) == PU_PASS);
    assert(raw[PH_P_FLIP] == 0);
    /* The page is covered while C is down: its release is still taken, the next press toggles again. */
    press(&s, PU_PUSH + 2);
    assert(raw[PH_P_HOLD] == 1);
    pu_context(&s, 0, 1);
    release(&s, PU_PUSH + 2);
    pu_context(&s, 1, 1);
    tap(&s, PU_PUSH + 2);
    assert(raw[PH_P_HOLD] == 0);
    /* The release was lost under the cover: the key is up, and the next press is a new gesture. */
    press(&s, PU_PUSH + 2);
    pu_context(&s, 0, 1);
    uint32_t up[2] = {0, 0};
    pu_reconcile(&s, up);
    pu_context(&s, 1, 1);
    tap(&s, PU_PUSH + 2);
    assert(raw[PH_P_HOLD] == 0);
    /* The native flags: bit 4 is a release, bit 3 a repeat, bit 0 a press, bit 1 alone a chord, none a release. */
    assert(pu_native_flags(16) == PU_RELEASE && pu_native_flags(9) == PU_REPEAT);
    assert(pu_native_flags(1) == PU_PRESS && pu_native_flags(2) == PU_CHORD);
    assert(pu_native_flags(0) == PU_RELEASE);
}
static void sync(void)
{
    PhraseUi s;
    init(&s);
    tap(&s, PU_PUSH + 1);
    assert(raw[PH_P_SYNC] == 1 && pu_parameter(1) == PH_P_NOTE);
    /* With SYNC, B steps through the note lengths, four ticks a step, and RATE stays where it was. */
    for (uint32_t i = 0; i < 3; ++i)
        assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_NOTE] == 8);
    assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_NOTE] == 9);
    assert(pu_turn(&s, 1, -4, 1) == PU_OWN && raw[PH_P_NOTE] == 8);
    for (uint32_t i = 0; i < 20; ++i)
        pu_turn(&s, 1, 4, 0);
    assert(raw[PH_P_NOTE] == PH_TIME_CHOICES - 1);
    for (uint32_t i = 0; i < 20; ++i)
        pu_turn(&s, 1, -4, 0);
    assert(raw[PH_P_NOTE] == 0 && raw[PH_P_RATE] == 508);
    /* Three ticks towards the next note are dropped by the switch back: free, the first tick moves RATE. */
    pu_turn(&s, 1, 3, 0);
    tap(&s, PU_PUSH + 1);
    assert(raw[PH_P_SYNC] == 0 && pu_parameter(1) == PH_P_RATE);
    assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_RATE] == 508 + PH_RATE_COARSE);
    assert(raw[PH_P_NOTE] == 0);
}
static void same(const char *got, const char *want)
{
    if (strcmp(got, want)) {
        fprintf(stderr, "got \"%s\", want \"%s\"\n", got, want);
        assert(0);
    }
}
static void formats(void)
{
    char b[16];
    static const char *const zones[PH_ZONES] = {"KARP", "FLNG", "CHOR", "SLAP",
                                                "ECHO", "LONG", "PHRS", "LOOP"};
    for (uint32_t i = 0; i < PH_ZONES; ++i) {
        pu_format(b, PH_P_ZONE, i);
        same(b, zones[i]);
    }
    /* The note lengths in the order of the provider's table, shortest first; every text fits a cell. */
    static const char *const notes[PH_TIME_CHOICES] = {
        "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8",  "1/4T",
        "1/8.", "1/4",   "1/2T",  "1/4.", "1/2",  "1/2.",  "1BAR", "2BAR"};
    for (uint32_t i = 0; i < PH_TIME_CHOICES; ++i) {
        pu_format(b, PH_P_NOTE, i);
        same(b, notes[i]);
        assert(strlen(b) <= 7);
    }
    pu_format(b, PH_P_RPTS, 62);
    same(b, "62");
    pu_format(b, PH_P_HALO, 0);
    same(b, "0");
    pu_format(b, PH_P_MIX, 127);
    same(b, "127");
    pu_format(b, PH_P_COLR, 44);
    same(b, "20");
    pu_format(b, PH_P_COLR, 64);
    same(b, "0");
    pu_format(b, PH_P_COLR, 108);
    same(b, "44");
    pu_format(b, PH_P_SKEW, 52);
    same(b, "L 12");
    pu_format(b, PH_P_SKEW, 64);
    same(b, "0");
    pu_format(b, PH_P_SKEW, 94);
    same(b, "R 30");
    pu_format(b, PH_P_SKEW, 0);
    same(b, "L 64");
    pu_format(b, PH_P_SKEW, 127);
    same(b, "R 63");
    pu_time(b, 48);
    same(b, "1 MS");
    pu_time(b, 18000);
    same(b, "375 MS");
    pu_time(b, 47975);
    same(b, "999 MS");
    pu_time(b, 48000);
    same(b, "1.00 S");
    pu_time(b, 60000);
    same(b, "1.25 S");
    pu_time(b, 479700);
    same(b, "9.99 S");
    pu_time(b, 479900);
    same(b, "10.0 S");
    pu_time(b, 516000);
    same(b, "10.8 S");
}
static void page_cells(void)
{
    PhraseUi s;
    PhrasePage p;
    init(&s);
    pu_page(&s, &p);
    same(p.title, "PRISM");
    same(p.zone, "ECHO");
    same(p.time, "375 MS");
    assert(p.mix == 100 && p.zone_index == 4);
    static const char *const defaults[8][3] = {{"ZONE", "ECHO", ""}, {"RATE", "375 MS", "FREE"},
                                               {"RPTS", "48", ""},   {"COLR", "0", ""},
                                               {"HALO", "0", ""},    {"SKEW", "0", ""},
                                               {"INPT", "LIVE", ""}, {"XFAD", "--", "NO LOOP"}};
    for (uint32_t i = 0; i < 8; ++i) {
        same(p.cells[i].label, defaults[i][0]);
        same(p.cells[i].value, defaults[i][1]);
        same(p.cells[i].state, defaults[i][2]);
        assert(!p.cells[i].lit);
    }
    /* SYNC: the note length in the RATE cell, and the header keeps the time the engine plays. */
    raw[PH_P_SYNC] = 1;
    raw[PH_P_NOTE] = 8;
    status(&s, 9000, 9000);
    pu_page(&s, &p);
    same(p.cells[1].value, "1/8.");
    same(p.cells[1].state, "SYNC");
    same(p.time, "188 MS");
    /* HOLD and FLIP with RATE free, as the plan draws it. */
    raw[PH_P_SYNC] = 0;
    raw[PH_P_ZONE] = 3;
    raw[PH_P_FLIP] = raw[PH_P_HOLD] = 1;
    raw[PH_P_COLR] = 108;
    raw[PH_P_HALO] = 70;
    raw[PH_P_SKEW] = 94;
    raw[PH_P_MIX] = 104;
    /* SKEW makes one side shorter and the other longer by one ratio: the page shows the time between them. */
    status(&s, 3312, 5888);
    pu_page(&s, &p);
    same(p.zone, "SLAP");
    same(p.time, "92 MS");
    same(p.cells[0].value, "SLAP");
    same(p.cells[0].state, "FLIP");
    same(p.cells[1].value, "92 MS");
    same(p.cells[1].state, "FREE");
    same(p.cells[2].value, "INF");
    same(p.cells[2].state, "HOLD");
    same(p.cells[3].value, "44");
    same(p.cells[3].state, "BRIT");
    same(p.cells[4].value, "70");
    same(p.cells[5].value, "R 30");
    assert(p.cells[0].lit && p.cells[2].lit && !p.cells[1].lit && p.mix == 104);
    raw[PH_P_COLR] = 44;
    pu_page(&s, &p);
    same(p.cells[3].value, "20");
    same(p.cells[3].state, "DARK");
    /* Without the loop effect's home the loop cells say there is none. */
    same(p.cells[6].value, "LIVE");
    same(p.cells[7].state, "NO LOOP");
    /* Long times: the mean of two sides of more than 16 bits is right to 16 frames. */
    status(&s, 400000, 600000);
    assert(pu_frames(&s) >= 489898 - 16 && pu_frames(&s) <= 489898 + 16);
    status(&s, 516000, 516000);
    assert(pu_frames(&s) == 516000);
    /* A status of an engine that has no time yet, and no status at all. */
    status(&s, 0, 0);
    pu_page(&s, &p);
    same(p.time, "--");
    same(p.cells[1].value, "--");
    s.fresh = 0;
    pu_page(&s, &p);
    same(p.time, "NO LINK");
    same(p.cells[1].value, "--");
}
/* A canvas that checks what the page draws: inside the screen, every text inside its place. */
typedef struct {
    uint32_t boxes, texts, inverted;
    char seen[64][24];
} Drawn;
static void drawn_box(void *ctx, int x, int y, int w, int h, int ink)
{
    Drawn *d = ctx;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 128 && y + h <= 64);
    assert(ink == 0 || ink == 1);
    ++d->boxes;
}
static void drawn_text(void *ctx, int x, int y, const char *s, int ink)
{
    Drawn *d = ctx;
    /* The label font is at most 4 pixels a character and 5 high. A cell's text has at most 7 characters; the header's loop
     * line (at the top) more. */
    int half = (int)strlen(s) * 4 / 2;
    assert(*s && (strlen(s) <= 7 || (y == 2 && strlen(s) < 24)) && x - half >= 0 &&
           x + half <= 128 && y >= 0 && y + 5 <= 64);
    assert(d->texts < 64);
    strcpy(d->seen[d->texts++], s);
    d->inverted += !ink;
}
static int saw(const Drawn *d, const char *s)
{
    for (uint32_t i = 0; i < d->texts; ++i)
        if (!strcmp(d->seen[i], s))
            return 1;
    return 0;
}
static void rendering(void)
{
    PhraseUi s;
    PhrasePage p;
    init(&s);
    Drawn d = {0};
    PhraseCanvas c = {&d, drawn_box, drawn_text};
    pu_page(&s, &p);
    pu_render(&p, &c);
    static const char *const shown[] = {"PRISM", "ECHO", "375 MS", "MIX",  "100",  "ZONE",
                                        "RATE",  "FREE", "RPTS",   "48",   "COLR", "HALO",
                                        "SKEW",  "INPT", "LIVE",   "XFAD", "--",   "NO LOOP"};
    for (uint32_t i = 0; i < sizeof(shown) / sizeof(*shown); ++i)
        if (!saw(&d, shown[i])) {
            fprintf(stderr, "not drawn: %s\n", shown[i]);
            assert(0);
        }
    assert(!d.inverted);
    /* Every value at both ends of its range, with every switch on, stays on the screen. */
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < PH_PARAM_COUNT; ++i)
            raw[i] = pass ? 127 : 0;
        raw[PH_P_ZONE] = pass ? PH_ZONES - 1 : 0;
        raw[PH_P_NOTE] = pass ? PH_TIME_CHOICES - 1 : 0;
        raw[PH_P_SYNC] = raw[PH_P_FLIP] = raw[PH_P_HOLD] = pass;
        status(&s, pass ? 344064 : 48, pass ? 516096 : 72);
        memset(&d, 0, sizeof(d));
        pu_page(&s, &p);
        pu_render(&p, &c);
        /* FLIP and HOLD are drawn inverted when they are on. */
        assert(d.inverted == (pass ? 2u : 0u));
        assert(saw(&d, "FLIP") == (int)pass && saw(&d, "HOLD") == (int)pass);
    }
    /* The header alone: three texts, nothing below the bar. */
    memset(&d, 0, sizeof(d));
    pu_render_header(&p, &c);
    assert(d.texts == 3 && saw(&d, "PRISM") && saw(&d, "LOOP"));
}

/* The loop effect's home as core/loop-control keeps it (the crossfader and the counts of ARM and DROP), and its status. */
static uint32_t home_xfad, home_arms, home_drops, home_fresh, home_words[LS_WORDS];
static uint32_t home_get(uint32_t i)
{
    return i == LP_XFAD ? home_xfad : 0;
}
static void home_set_xfad(uint32_t v)
{
    assert(v <= LP_MAX);
    home_xfad = v;
}
static void home_arm(void)
{
    ++home_arms;
}
static void home_drop(void)
{
    ++home_drops;
}
static uint32_t home_status(uint32_t *out)
{
    memcpy(out, home_words, sizeof(home_words));
    return home_fresh;
}
static const PhraseLoop fake_home = {home_get, home_set_xfad, home_arm, home_drop, home_status};
/* The engine's report: state bits, loops taken, the pattern's steps, the loop's steps and frames, the crossfader applied. */
static void report(uint32_t bits, uint32_t count, uint32_t steps, uint32_t loop_steps,
                   uint32_t frames, float gain)
{
    memset(home_words, 0, sizeof(home_words));
    home_words[LS_MAGIC] = LS_MAGIC_VALUE;
    home_words[LS_STATE] = bits | count << LS_COUNT_SHIFT;
    home_words[LS_STEPS] = loop_steps | steps << 8;
    home_words[LS_LOOP_FRAMES] = frames;
    memcpy(&home_words[LS_GAIN], &gain, 4);
    home_fresh = 1;
}
static void loop_page(void)
{
    PhraseUi s;
    PhrasePage p;
    init(&s);
    s.loop = &fake_home;
    home_xfad = home_arms = home_drops = 0;
    /* Pattern 1 plays, no loop yet: INPT stays LIVE whatever G does, H and FUNC + NO move nothing; a press of H arms. */
    report(LS_TRACK, 0, 16, 0, 0, 0.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 0);
    assert(pu_turn(&s, 6, 1, 0) == PU_OWN && pu_turn(&s, 7, 5, 0) == PU_OWN &&
           raw[PH_P_INPT] == 0 && home_xfad == 0);
    tap(&s, PU_PUSH + 6);
    assert(raw[PH_P_INPT] == 0);
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    tap(&s, PU_NO);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS && home_drops == 0);
    pu_page(&s, &p);
    same(p.cells[6].value, "LIVE");
    same(p.cells[7].value, "--");
    same(p.cells[7].state, "NO LOOP");
    tap(&s, PU_PUSH + 7);
    assert(home_arms == 1);
    report(LS_TRACK | LS_ARMED, 0, 16, 0, 0, 0.0f);
    pu_loop(&s);
    pu_page(&s, &p);
    same(p.cells[7].value, "ARMD");
    same(p.cells[7].state, "16 ST");
    assert(!p.cells[7].lit && !p.cells[7].fader && !p.line[0]);
    report(LS_ARMED, 0, 0, 0, 0, 0.0f);
    pu_loop(&s);
    pu_page(&s, &p);
    same(p.cells[7].state, "WAIT");
    report(LS_TRACK | LS_ARMED | LS_NEXT_CUT, 0, 128, 0, 0, 0.0f);
    pu_loop(&s);
    pu_page(&s, &p);
    same(p.cells[7].state, "CUT");
    /* The armed change takes a loop: the home puts the crossfader at LOOP, and INPT goes to LOOP by itself. */
    home_xfad = LP_MAX;
    report(LS_LOOP | LS_HELD | LS_TRACK, 1, 16, 16, 96000, 1.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    pu_page(&s, &p);
    same(p.cells[6].value, "LOOP");
    same(p.cells[7].value, "LOOP");
    assert(p.cells[7].fader == 128);
    same(p.line, "LOOP 16 ST 2.00 S");
    /* H is the crossfader, a step a tick, through the home; the cell shows the number between the ends. */
    assert(pu_turn(&s, 7, -64, 0) == PU_OWN && home_xfad == 63);
    report(LS_LOOP | LS_TRACK, 1, 16, 16, 96000, 63.0f / 127.0f);
    pu_loop(&s);
    pu_page(&s, &p);
    same(p.cells[7].value, "63");
    assert(p.cells[7].fader >= 63 && p.cells[7].fader <= 64);
    assert(pu_turn(&s, 7, -500, 0) == PU_OWN && home_xfad == 0);
    report(LS_LOOP | LS_TRACK, 1, 16, 16, 96000, 0.0f);
    /* At LIVE INPT stays at LOOP: switching it is the performer's move. */
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    pu_page(&s, &p);
    same(p.cells[7].value, "LIVE");
    assert(p.cells[7].fader == 1);
    /* G: a press switches, a tick up is LOOP and a tick down LIVE; the header names the loop only while PRISM hears it. */
    tap(&s, PU_PUSH + 6);
    assert(raw[PH_P_INPT] == 0);
    pu_page(&s, &p);
    assert(!p.line[0]);
    same(p.zone, "ECHO");
    assert(pu_turn(&s, 6, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, 3, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
    /* Arming again while the loop plays: ARMD, drawn inverted, in place of the crossfader's line. */
    tap(&s, PU_PUSH + 7);
    assert(home_arms == 2);
    report(LS_LOOP | LS_ARMED | LS_TRACK, 1, 16, 16, 96000, 0.0f);
    pu_loop(&s);
    pu_page(&s, &p);
    same(p.cells[7].state, "ARMD");
    assert(p.cells[7].lit && !p.cells[7].fader);
    /* The next loop: INPT goes to LOOP again, from LIVE. */
    report(LS_LOOP | LS_HELD | LS_TRACK, 2, 16, 16, 96000, 1.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    /* FUNC + NO drops through the home; without a loop INPT is LIVE. */
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    tap(&s, PU_NO);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS && home_drops == 1);
    report(LS_TRACK, 2, 16, 0, 0, 0.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 0);
    /* A page that first sees a loop already taken does not switch INPT (it was not this change). */
    PhraseUi late;
    memset(&late, 0, sizeof(late));
    pu_context(&late, 1, 1);
    late.loop = &fake_home;
    report(LS_LOOP | LS_TRACK, 3, 16, 16, 96000, 1.0f);
    pu_loop(&late);
    assert(raw[PH_P_INPT] == 0);
    report(LS_LOOP | LS_HELD | LS_TRACK, 4, 16, 16, 96000, 1.0f);
    pu_loop(&late);
    assert(raw[PH_P_INPT] == 1);
    /* The loop effect stops reporting: the cells say there is no loop, and after PU_LOOP_GONE passes INPT is LIVE. */
    home_fresh = 0;
    for (uint32_t i = 0; i < PU_LOOP_GONE; ++i)
        pu_loop(&late);
    assert(raw[PH_P_INPT] == 1);
    pu_loop(&late);
    assert(raw[PH_P_INPT] == 0);
    pu_page(&late, &p);
    same(p.cells[7].value, "--");
    same(p.cells[7].state, "NO LOOP");
    uint32_t arms = home_arms;
    tap(&late, PU_PUSH + 7);
    assert(home_arms == arms);
    /* What the loop's page shows is drawn on the screen: the header line, the crossfader's line. */
    report(LS_LOOP | LS_TRACK, 4, 16, 16, 96000, 0.5f);
    raw[PH_P_INPT] = 1;
    home_xfad = 64;
    pu_loop_status(&s);
    Drawn d = {0};
    PhraseCanvas c = {&d, drawn_box, drawn_text};
    pu_page(&s, &p);
    pu_render(&p, &c);
    assert(saw(&d, "LOOP 16 ST 2.00 S") && saw(&d, "64"));
    memset(&d, 0, sizeof(d));
    pu_render_header(&p, &c);
    assert(d.texts == 2 && saw(&d, "PRISM") && saw(&d, "LOOP 16 ST 2.00 S"));
}
int main(void)
{
    encoders();
    presses();
    sync();
    formats();
    page_cells();
    rendering();
    loop_page();
    puts("phrase ui: ok");
    return 0;
}
