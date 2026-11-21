#ifndef DK_TRACK_CAPTURE_H
#define DK_TRACK_CAPTURE_H

#include "track-service.h"

enum { TK_CAPTURE_OFF, TK_CAPTURE_LIVE, TK_CAPTURE_STEP, TK_CAPTURE_HELD };

/* Native recording still determines placement. Ingress and note-list commits
 * are paired by track, pitch and admission order before storing an extension.
 */
uint32_t tk_capture_midi_mode(void);
bool tk_capture_input(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                      uint8_t velocity, bool step_record);
bool tk_capture_input_at(uint8_t track, uint8_t source, uint8_t channel, uint32_t id, uint8_t pitch,
                         uint8_t velocity, bool step_record, uint32_t now);
void tk_capture_shape_changed(uint8_t track);
void tk_capture_panel_press(uint32_t key, uint32_t track, uint32_t pitch, uint32_t velocity,
                            uint32_t step_record);
void tk_capture_panel_release(uint32_t key);
void tk_capture_note_commit(uint32_t note_table, uint32_t track, uint32_t step, uint32_t pitch,
                            uint32_t velocity);
void tk_capture_poll(uint32_t edit_pattern, uint16_t bpm);

#endif
