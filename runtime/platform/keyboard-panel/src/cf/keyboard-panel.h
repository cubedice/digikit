/* core/keyboard-panel (docs/plans/fork-image-diet.md section 9): what panel.c needs besides the firmware. It calls nothing of CANOPY's
 * host: the track keyboard plays and records CANOPY's tracks. */
#ifndef DK_KEYBOARD_PANEL_H
#define DK_KEYBOARD_PANEL_H
#include <stdint.h>
#include "src-clock.h"
#define IP_NONE 0xffffffffu
/* an optional function of another adapter (weak): present when its address is not 0 */
#define IP_HAVE(f) ((f) != 0)
#endif
