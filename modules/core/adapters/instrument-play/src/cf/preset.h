/* CANOPY explicit sound presets. These identities are independent of encoders,
 * firmware slots and CPS transport ordering. No performance or route state. */
#ifndef DK_CANOPY_PRESET_H
#define DK_CANOPY_PRESET_H
#include "keyboard.h"
enum {
    CP_CHORD=0, CP_VOICING=1, CP_COLOR=2, CP_DRIFT=3,
    CP_STRUM=4, CP_ARP_MIX=5, CP_RATE=6, CP_ORDER=7,
    CP_OCTAVES=8, CP_GATE=9, CP_SWING=10, CP_RESTART=11,
    CP_LATCH=12, CP_SOURCE=13, CP_ATTACK=14, CP_RELEASE=15,
    CP_CONTROLS=16, CP_RECORD_BYTES=64, CP_SLOTS=4
};
typedef struct {
    uint16_t control[CP_CONTROLS];
    uint16_t bpm;
    uint8_t family, mode, clock;
    KbShape shape;
} CpPreset;
/* SOURCE=keyboard only until the sequencer event owner is implemented. Swing
 * retains CPS5's extra-delay percentage, not an incorrectly labelled 50..75%. */
int cp_valid(const CpPreset *p);
int cp_encode(const CpPreset *p, uint8_t out[CP_RECORD_BYTES]);
/* Validate everything, including canonical padding, before touching *out. */
int cp_decode(CpPreset *out, const uint8_t *bytes, uint32_t length);
#endif
