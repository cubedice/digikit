/* core/keyboard-panel (docs/plans/fork-image-diet.md section 9): what panel.c needs besides the firmware. The kbp_ functions are the
 * forwarders of seam.s into CANOPY's host (services/instrument.dks); while it is not loaded each answers as for a track it does not own. */
#ifndef DK_KEYBOARD_PANEL_H
#define DK_KEYBOARD_PANEL_H
#include <stdint.h>
#include "src-clock.h"
#define IP_NONE 0xffffffffu
/* an optional function of another adapter (weak): present when its address is not 0 */
#define IP_HAVE(f) ((f) != 0)
int kbp_track_owned(uint32_t track);
int kbp_track_keyboard_routed(uint32_t track);
int kbp_track_plays_keys(uint32_t track);
int kbp_record_chord(const uint32_t steps[4], const int8_t *intervals, uint32_t count);
void kbp_select_track(uint32_t track);
int kbp_panel_key(uint32_t key, uint32_t note, uint32_t velocity, int down, uint32_t track);
void kbp_panel_release(void);
void kbp_seq_mute(uint32_t track, int on);
#endif
