#ifndef DK_CANOPY_SRC_DISPLAY_H
#define DK_CANOPY_SRC_DISPLAY_H
#include "src-state.h"
#include "status.h"
typedef struct {
    char label[8][8], value[8][6], heading[22], mode[6], notes[4][6];
    uint8_t pending, membership[4], highlight, latch, pedal, mix_bar, mix_requested, mix_applied;
} CvDisplay;
/* Snapshot in, bounded presentation out. Native values are already formatted
 * by their firmware owners; this code never invents a second TUNE/LEVEL scale. */
void cv_build(CvDisplay *,const CsState *,const IsStatus *,uint32_t now_ms,
    const char *native_tune,const char *native_level,int save_error);
/* Four-byte output including terminator, or five for C#-1. MIDI zero is valid. */
void cv_note(char out[6],uint32_t note,int valid);
/* 128x64, row-major, MSB first. Only x0..123/y10..63 may be touched. */
void cv_paint(uint8_t pixels[1024],const CvDisplay *);
#endif
