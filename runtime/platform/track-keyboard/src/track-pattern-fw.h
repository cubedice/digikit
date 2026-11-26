#ifndef DK_TRACK_PATTERN_FW_H
#define DK_TRACK_PATTERN_FW_H
#include "track-service.h"

TkPattern *tk_pattern_fw_get(uint32_t address);
const TkPattern *tk_pattern_fw_existing(uint32_t address);
TkPattern *tk_pattern_fw_peek(uint32_t address);
uint32_t tk_pattern_fw_track_base(uint32_t address, uint8_t *track);
void tk_pattern_fw_changed(void);
uint32_t tk_pattern_fw_revision(void);
uint32_t tk_pattern_fw_selected(void);
bool tk_pattern_fw_note_step(uint32_t address, uint8_t track, uint8_t step);
/* Write the live settings of the project pattern at `address` into its record in the firmware's working copy (what a
 * power cycle brings back). False when the pattern is not a project pattern, the working copy holds no record for it, or
 * the settings do not fit. */
bool tk_pattern_fw_work_write(uint32_t address);

#endif
