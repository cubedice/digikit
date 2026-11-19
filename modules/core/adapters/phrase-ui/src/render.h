#ifndef PHRASE_UI_RENDER_H
#define PHRASE_UI_RENDER_H
#include "model.h"
/* The page as text: what each cell says. pu_page fills it from the provider's values and the engine's status;
 * pu_render draws it. A cell is 26 pixels wide, so a text has at most 7 characters. */
typedef struct {
    char label[8], value[8], state[8];
    uint32_t lit; /* the state is on (FLIP, HOLD, ARMD): drawn inverted */
    uint32_t
        fader; /* 0: none; else 1 + the crossfader as it sounds, 0 .. 127: drawn in place of the state */
} PhraseCell;
typedef struct {
    char title[8], zone[8], time[8];
    /* While PRISM hears the loop: "LOOP 16 ST 2.00 S" in place of the zone and the time. */
    char line[24];
    uint32_t mix, zone_index;
    PhraseCell cells[8]; /* A to H */
} PhrasePage;
typedef struct {
    void *context;
    void (*box)(void *, int x, int y, int width, int height, int ink);
    void (*text)(void *, int centre, int top, const char *, int ink);
} PhraseCanvas;
void pu_page(const PhraseUi *, PhrasePage *);
void pu_render(const PhrasePage *, const PhraseCanvas *);
/* Header-only repaint after native passive overlays; never touches controls. */
void pu_render_header(const PhrasePage *, const PhraseCanvas *);
#endif
