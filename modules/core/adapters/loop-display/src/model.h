#ifndef LOOP_DISPLAY_MODEL_H
#define LOOP_DISPLAY_MODEL_H
#include <stdint.h>
#include <dk/loop.h>
/* The loop effect's own page (Overloop): what a key and a turn do, and what the page says. No firmware is named here:
 * native.c hands in the native events, and the home (core/loop-control) holds the values and the engine's status.
 *
 *   H or LEVEL   XFAD, the crossfader: 0 LIVE .. 127 LOOP
 *   press H      ARM: the next pattern change keeps the last cycle; a second press takes the arm back
 *   FUNC + NO    drop the loop
 *   A to G       taken, so that the stock page under this one does nothing with them; they do nothing
 */
enum { LD_PASS = 0, LD_OWN = 1 };
/* Keys and flags as <dk/fxview.h> names them. */
enum { LD_NO = 12, LD_PUSH = 41, LD_PRESS = 1, LD_CHORD = 2, LD_REPEAT = 8, LD_RELEASE = 16 };
/* The encoders: 0 to 7 are A to H, 8 is LEVEL. */
enum { LD_H = 7, LD_LEVEL = 8 };
typedef struct {
    uint32_t active, revision;
    uint32_t owned[2];
} LoopUi;
/* FUNC: FUNC is down. */
uint32_t ld_key(LoopUi *, uint32_t key, uint32_t flags, uint32_t func);
uint32_t ld_turn(LoopUi *, uint32_t encoder, int32_t delta);

/* The page as text. A line has at most 24 characters. */
typedef struct {
    char title[12], state[12], line1[28], line2[28], arm[12], xfad[12];
    /* ARM is on (drawn inverted); a loop is held */
    uint32_t armed, loop;
    /* where the timeline's mark stands, 0 .. 1000, or LD_NO_MARK: the loop's phase with a loop, else the step of the
     * pattern that plays */
    uint32_t mark;
    /* the crossfader as the engine applies it, 0 .. 127 */
    uint32_t fader;
} LoopPage;
enum { LD_NO_MARK = 0xffffffffu };
/* STATUS: the engine's LS_WORDS words; FRESH: they are fresh; XFAD: the home's crossfader. */
void ld_page(const uint32_t *status, uint32_t fresh, uint32_t xfad, LoopPage *);
/* A time in frames as text: "375 MS" below a second, "1.25 S" below ten, "21.5 S" from there. */
void ld_time(char out[16], uint32_t frames);

typedef struct {
    void *context;
    void (*box)(void *, int x, int y, int width, int height, int ink);
    void (*text)(void *, int centre, int top, const char *, int ink);
} LoopCanvas;
void ld_render(const LoopPage *, const LoopCanvas *);
/* Header-only repaint after native passive overlays. */
void ld_render_header(const LoopPage *, const LoopCanvas *);
#endif
