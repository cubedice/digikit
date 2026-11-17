#ifndef PHRASE_UI_MODEL_H
#define PHRASE_UI_MODEL_H
#include <stdint.h>
#include <dk/phrase.h>
enum { PU_PASS = 0, PU_OWN = 1, PU_NEXT_FX = 2 };
enum {
    PU_FX = 5,
    PU_YES = 10,
    PU_UP = 11,
    PU_NO = 12,
    PU_DOWN = 14,
    PU_FUNC = 17,
    PU_TRIG = 25,
    PU_H = 48,
    PU_PRESS = 1,
    PU_CHORD = 2,
    PU_REPEAT = 8,
    PU_RELEASE = 16
};
typedef struct {
    uint32_t page, overlay, active, generation, revision;
    uint32_t held[2], owned[2], cancelled[2], adjusted, serial, order[16];
    int32_t detent[9];
    uint32_t focus, focus_ticks, fresh, show_values, status[PH_STATUS_WORDS];
    const char *title;
} PhraseUi;
void pu_context(PhraseUi *, uint32_t active, uint32_t generation);
void pu_status(PhraseUi *, const uint32_t *, uint32_t fresh);
uint32_t pu_key(PhraseUi *, uint32_t key, uint32_t flags);
uint32_t pu_native_flags(uint32_t flags);
void pu_reconcile(PhraseUi *, const uint32_t physical[2]);
uint32_t pu_turn(PhraseUi *, uint32_t encoder, int32_t delta);
void pu_leave(PhraseUi *);
int32_t pu_gain_track(const PhraseUi *);
uint32_t pu_parameter(uint32_t page, uint32_t encoder);
uint32_t pu_effective_mask(void);
uint32_t pu_led(const PhraseUi *, uint32_t logical, uint32_t stock);
void pu_format(char out[16], uint32_t parameter, uint32_t raw);
#endif
