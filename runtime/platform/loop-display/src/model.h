#ifndef LOOP_DISPLAY_MODEL_H
#define LOOP_DISPLAY_MODEL_H
#include <stdint.h>
#include <dk/loop.h>
/* The loop effect's own page (Overloop): what a key and a turn do, and what the page says. No firmware is named here:
 * native.c hands in the native events, and the home (core/loop-control) holds the values and the engine's status.
 *
 *   E            ARM: the next pattern change keeps the last cycle. A press arms, or takes the arm back; a turn to the
 *                right arms, to the left disarms
 *   H or LEVEL   XFAD, the crossfader: 0 LIVE .. 127 LOOP; a press of H is ARM too, as on PRISM's page
 *   FUNC + NO    drop the loop
 *   A to D, F, G taken, so that the stock page under this one does nothing with them; they do nothing
 *
 * The page keeps the stock FX view's header (with its title bubble), the effect's tab and the page indicator. Its body is
 * laid out as the stock pages: the timeline across the top in the stock waveform style (no frame, dotted end markers), the
 * line that says what the loop does under it, and the bottom row's cells at the stock centres: ARM's graphic and caption
 * in E, the crossfader's in H, the stock placeholder in F and G. A caption shows its value after a turn (the view's own
 * focus countdown) and while FUNC + FX is held.
 */
enum { LD_PASS = 0, LD_OWN = 1 };
/* Keys and flags as <dk/fxview.h> names them. */
enum { LD_NO = 12, LD_PUSH = 41, LD_PRESS = 1, LD_CHORD = 2, LD_REPEAT = 8, LD_RELEASE = 16 };
/* The encoders: 0 to 7 are A to H, 8 is LEVEL. */
enum { LD_E = 4, LD_H = 7, LD_LEVEL = 8 };
/* The centres of the bottom row's cells E to H on the screen: 33 + 26 a cell, as the stock pages have them. */
enum { LD_CELL_E = 33, LD_CELL_F = 59, LD_CELL_G = 85, LD_CELL_H = 111 };
/* The timeline, in the stock waveform style: its end markers at x LD_WAVE_LEFT and LD_WAVE_RIGHT (the stock waveform's),
 * the engine's LS_WAVE_COLUMNS columns merged into the columns between them, rows LD_WAVE_TOP to LD_WAVE_BOTTOM about
 * the centre line LD_WAVE_MIDDLE; the line that says what the loop does centred under it at LD_LINE_Y. */
enum {
    LD_WAVE_LEFT = 25,
    LD_WAVE_RIGHT = 121,
    LD_WAVE_PLOT = LD_WAVE_RIGHT - LD_WAVE_LEFT - 1,
    LD_WAVE_TOP = 12,
    LD_WAVE_MIDDLE = 22,
    LD_WAVE_BOTTOM = 32,
    LD_LINE_Y = 34,
};
/* Blocks of the engine's heartbeat that ARM as the page asked for it counts while the status does not show it yet (the
 * value goes to the engine once a UI pass and comes back in its status): 0.5 s. */
enum { LD_ARM_SETTLE = 750 };
typedef struct {
    uint32_t active, revision;
    uint32_t owned[2];
    /* ARM as the page asked for it while the status does not show it yet: 0 none, else 1 + the state asked for; the
     * engine's heartbeat at the ask */
    uint32_t arm_asked, arm_beat;
} LoopUi;
/* FUNC: FUNC is down. */
uint32_t ld_key(LoopUi *, uint32_t key, uint32_t flags, uint32_t func);
uint32_t ld_turn(LoopUi *, uint32_t encoder, int32_t delta);
/* The engine's status (STATUS, FRESH as dkrt_loop_status gives them), each time the page reads it: ARM as the page asked for
 * it is done with once the status shows it, the status is not fresh or LD_ARM_SETTLE blocks have passed. */
void ld_observe(LoopUi *, const uint32_t *status, uint32_t fresh);

/* The page as text. A line has at most 24 characters. */
typedef struct {
    /* the effect's name and its state (diagnostics: the stock header names the page); ARM's state ("OFF", "ON") and XFAD's
     * value ("LIVE", "64", "LOOP"): what the captions of E and H show as their values */
    char title[12], state[12], line1[28], line2[28], arm[12], xfad[12];
    /* ARM is on (drawn inverted); a loop is held */
    uint32_t armed, loop;
    /* where the timeline's mark stands, 0 .. 1000, or LD_NO_MARK: the loop's phase with a loop, else the step of the
     * pattern that plays */
    uint32_t mark;
    /* the crossfader as the engine applies it, 0 .. 127 */
    uint32_t fader;
    /* the engine does not report: the page says NO LINK */
    uint32_t link_error;
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
/* The page's body (x 16 to 123 below the header) and the empty place of the level strip under the tab (Overloop has no
 * level: LEVEL turns XFAD). SHOW_VALUES: FUNC + FX is held; FOCUS bit i: cell i (LD_E, LD_H) shows its value for a moment
 * after a turn. */
void ld_render(const LoopPage *, const LoopCanvas *, int show_values, uint32_t focus);
#endif
