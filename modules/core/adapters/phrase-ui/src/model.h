#ifndef PHRASE_UI_MODEL_H
#define PHRASE_UI_MODEL_H
#include <stdint.h>
#include <dk/phrase.h>
/* The one page of the phrase effect (PRISM): what a key and a turn do. No firmware is named here: native.c hands in the
 * native events and the provider (core/phrase-control) holds the values.
 *
 *   LEVEL  MIX            A  ZONE  press: FLIP        B  RATE  press: SYNC on/off, FUNC + turn: fine
 *                         C  RPTS  press: HOLD        D  COLR
 *                         E  HALO                     F  SKEW
 *                         G  INPT  (kept for the loop: nothing yet)       H  XFAD  (the same)
 */
enum { PU_PASS = 0, PU_OWN = 1 };
enum {
    PU_FX = 5,
    PU_NO = 12,
    PU_FUNC = 17,
    PU_PUSH = 41, /* the presses of encoders A to H: 41 to 48 */
    PU_PRESS = 1,
    PU_CHORD = 2,
    PU_REPEAT = 8,
    PU_RELEASE = 16
};
enum { PU_LEVEL = 8 }; /* the encoders: 0 to 7 are A to H */
typedef struct {
    uint32_t active, generation, revision;
    uint32_t held[2], owned[2], cancelled[2];
    int32_t detent[9];
    uint32_t fresh, status[PH_STATUS_WORDS];
    const char *title;
} PhraseUi;
void pu_context(PhraseUi *, uint32_t active, uint32_t generation);
void pu_status(PhraseUi *, const uint32_t *, uint32_t fresh);
uint32_t pu_key(PhraseUi *, uint32_t key, uint32_t flags);
uint32_t pu_native_flags(uint32_t flags);
void pu_reconcile(PhraseUi *, const uint32_t physical[2]);
/* FINE: FUNC is held. */
uint32_t pu_turn(PhraseUi *, uint32_t encoder, int32_t delta, uint32_t fine);
void pu_leave(PhraseUi *);
/* The parameter an encoder turns now (RATE's encoder turns NOTE with SYNC), or PH_PARAM_COUNT for none. */
uint32_t pu_parameter(uint32_t encoder);
/* The text of a value, at most 7 characters: a zone's name, a note length, COLR's distance from the centre, SKEW's
 * distance with the longer side ("L 12", "R 30"), the number itself for the rest. */
void pu_format(char out[16], uint32_t parameter, uint32_t raw);
/* A time in frames as text: "375 MS" below a second, "1.25 S" below ten, "10.7 S" from there. */
void pu_time(char out[16], uint32_t frames);
/* The time the engine reports, in frames: the two sides' geometric mean (SKEW makes one shorter and one longer by the
 * same ratio), 0 without a status. */
uint32_t pu_frames(const PhraseUi *);
const char *pu_zone_name(uint32_t zone);
#endif
