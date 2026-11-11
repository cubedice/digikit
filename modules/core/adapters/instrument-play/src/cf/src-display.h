#ifndef DK_CANOPY_SRC_DISPLAY_H
#define DK_CANOPY_SRC_DISPLAY_H
#include "src-state.h"
#include "status.h"
typedef struct {
    char label[8][8], value[8][8], heading[22], mode[6], notes[4][6];
    uint8_t pending, membership[4], highlight, pedal, mix_bar, mix_requested, mix_applied, focused;
    uint8_t knob[8], rotary, fault;
} CvDisplay;
/* Snapshot in, bounded presentation out. Native values are already formatted
 * by their firmware owners; this code never invents a second TUNE/LEVEL scale. */
void cv_build(CvDisplay *,const CsState *,const IsStatus *,uint32_t now_ms,
    const char *native_tune,const char *native_level,int save_error);
/* Four-byte output including terminator, or five for C#-1. MIDI zero is valid. */
void cv_note(char out[6],uint32_t note,int valid);
/* Full native toast text for a custom edit; never used for native TUNE/LEV. */
void cv_feedback(char out[32],const CsState *,uint32_t encoder);
/* Caller supplies a locked mapper snapshot. Match full token and pitch, never
 * just a pitch or a slot reused by another performance generation. */
void cv_ownership(CvDisplay *,const IsStatus *,const KbState *,uint32_t now_ms);
#endif
