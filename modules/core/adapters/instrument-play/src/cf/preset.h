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
    CP_WT_TABLE=16, CP_WT_POS=17, CP_WT_DEPTH=18, CP_WT_RATE=19,
    CP_WT_SHAPE=20, CP_WT_RESET=21, CP_WT_CLOCK=22, CP_WT_ENV=23,
    CP_WT_ATTACK=24, CP_WT_RELEASE=25, CP_WT_VELOCITY=26, CP_WT_WHEEL=27,
    CP_CONTROLS=28, CP_RECORD_BYTES=96, CP_LEGACY_BYTES=64, CP_SLOTS=4,
    CP_TABLE_VERSION=1
};
typedef struct {
    uint16_t control[CP_CONTROLS];
    uint16_t bpm;
    uint8_t family, mode, clock;
    KbShape shape;
} CpPreset;
/* OCTAVES stores signed spread with a +3 bias: 0..6 means -3..+3 additional
 * octaves in one direction. V1 stored total upward octaves (1..3).
 * SOURCE=keyboard only until the sequencer event owner is implemented. Swing
 * retains CPS5's extra-delay percentage, not an incorrectly labelled 50..75%. */
int cp_valid(const CpPreset *p);
/* Initialize the extension without changing any legacy sound parameter. */
void cp_wavetable_defaults(uint16_t controls[CP_CONTROLS]);
int cp_encode(const CpPreset *p, uint8_t out[CP_RECORD_BYTES]);
/* Validate everything, including canonical padding, before touching *out. */
int cp_decode(CpPreset *out, const uint8_t *bytes, uint32_t length);
#endif
