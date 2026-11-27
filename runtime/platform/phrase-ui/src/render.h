#ifndef PHRASE_UI_RENDER_H
#define PHRASE_UI_RENDER_H
#include "model.h"
/* The page as text: what each cell says. pu_page fills it from the provider's values and the engine's status;
 * pu_render draws it. A cell is 26 pixels wide, so a text has at most 7 characters. */
typedef struct {
    char label[8], value[8], state[8];
    uint32_t lit;   /* the state is on (FLIP, HOLD, ARMD): drawn inverted */
    uint32_t fader; /* 0: none; else 1 + the crossfader as it sounds, 0 .. 127 */
} PhraseCell;
typedef struct {
    /* The effect's name, its zone and the time the engine plays: diagnostics only, the stock header names the page. */
    char title[8], zone[8], time[8];
    /* While PRISM hears the loop: "LOOP 16 ST 2.00 S" (diagnostics; Overloop's page shows the loop). */
    char line[24];
    uint32_t mix, zone_index;
    PhraseCell cells[8]; /* A to H */
    /* Icon inputs follow the existing page fields read by device diagnostics. */
    uint8_t sync, repeats, colour, halo, skew, loop_input, loop_ready, armed, link_error;
    uint16_t tracks;
} PhrasePage;
typedef struct {
    void *context;
    void (*box)(void *, int x, int y, int width, int height, int ink);
    void (*text)(void *, int centre, int top, const char *, int ink);
} PhraseCanvas;
void pu_page(const PhraseUi *, PhrasePage *);
/* The centre of cell A and the width of a cell: the stock pages' (33, 59, 85, 111). */
enum { PU_CELL_X = 33, PU_CELL_W = 26 };
/* The page's body and its level strip; the header, the effect's tab and the page indicator are the stock FX view's.
 * SHOW_VALUES: FUNC + FX is held; FOCUS bit i: cell i shows its value for a moment after a turn (the view's countdown). */
void pu_render(const PhrasePage *, const PhraseCanvas *, int show_values, uint32_t focus);
#endif
