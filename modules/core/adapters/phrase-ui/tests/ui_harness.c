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
    raw[PH_P_RATIO] = PH_SYNC_UNITY;
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
    /* G is INPT's switch, also without a loop: right LOOP, left LIVE, once each way. H is taken and writes nothing. */
    uint32_t before = writes;
    for (uint32_t encoder = 6; encoder < 8; ++encoder)
        assert(pu_parameter(encoder) == PH_PARAM_COUNT);
    assert(pu_turn(&s, 6, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1 && writes == before + 1);
    assert(pu_turn(&s, 6, 5, 0) == PU_OWN && raw[PH_P_INPT] == 1 && writes == before + 1);
    assert(pu_turn(&s, 6, 0, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, -5, 1) == PU_OWN && raw[PH_P_INPT] == 0 && writes == before + 2);
    assert(pu_turn(&s, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0 && writes == before + 2);
    before = writes;
    assert(pu_turn(&s, 7, 5, 0) == PU_OWN && pu_turn(&s, 7, -5, 1) == PU_OWN);
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
    assert(raw[PH_P_SYNC] == 1 && pu_parameter(1) == PH_P_RATIO);
    /* With SYNC, B steps through the ratios of the zone's 1/1, four ticks a step, toward X2 to the right as a free RATE
     * goes longer, and RATE stays where it was. */
    for (uint32_t i = 0; i < 3; ++i)
        assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_RATIO] == PH_SYNC_UNITY);
    assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_RATIO] == PH_SYNC_UNITY + 1);
    assert(pu_turn(&s, 1, -4, 1) == PU_OWN && raw[PH_P_RATIO] == PH_SYNC_UNITY);
    for (uint32_t i = 0; i < 20; ++i)
        pu_turn(&s, 1, 4, 0);
    assert(raw[PH_P_RATIO] == PH_SYNC_RATIOS - 1 && PH_SYNC_RATIOS == 13);
    for (uint32_t i = 0; i < 20; ++i)
        pu_turn(&s, 1, -4, 0);
    assert(raw[PH_P_RATIO] == 0 && raw[PH_P_RATE] == 508);
    /* Three ticks towards the next note are dropped by the switch back: free, the first tick moves RATE. */
    pu_turn(&s, 1, 3, 0);
    tap(&s, PU_PUSH + 1);
    assert(raw[PH_P_SYNC] == 0 && pu_parameter(1) == PH_P_RATE);
    assert(pu_turn(&s, 1, 1, 0) == PU_OWN && raw[PH_P_RATE] == 508 + PH_RATE_COARSE);
    assert(raw[PH_P_RATIO] == 0);
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
    /* The SYNC ratios in the order of the provider's table (1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 1, 5/4, 4/3, 3/2, 5/3, 7/4, 2),
     * shortest first, as the Mimeophon names them; every text fits a cell. */
    static const char *const ratios[PH_SYNC_RATIOS] = {"/2",    "/1.75", "/1.67", "/1.5",  "/1.33",
                                                       "/1.25", "1/1",   "X1.25", "X1.33", "X1.5",
                                                       "X1.67", "X1.75", "X2"};
    for (uint32_t i = 0; i < PH_SYNC_RATIOS; ++i) {
        pu_format(b, PH_P_RATIO, i);
        same(b, ratios[i]);
        assert(strlen(b) <= 7);
    }
    same(ratios[PH_SYNC_UNITY], "1/1");
    /* a value out of range is the 1/1 */
    pu_format(b, PH_P_RATIO, PH_SYNC_RATIOS);
    same(b, "1/1");
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
    /* INPT: no track is heard until one is chosen */
    static const char *const defaults[8][3] = {
        {"ZONE", "ECHO", ""},       {"RATE", "375 MS", "FREE"}, {"RPTS", "48", ""},
        {"COLR", "0", ""},          {"HALO", "0", ""},          {"SKEW", "0", ""},
        {"INPT", "LIVE", "NO TRK"}, {"XFAD", "--", "NO LOOP"}};
    for (uint32_t i = 0; i < 8; ++i) {
        same(p.cells[i].label, defaults[i][0]);
        same(p.cells[i].value, defaults[i][1]);
        same(p.cells[i].state, defaults[i][2]);
        assert(!p.cells[i].lit);
    }
    /* SYNC: the ratio in the RATE cell, and the header keeps the time the engine plays; ZONE stays the zone (its octave). */
    raw[PH_P_SYNC] = 1;
    raw[PH_P_RATIO] = 9;
    status(&s, 36000, 36000);
    pu_page(&s, &p);
    same(p.cells[1].value, "X1.5");
    same(p.cells[1].state, "SYNC");
    same(p.time, "750 MS");
    same(p.cells[0].value, "ECHO");
    same(p.zone, "ECHO");
    raw[PH_P_RATIO] = PH_SYNC_UNITY;
    pu_page(&s, &p);
    same(p.cells[1].value, "1/1");
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
    int centres[64], tops[64];
    uint8_t pixels[64][128];
} Drawn;
static void drawn_box(void *ctx, int x, int y, int w, int h, int ink)
{
    Drawn *d = ctx;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 128 && y + h <= 64);
    /* below the header, x 124 to 127 are the FX view's page indicator: nothing is drawn or cleared there */
    assert(y + h <= 10 || x + w <= 124);
    assert(ink == 0 || ink == 1);
    for (int row = y; row < y + h; ++row)
        memset(&d->pixels[row][x], ink, (size_t)w);
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
    /* below the header, x 124 to 127 are the FX view's page indicator */
    assert(y < 10 || x + half <= 124);
    /* The RATE readout is the only text inside an icon band. */
    assert(y < 10 || x + half + !ink <= 19 || y == 32 || y == 59 || (x == 58 && y == 19));
    assert(d->texts < 64);
    d->centres[d->texts] = x;
    d->tops[d->texts] = y;
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
static int saw_at(const Drawn *d, int x, int y, const char *s)
{
    for (uint32_t i = 0; i < d->texts; ++i)
        if (d->centres[i] == x && d->tops[i] == y && !strcmp(d->seen[i], s))
            return 1;
    return 0;
}
static void draw_page(const PhrasePage *p, Drawn *d, int show_values)
{
    memset(d, 0, sizeof(*d));
    PhraseCanvas c = {d, drawn_box, drawn_text};
    pu_render(p, &c, show_values);
}
static void rendering(void)
{
    PhraseUi s;
    PhrasePage p;
    init(&s);
    Drawn d, values;
    PhraseCanvas c = {&d, drawn_box, drawn_text};
    pu_page(&s, &p);
    draw_page(&p, &d, 0);
    draw_page(&p, &values, 1);
    static const char *const labels[] = {"ZONE", "RATE", "RPTS", "COLR",
                                         "HALO", "SKEW", "INPT", "XFAD"};
    static const char *const formatted[] = {"ECHO", "RATE", "48", "0", "0", "0", "LIVE", "--"};
    for (int i = 0; i < 8; ++i) {
        int x = 32 + 26 * (i % 4), y = i < 4 ? 32 : 59;
        assert(saw_at(&d, x, y, labels[i]) && saw_at(&values, x, y, formatted[i]));
        if (i != 1)
            assert(!saw_at(&d, x, y, formatted[i]) && !saw_at(&values, x, y, labels[i]));
    }
    /* Holding FUNC swaps the captions while the underlying icon pixels stay the same. */
    for (int row = 14; row <= 28; ++row) {
        assert(!memcmp(d.pixels[row], values.pixels[row], sizeof(d.pixels[row])));
        assert(!memcmp(d.pixels[row + 27], values.pixels[row + 27], sizeof(d.pixels[row])));
    }
    assert(saw_at(&d, 7, 59, "MIX") && !saw(&d, "100"));
    assert(saw_at(&values, 7, 59, "100") && !saw(&values, "MIX"));
    assert(saw_at(&d, 15, 2, "PRISM") && !saw(&d, "ECHO"));
    assert(saw_at(&d, 42, 2, "FREE") && saw_at(&values, 42, 2, "FREE"));
    assert(saw_at(&d, 76, 2, "NO TRK") && saw_at(&values, 76, 2, "NO TRK"));
    assert(saw_at(&d, 58, 19, "375 MS") && saw_at(&values, 58, 19, "375 MS"));
    assert(saw_at(&d, 111, 2, "NO LOOP") && saw_at(&values, 111, 2, "NO LOOP"));
    assert(!d.inverted && !values.inverted);
    /* Every value at both ends of its range, with every switch on, stays on the screen. */
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < PH_PARAM_COUNT; ++i)
            raw[i] = pass ? 127 : 0;
        raw[PH_P_ZONE] = pass ? PH_ZONES - 1 : 0;
        raw[PH_P_RATIO] = pass ? PH_SYNC_RATIOS - 1 : 0;
        raw[PH_P_SYNC] = raw[PH_P_FLIP] = raw[PH_P_HOLD] = pass;
        status(&s, pass ? 344064 : 48, pass ? 516096 : 72);
        pu_page(&s, &p);
        for (int mode = 0; mode <= 1; ++mode) {
            draw_page(&p, &d, mode);
            /* FLIP and HOLD are drawn inverted when they are on. */
            assert(d.inverted == (pass ? 2u : 0u));
            assert(saw_at(&d, 9, 14, "FLIP") == (int)pass);
            assert(saw_at(&d, 9, 22, "HOLD") == (int)pass);
        }
    }
    /* The header alone stays consistent with the page in both modes. */
    memset(&d, 0, sizeof(d));
    pu_render_header(&p, &c, 0);
    assert(d.texts == 4 && saw_at(&d, 15, 2, "PRISM") && saw_at(&d, 42, 2, "SYNC"));
    assert(saw_at(&d, 76, 2, "SILENT") && saw_at(&d, 111, 2, "NO LOOP"));
    memset(&d, 0, sizeof(d));
    pu_render_header(&p, &c, 1);
    assert(d.texts == 4 && saw_at(&d, 15, 2, "PRISM") && saw_at(&d, 42, 2, "SYNC"));
    assert(saw_at(&d, 76, 2, p.time) && saw_at(&d, 111, 2, "NO LOOP"));
    s.fresh = 0;
    pu_page(&s, &p);
    draw_page(&p, &d, 0);
    assert(saw_at(&d, 76, 2, "NO LINK"));
    raw[PH_P_COLR] = 0;
    pu_page(&s, &p);
    draw_page(&p, &d, 1);
    assert(saw_at(&d, 110, 32, "-64"));
}
static void rate_readout(void)
{
    PhraseUi s;
    PhrasePage p;
    Drawn d;
    init(&s);
    static const struct {
        uint32_t sync, ratio, frames;
        const char *readout;
    } cases[] = {{0, PH_SYNC_UNITY, 18000, "375 MS"},
                 {0, PH_SYNC_UNITY, 12000, "250 MS"},
                 {1, PH_SYNC_UNITY, 12000, "1/1"},
                 {1, PH_SYNC_UNITY + 3, 18000, "X1.5"}};
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        raw[PH_P_SYNC] = cases[i].sync;
        raw[PH_P_RATIO] = cases[i].ratio;
        status(&s, cases[i].frames, cases[i].frames);
        pu_page(&s, &p);
        for (int mode = 0; mode <= 1; ++mode) {
            draw_page(&p, &d, mode);
            assert(saw_at(&d, 58, 32, "RATE") && saw_at(&d, 58, 19, cases[i].readout));
            assert(saw_at(&d, 76, 2, mode && cases[i].sync ? p.time : "NO TRK"));
        }
    }
}
static void mix_meter(void)
{
    PhraseUi s;
    PhrasePage p;
    Drawn normal, values;
    init(&s);
    /* Each stock level frame begins at one of these integers. */
    static const uint8_t first[] = {0, 1, 12, 22, 33, 43, 54, 64, 75, 85, 96, 106, 117, 127};
    int height = 0;
    for (uint32_t level = 0; level <= 127; ++level) {
        if (height < 13 && level == first[height + 1])
            ++height;
        raw[PH_P_MIX] = level;
        pu_page(&s, &p);
        draw_page(&p, &normal, 0);
        draw_page(&p, &values, 1);
        for (int y = 40; y <= 56; ++y) {
            assert(!memcmp(&normal.pixels[y][3], &values.pixels[y][3], 9));
            for (int x = 3; x <= 11; ++x) {
                int end = y == 40 || y == 56;
                int frame = end ? x > 3 && x < 11 : x == 3 || x == 11;
                int fill = x >= 5 && x <= 9 && y >= 55 - height && y <= 54;
                int rest_tick = y >= 42 && y <= 54 && !(y & 1) && (x == 5 || x == 9);
                assert(normal.pixels[y][x] == (frame || fill || rest_tick));
            }
        }
    }
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
    Drawn d;
    init(&s);
    s.loop = &fake_home;
    home_xfad = home_arms = home_drops = 0;
    /* Pattern 1 plays, no loop yet. G turns INPT at any time: to the right LOOP, which stays (PRISM hears silence until a
     * loop exists, and the page says so), to the left LIVE. Its press switches nothing; H and FUNC + NO move nothing; a
     * press of H arms. */
    report(LS_TRACK, 0, 16, 0, 0, 0.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 0);
    assert(pu_turn(&s, 6, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    for (uint32_t i = 0; i < 3 * PU_LOOP_GONE; ++i)
        pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    pu_page(&s, &p);
    same(p.cells[6].value, "LOOP");
    same(p.cells[6].state, "SILENT");
    tap(&s, PU_PUSH + 6);
    assert(raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
    tap(&s, PU_PUSH + 6);
    assert(raw[PH_P_INPT] == 0);
    assert(pu_turn(&s, 7, 5, 0) == PU_OWN && home_xfad == 0);
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
    draw_page(&p, &d, 0);
    assert(saw_at(&d, 110, 59, "XFAD") && saw_at(&d, 9, 30, "ARMD") && !saw(&d, "16 ST"));
    assert(!saw(&d, "NO LOOP") && d.inverted == 1);
    draw_page(&p, &d, 1);
    assert(saw_at(&d, 110, 59, "16 ST") && saw_at(&d, 9, 30, "ARMD") && d.inverted == 1);
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
    /* G: a press switches nothing; a tick to the left is LIVE and to the right LOOP; the header names the loop only while
     * PRISM hears it. */
    tap(&s, PU_PUSH + 6);
    assert(raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
    pu_page(&s, &p);
    assert(!p.line[0]);
    same(p.zone, "ECHO");
    assert(pu_turn(&s, 6, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, 3, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    assert(pu_turn(&s, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
    /* Arming again while the loop plays keeps the crossfader and its armed state. */
    tap(&s, PU_PUSH + 7);
    assert(home_arms == 2);
    report(LS_LOOP | LS_ARMED | LS_TRACK, 1, 16, 16, 96000, 0.0f);
    pu_loop(&s);
    pu_page(&s, &p);
    same(p.cells[7].state, "ARMD");
    assert(p.cells[7].lit && p.cells[7].fader == 1);
    /* The next loop: INPT goes to LOOP again, from LIVE. */
    report(LS_LOOP | LS_HELD | LS_TRACK, 2, 16, 16, 96000, 1.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    /* FUNC + NO drops through the home; when the loop is gone INPT is LIVE, once: G may put it at LOOP again, and it stays. */
    assert(pu_key(&s, PU_FUNC, PU_PRESS) == PU_PASS);
    tap(&s, PU_NO);
    assert(pu_key(&s, PU_FUNC, PU_RELEASE) == PU_PASS && home_drops == 1);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    report(LS_TRACK, 2, 16, 0, 0, 0.0f);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 0);
    assert(pu_turn(&s, 6, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    pu_loop(&s);
    pu_loop(&s);
    assert(raw[PH_P_INPT] == 1);
    pu_page(&s, &p);
    same(p.cells[6].state, "SILENT");
    draw_page(&p, &d, 0);
    assert(saw_at(&d, 84, 59, "INPT") && saw_at(&d, 76, 2, "SILENT"));
    assert(pu_turn(&s, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
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
    /* A gap keeps the existing loop and its displayed length until the same expiry as the input route. */
    home_fresh = 0;
    for (uint32_t i = 0; i < PU_LOOP_GONE; ++i) {
        pu_loop(&late);
        pu_loop_status(&late); /* The draw callback also reads the home. */
        pu_page(&late, &p);
        assert(p.loop_ready && p.cells[7].fader == 128);
        same(p.line, "LOOP 16 ST 2.00 S");
    }
    assert(raw[PH_P_INPT] == 1);
    pu_loop(&late);
    assert(raw[PH_P_INPT] == 0);
    pu_loop_status(&late);
    pu_page(&late, &p);
    assert(!p.loop_ready && !p.line[0]);
    same(p.cells[7].value, "--");
    same(p.cells[7].state, "NO LOOP");
    /* once: G at LOOP again stays while the loop effect still does not report */
    assert(pu_turn(&late, 6, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    for (uint32_t i = 0; i < 3 * PU_LOOP_GONE; ++i)
        pu_loop(&late);
    assert(raw[PH_P_INPT] == 1);
    pu_page(&late, &p);
    same(p.cells[6].state, "SILENT");
    assert(pu_turn(&late, 6, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
    uint32_t arms = home_arms;
    tap(&late, PU_PUSH + 7);
    assert(home_arms == arms);
    /* What the loop's page shows is drawn on the screen: the header line, the crossfader's line. */
    report(LS_LOOP | LS_TRACK, 4, 16, 16, 96000, 0.5f);
    raw[PH_P_INPT] = 1;
    home_xfad = 64;
    pu_loop_status(&s);
    PhraseCanvas c = {&d, drawn_box, drawn_text};
    pu_page(&s, &p);
    draw_page(&p, &d, 0);
    assert(saw_at(&d, 76, 2, "LOOP") && !saw(&d, "LOOP 16 ST 2.00 S") && !saw(&d, "64"));
    draw_page(&p, &d, 1);
    assert(saw_at(&d, 90, 2, "LOOP 16 ST 2.00 S") && saw_at(&d, 110, 59, "64"));
    memset(&d, 0, sizeof(d));
    pu_render_header(&p, &c, 1);
    assert(d.texts == 3 && saw_at(&d, 15, 2, "PRISM") && saw_at(&d, 42, 2, "FREE"));
    assert(saw_at(&d, 90, 2, "LOOP 16 ST 2.00 S"));
}
static uint32_t trig(uint32_t track)
{
    return PU_TRIG + track - 1;
}
static void hold_g(PhraseUi *s)
{
    press(s, PU_PUSH + PU_SELECT);
    assert(pu_selecting(s));
}
static void release_g(PhraseUi *s)
{
    release(s, PU_PUSH + PU_SELECT);
    assert(!pu_selecting(s));
}
/* The LEDs of TRIG 1 to 16 as pu_led gives them, the firmware's own value being STOCK. */
static void leds(const PhraseUi *s, uint32_t stock, uint32_t want[16])
{
    for (uint32_t t = 0; t < 16; ++t)
        want[t] = pu_led(s, PU_LED_TRIG + t, stock);
}
static void tracks(void)
{
    PhraseUi s;
    PhrasePage p;
    Drawn d;
    uint32_t led[16];
    init(&s);
    /* Nothing chosen: INPT says so. A TRIG key and its LED are the firmware's. */
    pu_page(&s, &p);
    same(p.cells[6].value, "LIVE");
    same(p.cells[6].state, "NO TRK");
    assert(pu_key(&s, trig(3), PU_PRESS) == PU_PASS && pu_key(&s, trig(3), PU_RELEASE) == PU_PASS);
    leds(&s, 0x11, led);
    for (uint32_t t = 0; t < 16; ++t)
        assert(led[t] == 0x11);
    assert(raw[PH_P_TRKS] == 0 && writes == 0);
    /* G held: the TRIG LEDs show the tracks (none: all dim) and INPT the selection; no other LED changes. */
    hold_g(&s);
    pu_page(&s, &p);
    same(p.cells[6].value, "NO TRK");
    same(p.cells[6].state, "TRIGS");
    assert(p.cells[6].lit);
    leds(&s, 0x11, led);
    for (uint32_t t = 0; t < 16; ++t)
        assert(led[t] == PU_LED_DIM);
    assert(pu_led(&s, PU_LED_TRIG - 1, 0x71) == 0x71 && pu_led(&s, PU_LED_TRIG + 16, 0x01) == 0x01);
    /* TRIG 3 turns track 3 on, its LED is lit; the release is the page's too. */
    assert(pu_key(&s, trig(3), PU_PRESS) == PU_OWN);
    assert(raw[PH_P_TRKS] == 1u << 2 && pu_track_count() == 1);
    leds(&s, 0x11, led);
    assert(led[2] == PU_LED_HEARD && led[0] == PU_LED_DIM && led[3] == PU_LED_DIM);
    pu_page(&s, &p);
    same(p.cells[6].value, "1 TRK");
    /* a repeat of the held key changes nothing */
    assert(pu_key(&s, trig(3), PU_REPEAT) == PU_OWN && raw[PH_P_TRKS] == 1u << 2);
    release(&s, trig(3));
    /* again: off */
    tap(&s, trig(3));
    assert(raw[PH_P_TRKS] == 0);
    leds(&s, 0x11, led);
    assert(led[2] == PU_LED_DIM);
    /* tracks 1 and 11 */
    tap(&s, trig(1));
    tap(&s, trig(11));
    assert(raw[PH_P_TRKS] == (1u | 1u << 10));
    pu_page(&s, &p);
    same(p.cells[6].value, "2 TRK");
    /* G released: every key and LED is the firmware's again; INPT shows LIVE and the tracks under it. The release switches
     * nothing. */
    release_g(&s);
    assert(raw[PH_P_INPT] == 0);
    leds(&s, 0x11, led);
    for (uint32_t t = 0; t < 16; ++t)
        assert(led[t] == 0x11);
    assert(pu_key(&s, trig(2), PU_PRESS) == PU_PASS && pu_key(&s, trig(2), PU_RELEASE) == PU_PASS);
    assert(raw[PH_P_TRKS] == (1u | 1u << 10));
    pu_page(&s, &p);
    same(p.cells[6].value, "LIVE");
    same(p.cells[6].state, "2 TRK");
    assert(!p.cells[6].lit);
    draw_page(&p, &d, 0);
    assert(saw_at(&d, 84, 59, "INPT") && saw_at(&d, 76, 2, "LIVE") && !saw(&d, "2 TRK"));
    draw_page(&p, &d, 1);
    assert(saw_at(&d, 84, 59, "LIVE") && saw_at(&d, 76, 2, "LIVE"));
    /* A TRIG key still down when G is released: its release stays the page's, its next press is the firmware's. */
    hold_g(&s);
    assert(pu_key(&s, trig(5), PU_PRESS) == PU_OWN);
    release_g(&s);
    release(&s, trig(5));
    assert(pu_key(&s, trig(5), PU_PRESS) == PU_PASS && pu_key(&s, trig(5), PU_RELEASE) == PU_PASS);
    assert(raw[PH_P_TRKS] == (1u | 1u << 4 | 1u << 10));
    /* A TRIG key down before G: the firmware's gesture, also its release. */
    assert(pu_key(&s, trig(7), PU_PRESS) == PU_PASS);
    hold_g(&s);
    assert(pu_key(&s, trig(7), PU_REPEAT) == PU_PASS && pu_key(&s, trig(7), PU_RELEASE) == PU_PASS);
    release_g(&s);
    /* All 16, drawn on the screen while G is held. */
    hold_g(&s);
    for (uint32_t t = 1; t <= 16; ++t)
        if (!(raw[PH_P_TRKS] & (1u << (t - 1))))
            tap(&s, trig(t));
    assert(raw[PH_P_TRKS] == PH_TRACKS_ALL);
    pu_page(&s, &p);
    draw_page(&p, &d, 0);
    assert(saw_at(&d, 84, 59, "INPT") && saw_at(&d, 76, 2, "TRIGS") && !saw(&d, "16 TRK"));
    draw_page(&p, &d, 1);
    assert(saw_at(&d, 84, 59, "16 TRK") && saw_at(&d, 76, 2, "TRIGS"));
    /* The page loses the top while G is held: the selection ends, keys and LEDs are the firmware's, G's release is the
     * page's and switches nothing. */
    pu_context(&s, 0, 1);
    assert(!pu_selecting(&s) && pu_led(&s, PU_LED_TRIG, 0x11) == 0x11);
    assert(pu_key(&s, trig(1), PU_PRESS) == PU_PASS && pu_key(&s, trig(1), PU_RELEASE) == PU_PASS);
    release(&s, PU_PUSH + PU_SELECT);
    pu_context(&s, 1, 1);
    assert(!pu_selecting(&s) && raw[PH_P_TRKS] == PH_TRACKS_ALL);
    pu_page(&s, &p);
    same(p.cells[6].state, "16 TRK");

    /* G's press switches nothing. While a loop is held, track edits prepare LIVE without switching away from the loop. */
    s.loop = &fake_home;
    home_xfad = LP_MAX;
    report(LS_LOOP | LS_TRACK, 1, 16, 16, 96000, 1.0f);
    pu_loop_status(&s);
    raw[PH_P_INPT] = 0;
    uint32_t before = writes;
    tap(&s, PU_PUSH + PU_SELECT);
    hold_g(&s);
    release_g(&s);
    assert(raw[PH_P_INPT] == 0 && writes == before);
    raw[PH_P_INPT] = 1;
    tap(&s, PU_PUSH + PU_SELECT);
    assert(raw[PH_P_INPT] == 1);
    hold_g(&s);
    tap(&s, trig(16));
    release_g(&s);
    assert(raw[PH_P_INPT] == 1 && raw[PH_P_TRKS] == 0x7fff);
    /* The same applies through a brief loss of Overloop status. */
    pu_loop(&s);
    home_fresh = 0;
    pu_loop(&s);
    hold_g(&s);
    tap(&s, trig(16));
    release_g(&s);
    assert(raw[PH_P_INPT] == 1 && raw[PH_P_TRKS] == PH_TRACKS_ALL);
    /* A turn of G while it is held still switches, and release keeps that choice. */
    hold_g(&s);
    assert(pu_turn(&s, PU_SELECT, -1, 0) == PU_OWN && raw[PH_P_INPT] == 0);
    release_g(&s);
    assert(raw[PH_P_INPT] == 0);

    /* In silent LOOP with no capture, choosing an actual track brings back the LIVE track grid. */
    report(LS_TRACK, 1, 16, 0, 0, 0.0f);
    pu_loop(&s);
    raw[PH_P_TRKS] = 0;
    assert(pu_turn(&s, PU_SELECT, 1, 0) == PU_OWN && raw[PH_P_INPT] == 1);
    hold_g(&s);
    release_g(&s);
    assert(raw[PH_P_INPT] == 1);
    hold_g(&s);
    tap(&s, trig(16));
    release_g(&s);
    assert(raw[PH_P_INPT] == 0 && raw[PH_P_TRKS] == (1u << 15));
    pu_page(&s, &p);
    same(p.cells[6].value, "LIVE");
    same(p.cells[6].state, "1 TRK");
    draw_page(&p, &d, 0);
    assert(!p.loop_input && saw_at(&d, 76, 2, "LIVE") && d.pixels[51][95]);
}

/* Compare only the icon rows. Labels, numeric values and state text cannot satisfy this test. */
static void icon_frame(const PhraseUi *s, Drawn *d)
{
    PhrasePage p;
    pu_page(s, &p);
    draw_page(&p, d, 0);
}
static void icon_changes(const PhraseUi *s, Drawn *previous, int changed_cell)
{
    Drawn current;
    icon_frame(s, &current);
    for (int cell = 0; cell < 8; ++cell) {
        int x = 19 + 26 * (cell % 4), y = cell < 4 ? 14 : 41;
        int changed = 0;
        for (int row = y; row < y + 15; ++row)
            changed |= memcmp(&previous->pixels[row][x], &current.pixels[row][x], 26) != 0;
        if (changed != (cell == changed_cell)) {
            fprintf(stderr, "icon for %c %s while changing %c\n", 'A' + cell,
                    changed ? "changed" : "stayed the same", 'A' + changed_cell);
            assert(0);
        }
    }
    *previous = current;
}
static void dynamic_icons(void)
{
    PhraseUi s;
    Drawn previous;
    init(&s);
    icon_frame(&s, &previous);

    raw[PH_P_ZONE] = 0;
    icon_changes(&s, &previous, 0);
    raw[PH_P_ZONE] = PH_ZONES - 1;
    icon_changes(&s, &previous, 0);
    raw[PH_P_RPTS] = 0;
    icon_changes(&s, &previous, 2);
    raw[PH_P_RPTS] = PH_RPTS_UNITY - 1;
    icon_changes(&s, &previous, 2);
    raw[PH_P_HOLD] = 1;
    icon_changes(&s, &previous, 2);
    raw[PH_P_COLR] = 0;
    icon_changes(&s, &previous, 3);
    raw[PH_P_COLR] = 127;
    icon_changes(&s, &previous, 3);
    raw[PH_P_HALO] = 127;
    icon_changes(&s, &previous, 4);
    raw[PH_P_SKEW] = 0;
    icon_changes(&s, &previous, 5);
    raw[PH_P_SKEW] = 127;
    icon_changes(&s, &previous, 5);

    hold_g(&s);
    icon_frame(&s, &previous);
    tap(&s, trig(1));
    icon_changes(&s, &previous, 6);
    tap(&s, trig(1));
    tap(&s, trig(16));
    icon_changes(&s, &previous, 6); /* Same number of tracks, different routing. */
    release_g(&s);
    icon_frame(&s, &previous);
    raw[PH_P_INPT] = 1;
    icon_changes(&s, &previous, 6); /* The open loop glyph precedes a capture. */
    raw[PH_P_INPT] = 0;
    icon_changes(&s, &previous, 6);

    s.loop = &fake_home;
    home_xfad = 64;
    report(LS_LOOP | LS_TRACK, 1, 16, 16, 96000, 0.0f);
    pu_loop_status(&s);
    icon_changes(&s, &previous, 7);
    raw[PH_P_INPT] = 1;
    icon_changes(&s, &previous, 6);
    /* H follows the applied crossfade even while a new loop is armed and the target stays fixed. */
    report(LS_LOOP | LS_TRACK | LS_ARMED, 1, 16, 16, 96000, 0.5f);
    pu_loop_status(&s);
    icon_changes(&s, &previous, 7);
    report(LS_LOOP | LS_TRACK | LS_ARMED, 1, 16, 16, 96000, 1.0f);
    pu_loop_status(&s);
    icon_changes(&s, &previous, 7);
}
static void repeat_icon(void)
{
    PhraseUi s;
    Drawn d;
    init(&s);
    static const uint32_t levels[] = {0, PH_RPTS_UNITY - 1};
    for (int i = 0; i < 2; ++i) {
        raw[PH_P_RPTS] = levels[i];
        icon_frame(&s, &d);
        int first = -1, last = 0, dashes = 0, width = 0;
        for (int x = 71; x <= 97; ++x) {
            int ink = x < 97 && d.pixels[20][x];
            if (ink) {
                if (first < 0)
                    first = x;
                last = x;
                ++width;
            } else if (width) {
                assert(width == 3);
                width = 0;
                ++dashes;
            }
            if (x < 97)
                for (int y = 14; y <= 28; ++y)
                    assert(d.pixels[y][x] == (ink && (y == 20 || y == 21)));
        }
        assert(first + last == 168 && dashes == (i ? 5 : 1));
    }
    static const uint16_t infinity[] = {0x306, 0x489, 0x451, 0x421, 0x451, 0x489, 0x306};
    static const uint32_t sustained[] = {PH_RPTS_UNITY, 127, 0};
    for (int i = 0; i < 3; ++i) {
        raw[PH_P_RPTS] = sustained[i];
        raw[PH_P_HOLD] = i == 2;
        icon_frame(&s, &d);
        for (int y = 14; y <= 28; ++y)
            for (int x = 71; x <= 96; ++x) {
                int on = x >= 79 && x <= 89 && y >= 18 && y <= 24 &&
                         (infinity[y - 18] & (1u << (x - 79)));
                assert(d.pixels[y][x] == on);
            }
    }
}
static void colour_icon(void)
{
    PhraseUi s;
    Drawn d;
    init(&s);
    static const uint32_t levels[] = {0, 63, 64, 127};
    static const int radii[] = {1, 4, 4, 7};
    for (int i = 0; i < 4; ++i) {
        raw[PH_P_COLR] = levels[i];
        icon_frame(&s, &d);
        int radius = radii[i];
        for (int y = 14; y <= 28; ++y)
            for (int x = 97; x <= 122; ++x) {
                int dx = x > 110 ? x - 110 : 110 - x, dy = y > 21 ? y - 21 : 21 - y;
                int on = dx <= radius && dy <= radius &&
                         (levels[i] < 64 || dx == radius || dy == radius);
                assert(d.pixels[y][x] == on);
            }
    }
}
static void halo_icon(void)
{
    PhraseUi s;
    Drawn d;
    init(&s);
    static const uint8_t ring[] = {0x1c, 0x22, 0x41, 0x41, 0x41, 0x22, 0x1c};
    static const struct {
        uint32_t value;
        int inner, outer, rise;
    } positions[] = {{0, 0, 0, 0},  {7, 0, 0, 0},  {8, 0, 1, 0},  {16, 1, 1, 0},
                     {32, 1, 2, 1}, {64, 2, 4, 1}, {96, 3, 6, 2}, {127, 4, 8, 2}};
    for (unsigned step = 0; step < sizeof(positions) / sizeof(positions[0]); ++step) {
        int inner = positions[step].inner, outer = positions[step].outer,
            rise = positions[step].rise;
        const int offsets[][2] = {
            {0, 0}, {-inner, -rise}, {inner, rise}, {-outer, rise}, {outer, -rise}};
        raw[PH_P_HALO] = positions[step].value;
        icon_frame(&s, &d);
        for (int y = 41; y <= 55; ++y)
            for (int x = 19; x <= 44; ++x) {
                int on = 0;
                for (int i = 0; i < 5; ++i) {
                    int dx = x - 29 - offsets[i][0], dy = y - 45 - offsets[i][1];
                    if (dx >= 0 && dx < 7 && dy >= 0 && dy < 7)
                        on |= (ring[dy] >> dx) & 1;
                }
                assert(d.pixels[y][x] == on);
            }
    }
}
int main(void)
{
    encoders();
    presses();
    sync();
    formats();
    page_cells();
    rendering();
    rate_readout();
    mix_meter();
    loop_page();
    tracks();
    dynamic_icons();
    repeat_icon();
    colour_icon();
    halo_icon();
    puts("phrase ui: ok");
    return 0;
}
