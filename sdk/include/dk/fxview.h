/* The FX view's native pages, DTII first. core/fxview owns the table slots of the firmware's FX view (draw, key, encoder)
 * and of the help view's draw, and hands every call to the image adapters that paint a page of their own. No firmware
 * address is named here.
 *
 * A page adapter defines the three functions of its prefix (core/phrase-ui: phrase_, core/loop-display: loop_); core/fxview
 * calls the ones the image has, after the stock draw and before the stock key and encoder handlers:
 *
 *   int <p>_fxv_draw(uint8_t *view, uint8_t *bitmap, int header_only);   1: it painted (header_only: only its header,
 *                                                                          after the firmware's help view has drawn)
 *   int <p>_fxv_key(uint8_t *view, uint8_t *event);                       1: it took the key (the stock handler is not called)
 *   int <p>_fxv_turn(uint8_t *view, uint8_t *event);                      1: it took the turn
 *
 * An adapter acts only while the page the view shows is its own (fxv_page) and the view is the visible FX view
 * (fxv_visible_view), so every other FX page stays the firmware's.
 */
#ifndef DK_FXVIEW_H
#define DK_FXVIEW_H
#include <stdint.h>

/* Keys and encoders as the view's events carry them: event word 12 is the key or the encoder (1 to 8: A to H, 9: LEVEL),
 * word 16 the key's flags or the encoder's ticks. */
enum { FXV_NO = 12, FXV_FUNC = 17, FXV_PUSH = 41 };
enum { FXV_PRESS = 1, FXV_CHORD = 2, FXV_REPEAT = 8, FXV_RELEASE = 16 };

/* The FX view that is on the screen and takes input, or 0 (another view covers it, or the FX view is not open). */
uint8_t *fxv_visible_view(void);
/* The native page code the view shows. */
uint32_t fxv_page(uint8_t *view);
/* What a key event's flags mean: FXV_PRESS, FXV_REPEAT, FXV_CHORD or FXV_RELEASE. */
uint32_t fxv_key_flags(uint32_t native_flags);
/* Drawing on the view's bitmap, 128 x 64 with the origin at the top left. ink 1 sets, 0 clears; a text is centred at
 * `centre` in the label font (5 pixels high, 5 a character), and with ink 0 it is drawn inverted in a filled box. Both have
 * the shape of a canvas callback: the first argument is the bitmap. */
void fxv_box(void *bitmap, int x, int y, int width, int height, int ink);
void fxv_text(void *bitmap, int centre, int top, const char *text, int ink);
#endif
