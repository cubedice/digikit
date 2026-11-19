#ifndef PHRASE_UI_MODEL_H
#define PHRASE_UI_MODEL_H
#include <stdint.h>
#include <dk/phrase.h>
#include <dk/loop.h>
/* The one page of the phrase effect (PRISM): what a key and a turn do. No firmware is named here: native.c hands in the
 * native events and the provider (core/phrase-control) holds the values.
 *
 *   LEVEL  MIX            A  ZONE  press: FLIP        B  RATE  press: SYNC on/off, FUNC + turn: fine
 *                         C  RPTS  press: HOLD        D  COLR
 *                         E  HALO                     F  SKEW
 *                         G  INPT  press or turn: LIVE / LOOP, only while there is a loop
 *                         H  XFAD  Overloop's crossfader (while there is a loop)   press: ARM   FUNC + NO: drop the loop
 *
 * INPT is the provider's (not saved); XFAD, ARM and the drop are the loop effect's, through its home (core/loop-control).
 * pu_loop, once a UI pass, puts INPT at LOOP when a new loop is taken and at LIVE when there is no loop.
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
/* The loop effect's home as the page reaches it (<dk/loop.h>): native.c fills it when the image has core/loop-control.
 * Without it (PhraseUi.loop 0) the page has no loop: INPT stays LIVE and XFAD shows --. */
typedef struct {
    uint32_t (*get)(uint32_t index);
    void (*xfad)(uint32_t value);
    void (*arm)(void);
    void (*drop)(void);
    uint32_t (*status)(uint32_t *out);
} PhraseLoop;
/* UI passes with no fresh status from the loop effect (after the home's own 60) before INPT goes back to LIVE. */
enum { PU_LOOP_GONE = 60 };
typedef struct {
    uint32_t active, generation, revision;
    uint32_t held[2], owned[2], cancelled[2];
    int32_t detent[9];
    uint32_t fresh, status[PH_STATUS_WORDS];
    const char *title;
    const PhraseLoop *loop;
    /* the loop effect's status as last read, whether it was fresh, the loop count last seen (once one was), stale passes */
    uint32_t loop_status[LS_WORDS], loop_fresh, loop_seen, loop_known, loop_stale;
} PhraseUi;
void pu_context(PhraseUi *, uint32_t active, uint32_t generation);
void pu_status(PhraseUi *, const uint32_t *, uint32_t fresh);
/* Read the loop effect's status (no rule applied). */
void pu_loop_status(PhraseUi *);
/* Once a UI pass: read the status and keep INPT with the loop (LOOP at a new loop, LIVE without one). */
void pu_loop(PhraseUi *);
/* There is a loop: the loop effect reports, and it holds one. */
uint32_t pu_has_loop(const PhraseUi *);
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
