#ifndef PHRASE_UI_RENDER_H
#define PHRASE_UI_RENDER_H
#include "model.h"
typedef struct {
    void *context;
    void (*box)(void *, int x, int y, int width, int height, int ink);
    void (*text)(void *, int centre, int top, const char *, int ink);
    void (*knob)(void *, uint32_t cell, uint32_t normalized);
} PhraseCanvas;
void pu_render(const PhraseUi *, const PhraseCanvas *);
/* Header-only repaint after native passive overlays; never touches controls. */
void pu_render_header(const PhraseUi *, const PhraseCanvas *);
#endif
